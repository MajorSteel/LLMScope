#include "plugins/plugin_manager.hpp"
#include <cmath>
#include <spdlog/spdlog.h>

std::vector<float> AttentionEntropyPlugin::last_entropies_ = {};
std::mutex AttentionEntropyPlugin::static_mutex_ = {};

QuantizationMemoryPlugin::MemoryProjection QuantizationMemoryPlugin::last_projection_ = {};
std::mutex QuantizationMemoryPlugin::static_mutex_ = {};

PluginManager::PluginManager(EventBus& event_bus) : event_bus_(event_bus) {}

void PluginManager::register_plugin(std::unique_ptr<AnalyzerPlugin> plugin) {
    std::lock_guard<std::mutex> lock(mutex_);
    spdlog::info("Registering analyzer plugin: {}", plugin->get_name());
    plugins_.push_back(std::move(plugin));
}

void PluginManager::process_event(const TelemetryEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& plugin : plugins_) {
        try {
            plugin->process_event(event, event_bus_);
        } catch (const std::exception& e) {
            spdlog::error("Error in plugin {}: {}", plugin->get_name(), e.what());
        }
    }
}

std::vector<std::string> PluginManager::get_loaded_plugins() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    for (const auto& plugin : plugins_) {
        names.push_back(plugin->get_name());
    }
    return names;
}

// AttentionEntropyPlugin Implementation
void AttentionEntropyPlugin::process_event(const TelemetryEvent& event, EventBus& bus) {
    if (event.event_type != "attention_weights") return;

    const auto& attn = event.attention;
    if (attn.matrices.empty() || attn.token_count == 0) return;

    std::vector<float> head_entropies;
    head_entropies.reserve(attn.num_heads);

    for (int h = 0; h < attn.num_heads; ++h) {
        const auto& head = attn.matrices[h];
        double total_entropy = 0.0;
        
        for (int q = 0; q < attn.token_count; ++q) {
            const auto& row = head[q];
            double row_entropy = 0.0;
            
            for (int k = 0; k < attn.token_count; ++k) {
                float w = row[k];
                if (w > 1e-6f) {
                    row_entropy -= w * std::log2(w);
                }
            }
            total_entropy += row_entropy;
        }
        
        // Average entropy over query tokens
        head_entropies.push_back(static_cast<float>(total_entropy / attn.token_count));
    }

    {
        std::lock_guard<std::mutex> lock(static_mutex_);
        last_entropies_ = head_entropies;
    }

    // Publish helper anomaly if entropy is extremely low (meaning attention is collapsed or overfocused)
    for (size_t h = 0; h < head_entropies.size(); ++h) {
        if (head_entropies[h] < 0.1f && attn.token_count > 4) {
            TelemetryEvent alert_event;
            alert_event.event_type = "anomaly";
            alert_event.timestamp = event.timestamp;
            alert_event.anomaly.timestamp = "Now";
            alert_event.anomaly.severity = "WARNING";
            alert_event.anomaly.layer_name = attn.layer_name;
            alert_event.anomaly.description = "Attention Collapse: Head " + std::to_string(h) + " entropy is extremely low (" + std::to_string(head_entropies[h]) + ").";
            bus.publish(alert_event);
        }
    }
}

// QuantizationMemoryPlugin Implementation
void QuantizationMemoryPlugin::process_event(const TelemetryEvent& event, EventBus& bus) {
    if (event.event_type != "model_info") return;

    const auto& model = event.model_info;
    
    // Estimate parameters: standard Transformer parameters estimate
    // e.g. Llama-3-8B has 32 layers, 4096 hidden, 32 heads, 128k vocab
    double num_params = 0.0;
    if (model.layers > 0 && model.hidden_size > 0) {
        double layers = model.layers;
        double h = model.hidden_size;
        
        // QKV projections + Output projection = 4 * h * h per layer
        // MLP (Gate, Up, Down) = 3 * h * (h * 8/3) per layer (SwiGLU) = 8 * h * h
        double attention_params = 4.0 * h * h;
        double mlp_params = 8.0 * h * h;
        double layer_params = layers * (attention_params + mlp_params);
        double vocab_params = static_cast<double>(model.vocab_size) * h;
        
        num_params = layer_params + vocab_params;
    }
    
    if (num_params == 0.0) {
        // Default estimate: 8 Billion parameters if invalid info
        num_params = 8.0e9;
    }

    MemoryProjection proj;
    proj.fp32_mb = (num_params * 4.0) / (1024.0 * 1024.0);
    proj.fp16_mb = (num_params * 2.0) / (1024.0 * 1024.0);
    proj.int8_mb = (num_params * 1.0) / (1024.0 * 1024.0);
    proj.q4_mb   = (num_params * 0.5) / (1024.0 * 1024.0); // 4-bit quantization

    {
        std::lock_guard<std::mutex> lock(static_mutex_);
        last_projection_ = proj;
    }
}
