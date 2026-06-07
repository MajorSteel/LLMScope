# LLMScope Telemetry Protocol Specification (v1.0)

LLMScope uses a line-oriented JSON protocol over TCP (default port `5005`) to receive real-time execution telemetry from local large language models. Every packet transmitted must be a single-line JSON string followed by a newline (`\n`).

---

## 1. Event Envelope
Every message contains an `event_type` and a `timestamp` field at its root:

```json
{
  "event_type": "string",
  "timestamp": 1717800000,
  "payload": {}
}
```

- `event_type`: The type of event being reported. Supported values: `model_info`, `layer_trace`, `attention_weights`, `anomaly`.
- `timestamp`: Unix epoch timestamp in milliseconds, or elapsed time.

---

## 2. Event Types

### 2.1 Model Information (`model_info`)
Sent once at the start of a session or generation loop to register the model topology.

**Payload Fields:**
- `name` (string): Model name (e.g. `llama-3-8b`).
- `layers` (integer): Total number of transformer layers.
- `hidden_size` (integer): Hidden dimension size (e.g. `4096`).
- `num_heads` (integer): Number of attention heads.
- `vocab_size` (integer): Size of vocabulary.
- `quantization` (string): Quantization type (e.g., `FP16`, `Q4_K_M`, `INT8`, `FP32`).

**Example:**
```json
{
  "event_type": "model_info",
  "timestamp": 1717800001000,
  "payload": {
    "name": "llama-3-8b",
    "layers": 32,
    "hidden_size": 4096,
    "num_heads": 32,
    "vocab_size": 128256,
    "quantization": "Q4_K_M"
  }
}
```

---

### 2.2 Layer Trace (`layer_trace`)
Sent immediately after a submodule completes execution (forward pass).

**Payload Fields:**
- `event_id` (integer): Monotonically increasing ID.
- `layer_name` (string): Canonical layer path (e.g. `layers.1.attn` or `layers.0.mlp.act`).
- `layer_type` (string): Category (e.g. `Embedding`, `SelfAttention`, `MLP`, `LayerNorm`, `RMSNorm`, `LMHead`).
- `device` (string): Execution unit (e.g., `CUDA:0`, `CPU`).
- `latency_ms` (double): Execution latency of the submodule in milliseconds.
- `input` (object): Metadata for the input tensor.
  - `shape` (array of integers): Dimensions of the tensor.
  - `dtype` (string): Data type (`float16`, `float32`, `int8`, etc.).
  - `size_bytes` (integer): Size in bytes.
  - `device` (string): Tensor location.
- `output` (object): Metadata for the output tensor (same structure as `input`).
- `stats` (object): Statistical aggregates of the activation tensor.
  - `mean` (double): Mean value of activations.
  - `variance` (double): Variance of activations.
  - `min` (double): Minimum activation value.
  - `max` (double): Maximum activation value.
  - `sparsity` (double): Sparsity percentage (0.0 to 100.0).

**Example:**
```json
{
  "event_type": "layer_trace",
  "timestamp": 1717800001050,
  "payload": {
    "event_id": 102,
    "layer_name": "layers.1.attn",
    "layer_type": "SelfAttention",
    "device": "CUDA:0",
    "latency_ms": 1.142,
    "input": {
      "shape": [1, 32, 4096],
      "dtype": "float16",
      "size_bytes": 262144,
      "device": "CUDA:0"
    },
    "output": {
      "shape": [1, 32, 4096],
      "dtype": "float16",
      "size_bytes": 262144,
      "device": "CUDA:0"
    },
    "stats": {
      "mean": 0.031,
      "variance": 0.533,
      "min": -4.12,
      "max": 5.73,
      "sparsity": 54.2
    }
  }
}
```

---

### 2.3 Attention Weights (`attention_weights`)
Sent along with `layer_trace` for attention submodules. Contains the weights or probabilities of attention heads.

**Payload Fields:**
- `layer_name` (string): Associated layer name.
- `num_heads` (integer): Number of heads inside this matrix.
- `token_count` (integer): Length of the sequence (rows/columns).
- `tokens` (array of strings): Token labels for rows/columns.
- `matrices` (array of arrays of arrays of doubles): A 3D array of size `[num_heads][token_count][token_count]` storing attention weights. For large context windows, the sender must apply compression (e.g. sliding window or downsampling) before serialization.

**Example:**
```json
{
  "event_type": "attention_weights",
  "timestamp": 1717800001055,
  "payload": {
    "layer_name": "layers.1.attn",
    "num_heads": 8,
    "token_count": 4,
    "tokens": ["I", "love", "observability", "."],
    "matrices": [
      [
        [1.0, 0.0, 0.0, 0.0],
        [0.1, 0.9, 0.0, 0.0],
        [0.05, 0.15, 0.8, 0.0],
        [0.1, 0.2, 0.3, 0.4]
      ]
    ]
  }
}
```

---

### 2.4 Anomaly Alert (`anomaly`)
Generated when numerical or system instability is detected.

**Payload Fields:**
- `severity` (string): Level of warning (`WARNING`, `ERROR`).
- `layer_name` (string): Associated module.
- `description` (string): Descriptive details of the anomaly.

**Example:**
```json
{
  "event_type": "anomaly",
  "timestamp": 1717800001080,
  "payload": {
    "severity": "WARNING",
    "layer_name": "layers.22.mlp",
    "description": "Exploding Activations: Max value 12.5 exceeds threshold 10.0"
  }
}
```
