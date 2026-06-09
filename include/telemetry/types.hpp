#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

struct ModelInfo {
    std::string name = "unknown";
    int layers = 0;
    int hidden_size = 0;
    int num_heads = 0;
    int vocab_size = 0;
    std::string quantization = "FP32";

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ModelInfo, name, layers, hidden_size, num_heads, vocab_size, quantization)
};

struct TensorInfo {
    std::vector<int64_t> shape;
    std::string dtype = "float32";
    size_t size_bytes = 0;
    std::string device = "CPU";

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(TensorInfo, shape, dtype, size_bytes, device)
};

struct TensorStats {
    double mean = 0.0;
    double variance = 0.0;
    double min_val = 0.0;
    double max_val = 0.0;
    double sparsity = 0.0; // 0.0 to 100.0

    friend void to_json(nlohmann::json& j, const TensorStats& s) {
        j = nlohmann::json{
            {"mean", s.mean},
            {"variance", s.variance},
            {"min", s.min_val},
            {"max", s.max_val},
            {"sparsity", s.sparsity}
        };
    }

    friend void from_json(const nlohmann::json& j, TensorStats& s) {
        j.at("mean").get_to(s.mean);
        j.at("variance").get_to(s.variance);
        if (j.contains("min_val")) {
            j.at("min_val").get_to(s.min_val);
        } else if (j.contains("min")) {
            j.at("min").get_to(s.min_val);
        }
        if (j.contains("max_val")) {
            j.at("max_val").get_to(s.max_val);
        } else if (j.contains("max")) {
            j.at("max").get_to(s.max_val);
        }
        j.at("sparsity").get_to(s.sparsity);
    }
};

struct AttentionData {
    std::string layer_name = "";
    int num_heads = 0;
    int token_count = 0;
    std::vector<std::string> tokens;
    std::vector<std::vector<std::vector<float>>> matrices; // [head][query][key]

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(AttentionData, layer_name, num_heads, token_count, tokens, matrices)
};

struct AnomalyAlert {
    std::string timestamp = "";
    std::string severity = "WARNING"; // "WARNING", "ERROR"
    std::string layer_name = "";
    std::string description = "";

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(AnomalyAlert, timestamp, severity, layer_name, description)
};

struct SemanticItem {
    std::string token = "";
    double score = 0.0;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(SemanticItem, token, score)
};

struct SemanticNeighbors {
    int token_index = 0;
    std::string token_text = "";
    std::vector<SemanticItem> top_k;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(SemanticNeighbors, token_index, token_text, top_k)
};

struct TelemetryEvent {
    std::string event_type; // "model_info", "layer_trace", "attention_weights", "anomaly"
    int64_t timestamp = 0;

    // Payload structures (only populated depending on event_type)
    ModelInfo model_info;
    
    // layer_trace fields
    int64_t event_id = 0;
    std::string layer_name = "";
    std::string layer_type = "";
    std::string device = "CPU";
    double latency_ms = 0.0;
    TensorInfo input_tensor;
    TensorInfo output_tensor;
    TensorStats activation_stats;
    std::vector<SemanticNeighbors> semantic_neighbors;

    // attention_weights fields
    AttentionData attention;

    // anomaly fields
    AnomalyAlert anomaly;

    // Custom serialization to support envelope-payload decoupling
    friend void to_json(nlohmann::json& j, const TelemetryEvent& e) {
        j = nlohmann::json{
            {"event_type", e.event_type},
            {"timestamp", e.timestamp}
        };
        if (e.event_type == "model_info") {
            j["payload"] = e.model_info;
        } else if (e.event_type == "layer_trace") {
            j["payload"] = nlohmann::json{
                {"event_id", e.event_id},
                {"layer_name", e.layer_name},
                {"layer_type", e.layer_type},
                {"device", e.device},
                {"latency_ms", e.latency_ms},
                {"input", e.input_tensor},
                {"output", e.output_tensor},
                {"stats", e.activation_stats},
                {"semantic_neighbors", e.semantic_neighbors}
            };
        } else if (e.event_type == "attention_weights") {
            j["payload"] = e.attention;
        } else if (e.event_type == "anomaly") {
            j["payload"] = e.anomaly;
        }
    }

    friend void from_json(const nlohmann::json& j, TelemetryEvent& e) {
        j.at("event_type").get_to(e.event_type);
        j.at("timestamp").get_to(e.timestamp);
        
        if (j.contains("payload") && !j["payload"].is_null()) {
            const auto& p = j["payload"];
            if (e.event_type == "model_info") {
                p.get_to(e.model_info);
            } else if (e.event_type == "layer_trace") {
                p.at("event_id").get_to(e.event_id);
                p.at("layer_name").get_to(e.layer_name);
                p.at("layer_type").get_to(e.layer_type);
                p.at("device").get_to(e.device);
                p.at("latency_ms").get_to(e.latency_ms);
                p.at("input").get_to(e.input_tensor);
                p.at("output").get_to(e.output_tensor);
                p.at("stats").get_to(e.activation_stats);
                if (p.contains("semantic_neighbors")) {
                    p.at("semantic_neighbors").get_to(e.semantic_neighbors);
                }
            } else if (e.event_type == "attention_weights") {
                p.get_to(e.attention);
            } else if (e.event_type == "anomaly") {
                p.get_to(e.anomaly);
            }
        }
    }
};
