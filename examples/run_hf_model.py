import sys
import os
import torch
import time
import argparse

# Add parent directory to path to import local module
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
from llmscope_pytorch import LLMScopeHookManager

# Attempt to import HuggingFace Transformers
try:
    from transformers import AutoModelForCausalLM, AutoTokenizer
    HAS_TRANSFORMERS = True
except ImportError:
    HAS_TRANSFORMERS = False

class ToyCausalLM(torch.nn.Module):
    """
    A simple lightweight PyTorch-only Causal LM (Toy Model) 
    used as a fallback when transformers library is not installed.
    """
    def __init__(self, vocab_size=1000, d_model=256, n_layers=4, n_heads=8):
        super().__init__()
        class Config:
            pass
        
        self.config = Config()
        self.config.num_hidden_layers = n_layers
        self.config.hidden_size = d_model
        self.config.num_attention_heads = n_heads
        self.config.vocab_size = vocab_size
        self.embed = torch.nn.Embedding(vocab_size, d_model)
        
        # Define toy layers
        self.layers = torch.nn.ModuleList()
        for i in range(n_layers):
            layer = torch.nn.Module()
            layer.input_layernorm = torch.nn.LayerNorm(d_model)
            layer.self_attn = torch.nn.Linear(d_model, d_model) # Mock attention projection
            layer.post_attention_layernorm = torch.nn.LayerNorm(d_model)
            layer.mlp = torch.nn.Sequential(
                torch.nn.Linear(d_model, d_model * 4),
                torch.nn.GELU(),
                torch.nn.Linear(d_model * 4, d_model)
            )
            self.layers.append(layer)
            
        self.norm = torch.nn.LayerNorm(d_model)
        self.lm_head = torch.nn.Linear(d_model, vocab_size)

    def forward(self, input_ids):
        # Cache mock attention weights on modules for hook extraction
        x = self.embed(input_ids)
        seq_len = input_ids.shape[1]
        
        for layer in self.layers:
            # Generate mock causal attention weights for hooks
            h = self.config.num_attention_heads
            mock_attn = torch.softmax(torch.randn(1, h, seq_len, seq_len), dim=-1)
            # Store on self_attn module so hook can read it
            layer.self_attn.attn_weights = mock_attn
            
            # Layer norm + Attn
            x_norm = layer.input_layernorm(x)
            attn_out = layer.self_attn(x_norm)
            x = x + attn_out
            
            # Layer norm + MLP
            x_norm2 = layer.post_attention_layernorm(x)
            mlp_out = layer.mlp(x_norm2)
            x = x + mlp_out
            
        x = self.norm(x)
        logits = self.lm_head(x)
        return logits

def run_telemetry_demo():
    parser = argparse.ArgumentParser(description="LLMScope PyTorch Telemetry Tracing Demo")
    parser.add_argument("--model", type=str, default="sshleifer/tiny-gpt2", 
                        help="HuggingFace model path or name (e.g. 'gpt2', 'sshleifer/tiny-gpt2', 'TinyLlama/TinyLlama-1.1B-Chat-v1.0') or 'toy'")
    parser.add_argument("--prompt", type=str, default="Observability is critical for transformers because",
                        help="Prompt to run generation on")
    parser.add_argument("--steps", type=int, default=15, help="Number of steps/tokens to generate")
    parser.add_argument("--host", type=str, default="127.0.0.1", help="LLMScope TCP server host")
    parser.add_argument("--port", type=int, default=5005, help="LLMScope TCP server port")
    args = parser.parse_args()

    print("=============================================================")
    print(" LLMScope PyTorch Tracing Integration Demo")
    print("=============================================================")

    model = None
    tokenizer = None
    model_name_display = args.model

    # 1. Initialize or download model
    if HAS_TRANSFORMERS and args.model.lower() != "toy":
        print(f"Loading HuggingFace model '{args.model}'...")
        try:
            tokenizer = AutoTokenizer.from_pretrained(args.model)
            model = AutoModelForCausalLM.from_pretrained(args.model)
            # Force HF model to cache attention weights in forward pass
            model.config.output_attentions = True
        except Exception as e:
            print(f"HuggingFace model download/load failed ({e}). Falling back to local Toy Model.")
            model = ToyCausalLM()
            model_name_display = "toy_fallback"
    else:
        if args.model.lower() == "toy":
            print("Launching custom Toy Causal LM as requested.")
        else:
            print("HuggingFace 'transformers' library not found. Launching custom Toy Causal LM.")
        model = ToyCausalLM()
        model_name_display = "toy_gpt_256"

    # 2. Attach LLMScope telemetry manager
    manager = LLMScopeHookManager(host=args.host, port=args.port)
    manager.register_model(model_name_display, model)

    if not manager.connected:
        print("\n[WARNING] Telemetry server not found on port 5005.")
        print("Ensure you have built and launched the C++ server first!")
        print("Model will run locally but no events will be streamed.\n")

    # 3. Execution Prompt Loop
    print(f"\nRunning generation on input: '{args.prompt}'...")
    
    if tokenizer:
        input_ids = tokenizer.encode(args.prompt, return_tensors="pt")
    else:
        # Mock tokenization for Toy LM
        input_ids = torch.randint(0, 1000, (1, 10))

    # Generation Loop (token by token)
    for step in range(args.steps):
        with torch.no_grad():
            # Pass output_attentions=True explicitly to ensure weights are generated
            if hasattr(model, "config") and getattr(model.config, "output_attentions", False):
                outputs = model(input_ids, output_attentions=True)
            else:
                outputs = model(input_ids)
                
            if isinstance(outputs, torch.Tensor):
                logits = outputs
            else:
                logits = outputs.logits
                
            next_token_logits = logits[:, -1, :]
            next_token = torch.argmax(next_token_logits, dim=-1).unsqueeze(-1)
            
            # Append next token to input
            input_ids = torch.cat([input_ids, next_token], dim=-1)
            
            # Print generation progress
            if tokenizer:
                decoded = tokenizer.decode(input_ids[0].tolist())
                print(f"Generated text: {decoded}")
            else:
                print(f"Generated token ID: {next_token.item()} (seq length: {input_ids.shape[1]})")
            
            time.sleep(0.5) # slowdown to visualize trace streams in dashboard

    manager.close()
    print("\nDemonstration inference trace finished successfully.")

if __name__ == "__main__":
    run_telemetry_demo()
