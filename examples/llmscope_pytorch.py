import socket
import json
import time
import torch
import torch.nn as nn
import numpy as np

class LLMScopeHookManager:
    """
    Non-invasive instrumentation hook for PyTorch / HuggingFace models.
    Intercepts activations, attention weights, shapes, and latencies,
    and streams them in real-time to the C++ LLMScope TUI/Web dashboard.
    """
    def __init__(self, host="127.0.0.1", port=5005, sample_top_k=8):
        self.host = host
        self.port = port
        self.sample_top_k = sample_top_k
        self.sock = None
        self.connected = False
        self.event_id = 1000
        self.layer_latencies = {}
        self.tokenizer = None
        self.current_input_ids = []
        self.embed_weights_norm = None

    def connect(self):
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.settimeout(2.0)
            self.sock.connect((self.host, self.port))
            self.connected = True
            print(f"[LLMScope] Connected to telemetry dashboard on {self.host}:{self.port}")
        except Exception as e:
            self.connected = False
            self.sock = None
            print(f"[LLMScope] Connection failed. Streaming disabled, running model normally. Error: {e}")

    def send_event(self, event_type, payload):
        if not self.connected or not self.sock:
            return
        
        event = {
            "event_type": event_type,
            "timestamp": int(time.time() * 1000),
            "payload": payload
        }
        
        try:
            packet = json.dumps(event) + "\n"
            self.sock.sendall(packet.encode('utf-8'))
        except Exception as e:
            print(f"[LLMScope] Telemetry send error: {e}. Disabling connection.")
            self.connected = False
            try:
                self.sock.close()
            except:
                pass
            self.sock = None

    def register_model(self, name, model, tokenizer=None):
        """Walks model layers and registers forward hooks to inject telemetry collectors."""
        self.tokenizer = tokenizer
        self.connect()

        # Get embedding weight and pre-normalize for cosine similarity
        self.embed_weights_norm = None
        if self.tokenizer is not None:
            try:
                embed_layer = model.get_input_embeddings()
                if embed_layer is not None:
                    W_embed = embed_layer.weight.detach().cpu()
                    # Pre-normalize: W / ||W||
                    norm = W_embed.norm(dim=-1, keepdim=True)
                    norm[norm == 0] = 1.0
                    self.embed_weights_norm = (W_embed / norm).float()
                    print("[LLMScope] Vocabulary embeddings captured and pre-normalized.")
            except Exception as e:
                print(f"[LLMScope] Could not extract input embeddings for semantic analysis: {e}")

        # Gather model parameters metadata
        layers_count = 0
        hidden_size = 4096
        num_heads = 32
        vocab_size = 32000

        # Try to infer details from config
        if hasattr(model, "config"):
            config = model.config
            layers_count = getattr(config, "num_hidden_layers", 0)
            hidden_size = getattr(config, "hidden_size", 4096)
            num_heads = getattr(config, "num_attention_heads", 32)
            vocab_size = getattr(config, "vocab_size", 32000)
            
        if layers_count == 0:
            # Fallback counting transformer blocks
            for name_module, module in model.named_modules():
                if "layer" in name_module.lower() and name_module.count('.') == 1:
                    layers_count += 1

        # Register metadata
        self.send_event("model_info", {
            "name": name,
            "layers": layers_count,
            "hidden_size": hidden_size,
            "num_heads": num_heads,
            "vocab_size": vocab_size,
            "quantization": "FP32"
        })

        # Attach hooks
        for name_module, module in model.named_modules():
            # Standard hooks for attention, mlp, normalization, and embeddings
            layer_type = None
            name_lower = name_module.lower()
            
            if "embed" in name_lower:
                layer_type = "Embedding"
            elif "attn" in name_lower or "attention" in name_lower:
                if name_lower.endswith("attn") or name_lower.endswith("attention") or "self_attn" in name_lower:
                    layer_type = "SelfAttention"
            elif "mlp" in name_lower or "feed_forward" in name_lower:
                if name_lower.endswith("mlp") or name_lower.endswith("feed_forward"):
                    layer_type = "MLP"
            elif "norm" in name_lower:
                layer_type = "RMSNorm" if "rms" in name_lower else "LayerNorm"
            elif "lm_head" in name_lower or name_lower == "output":
                layer_type = "LMHead"
                
            if layer_type:
                module.register_forward_pre_hook(self._make_pre_hook(name_module))
                module.register_forward_hook(self._make_post_hook(name_module, layer_type))

        print(f"[LLMScope] Instrumentation completed. Hooks attached successfully.")

    def _make_pre_hook(self, layer_name):
        def pre_hook(module, input):
            self.layer_latencies[layer_name] = time.perf_counter()
        return pre_hook

    def _make_post_hook(self, layer_name, layer_type):
        def post_hook(module, input, output):
            # Calculate latency
            start_time = self.layer_latencies.get(layer_name, None)
            latency_ms = (time.perf_counter() - start_time) * 1000.0 if start_time else 0.0
            
            # Resolve tensors shapes
            input_tensor = input[0] if isinstance(input, tuple) and len(input) > 0 else input
            output_tensor = output[0] if isinstance(output, tuple) and len(output) > 0 else output
            
            inp_shape = list(input_tensor.shape) if hasattr(input_tensor, 'shape') else []
            out_shape = list(output_tensor.shape) if hasattr(output_tensor, 'shape') else []
            
            inp_dtype = str(input_tensor.dtype).split('.')[-1] if hasattr(input_tensor, 'dtype') else "unknown"
            out_dtype = str(output_tensor.dtype).split('.')[-1] if hasattr(output_tensor, 'dtype') else "unknown"
            
            inp_bytes = input_tensor.nelement() * input_tensor.element_size() if hasattr(input_tensor, 'nelement') else 0
            out_bytes = output_tensor.nelement() * output_tensor.element_size() if hasattr(output_tensor, 'nelement') else 0
            
            device_str = str(output_tensor.device) if hasattr(output_tensor, 'device') else "CPU"
            
            # Retrieve stats
            stats = {"mean": 0.0, "variance": 0.0, "min": 0.0, "max": 0.0, "sparsity": 0.0}
            if isinstance(output_tensor, torch.Tensor):
                with torch.no_grad():
                    flat_tensor = output_tensor.detach().float()
                    stats["mean"] = float(flat_tensor.mean().item())
                    stats["variance"] = float(flat_tensor.var().item()) if flat_tensor.nelement() > 1 else 0.0
                    stats["min"] = float(flat_tensor.min().item())
                    stats["max"] = float(flat_tensor.max().item())
                    
                    # Sparsity rate (% of values near 0)
                    zero_mask = torch.abs(flat_tensor) < 1e-5
                    stats["sparsity"] = float((zero_mask.sum().item() / flat_tensor.nelement()) * 100.0)

            # Capture input token IDs if this is the embedding layer
            if layer_type == "Embedding":
                with torch.no_grad():
                    self.current_input_ids = input_tensor[0].detach().cpu().tolist() if hasattr(input_tensor, 'tolist') else []

            # Compute nearest semantic neighbors using cosine similarity
            semantic_neighbors = []
            if self.embed_weights_norm is not None and isinstance(output_tensor, torch.Tensor) and len(out_shape) == 3:
                try:
                    with torch.no_grad():
                        # shape: [batch, seq_len, hidden_size] -> [seq_len, hidden_size]
                        h_states = output_tensor[0].detach().cpu().float()
                        if h_states.shape[-1] == self.embed_weights_norm.shape[1]:
                            h_norms = h_states.norm(dim=-1, keepdim=True)
                            h_norms[h_norms == 0] = 1.0
                            h_states_normalized = h_states / h_norms
                            
                            # similarities shape: [seq_len, vocab_size]
                            similarities = torch.matmul(h_states_normalized, self.embed_weights_norm.T)
                            top_k_val, top_k_idx = torch.topk(similarities, k=min(10, similarities.shape[-1]), dim=-1)
                            
                            for seq_idx in range(h_states.shape[0]):
                                items = []
                                for kidx in range(top_k_val.shape[1]):
                                    idx_val = int(top_k_idx[seq_idx, kidx].item())
                                    score_val = float(top_k_val[seq_idx, kidx].item())
                                    token_text = "unknown"
                                    if self.tokenizer is not None:
                                        try:
                                            token_text = self.tokenizer.convert_ids_to_tokens(idx_val)
                                            if hasattr(self.tokenizer, 'convert_tokens_to_string'):
                                                token_text = self.tokenizer.convert_tokens_to_string([token_text])
                                        except:
                                            token_text = f"id_{idx_val}"
                                    items.append({"token": token_text, "score": score_val})
                                    
                                curr_tok_text = f"tok_{seq_idx}"
                                if self.tokenizer is not None and seq_idx < len(self.current_input_ids):
                                    try:
                                        curr_tok_text = self.tokenizer.decode([self.current_input_ids[seq_idx]])
                                    except:
                                        pass
                                        
                                semantic_neighbors.append({
                                    "token_index": seq_idx,
                                    "token_text": curr_tok_text,
                                    "top_k": items
                                })
                except Exception as e:
                    print(f"[LLMScope] Cosine similarity error at layer {layer_name}: {e}")

            # Send main layer trace event
            self.send_event("layer_trace", {
                "event_id": self.event_id,
                "layer_name": layer_name,
                "layer_type": layer_type,
                "device": device_str.upper(),
                "latency_ms": latency_ms,
                "input": {
                    "shape": inp_shape,
                    "dtype": inp_dtype,
                    "size_bytes": inp_bytes,
                    "device": device_str.upper()
                },
                "output": {
                    "shape": out_shape,
                    "dtype": out_dtype,
                    "size_bytes": out_bytes,
                    "device": device_str.upper()
                },
                "stats": stats,
                "semantic_neighbors": semantic_neighbors
            })
            self.event_id += 1

            # If SelfAttention, capture and compress weights
            if layer_type == "SelfAttention":
                self._capture_attention_weights(module, layer_name, out_shape, output)

        return post_hook

    def _capture_attention_weights(self, module, layer_name, out_shape, output):
        """Attempts to extract attention scores/probabilities from attention modules."""
        attn_matrix = None
        
        # Check output tuple first (standard for HF models when output_attentions=True)
        if isinstance(output, tuple) and len(output) > 1:
            for item in output[1:]:
                if isinstance(item, torch.Tensor) and len(item.shape) >= 3:
                    attn_matrix = item
                    break
                    
        # Fallback to module attributes
        if attn_matrix is None:
            for attr in ['attn_weights', 'attention_weights', 'attn_probs', 'probs']:
                if hasattr(module, attr):
                    attn_matrix = getattr(module, attr)
                    break
                
        if attn_matrix is None:
            return # Skip if attention weights are not exposed on the module
            
        with torch.no_grad():
            # Ensure it is on CPU and flattened
            matrix_cpu = attn_matrix.detach().cpu().float().numpy()
            
            # Format: [num_heads, seq_len, seq_len]
            if len(matrix_cpu.shape) == 4: # batch, heads, q, k
                matrix_cpu = matrix_cpu[0] # take first batch element
                
            num_heads, seq_len, _ = matrix_cpu.shape
            
            # Compress to Top-K to avoid RAM/socket explosion
            k = min(self.sample_top_k, seq_len)
            compressed_matrices = []
            
            for h in range(num_heads):
                head_matrix = []
                for q in range(seq_len):
                    row = matrix_cpu[h, q].tolist()
                    # Apply Top-K sampling
                    threshold = sorted(row, reverse=True)[k - 1]
                    compressed_row = [w if w >= threshold else 0.0 for w in row]
                    head_matrix.append(compressed_row)
                compressed_matrices.append(head_matrix)

            tokens = [f"tok_{i}" for i in range(seq_len)]
            
            self.send_event("attention_weights", {
                "layer_name": layer_name,
                "num_heads": num_heads,
                "token_count": seq_len,
                "tokens": tokens,
                "matrices": compressed_matrices
            })

    def close(self):
        if self.sock:
            try:
                self.sock.close()
            except:
                pass
            self.sock = None
            self.connected = False
