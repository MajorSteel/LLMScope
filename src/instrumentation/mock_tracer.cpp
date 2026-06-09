#include "instrumentation/mock_tracer.hpp"
#include <chrono>
#include <cmath>
#include <random>
#include <iomanip>
#include <sstream>
#include <spdlog/spdlog.h>

MockTracer::MockTracer(EventBus& event_bus, DeviceMonitor& device_monitor) 
    : event_bus_(event_bus), device_monitor_(device_monitor) {}

MockTracer::~MockTracer() {
    stop();
}

void MockTracer::start() {
    if (running_) return;
    running_ = true;
    runner_thread_ = std::thread(&MockTracer::simulation_loop, this);
}

void MockTracer::stop() {
    running_ = false;
    if (runner_thread_.joinable()) {
        runner_thread_.join();
    }
}

static std::string get_mock_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ) % 1000;

    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%H:%M:%S")
       << "." << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

void MockTracer::simulation_loop() {
    int token_idx = 0;
    int total_tokens = 30; // generate 30 tokens in loop
    int64_t global_event_id = 1000;

    spdlog::info("Starting telemetry simulation loop.");

    // Step 1: Send Model Info
    {
        TelemetryEvent init_ev;
        init_ev.event_type = "model_info";
        init_ev.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        init_ev.model_info.name = "llama-3-8b";
        init_ev.model_info.layers = 32;
        init_ev.model_info.hidden_size = 4096;
        init_ev.model_info.num_heads = 32;
        init_ev.model_info.vocab_size = 128256;
        init_ev.model_info.quantization = "Q4_K_M";
        event_bus_.publish(init_ev);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    while (running_ && token_idx < total_tokens) {
        generate_token_pass(token_idx, total_tokens, global_event_id);
        token_idx++;
        // Speed control: ~4 tokens per second
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    spdlog::info("Telemetry simulation loop finished.");
    running_ = false;
}

static std::vector<SemanticNeighbors> generate_mock_neighbors(int layer, const std::vector<std::string>& active_tokens) {
    std::vector<SemanticNeighbors> result;
    for (size_t i = 0; i < active_tokens.size(); ++i) {
        std::string tok = active_tokens[i];
        SemanticNeighbors sn;
        sn.token_index = static_cast<int>(i);
        sn.token_text = tok;

        // Custom mappings for key tokens
        if (tok == "LLMScope") {
            if (layer < 10) {
                sn.top_k = {
                    {"LLM", 0.85}, {"Scope", 0.82}, {"scope", 0.78}, {"LLMs", 0.75}, {"Llm", 0.71},
                    {"scopes", 0.68}, {"SC", 0.65}, {"ope", 0.62}, {"micro", 0.59}, {"tele", 0.55}
                };
            } else if (layer < 20) {
                sn.top_k = {
                    {"profiler", 0.81}, {"debugger", 0.79}, {"analyzer", 0.76}, {"telemetry", 0.73}, {"tracing", 0.70},
                    {"monitor", 0.68}, {"tool", 0.65}, {"inspect", 0.62}, {"wrapper", 0.59}, {"logger", 0.55}
                };
            } else {
                sn.top_k = {
                    {"observability", 0.88}, {"interpretability", 0.84}, {"transparency", 0.80}, {"analysis", 0.77}, {"insight", 0.73},
                    {"metadata", 0.70}, {"diagnostics", 0.67}, {"metrics", 0.64}, {"dashboard", 0.61}, {"observing", 0.58}
                };
            }
        } else if (tok == "developer") {
            if (layer < 10) {
                sn.top_k = {
                    {"developers", 0.89}, {"develop", 0.82}, {"developing", 0.79}, {"development", 0.76}, {"dev", 0.72},
                    {"developed", 0.69}, {"devs", 0.66}, {"deve", 0.61}, {"eloper", 0.58}, {"coder", 0.55}
                };
            } else if (layer < 20) {
                sn.top_k = {
                    {"programmer", 0.86}, {"engineer", 0.83}, {"coder", 0.81}, {"architect", 0.77}, {"creator", 0.74},
                    {"builder", 0.70}, {"author", 0.67}, {"user", 0.64}, {"professional", 0.61}, {"designer", 0.58}
                };
            } else {
                sn.top_k = {
                    {"technology", 0.82}, {"software", 0.79}, {"engineering", 0.76}, {"creation", 0.73}, {"productivity", 0.70},
                    {"innovation", 0.67}, {"industry", 0.64}, {"computing", 0.61}, {"expertise", 0.58}, {"ecosystem", 0.55}
                };
            }
        } else if (tok == "observability") {
            if (layer < 10) {
                sn.top_k = {
                    {"observable", 0.88}, {"observation", 0.81}, {"observe", 0.78}, {"observing", 0.75}, {"observations", 0.72},
                    {"observes", 0.68}, {"ability", 0.65}, {"ility", 0.61}, {"view", 0.58}, {"monitor", 0.55}
                };
            } else if (layer < 20) {
                sn.top_k = {
                    {"monitoring", 0.85}, {"telemetry", 0.82}, {"visibility", 0.79}, {"tracing", 0.76}, {"logging", 0.73},
                    {"diagnostics", 0.70}, {"metrics", 0.67}, {"debugging", 0.64}, {"inspection", 0.61}, {"auditing", 0.58}
                };
            } else {
                sn.top_k = {
                    {"transparency", 0.86}, {"interpretability", 0.83}, {"understanding", 0.80}, {"explainability", 0.77}, {"reliability", 0.74},
                    {"assurance", 0.71}, {"governance", 0.68}, {"control", 0.65}, {"insight", 0.62}, {"quality", 0.59}
                };
            }
        } else if (tok == "attention") {
            if (layer < 10) {
                sn.top_k = {
                    {"attending", 0.85}, {"attend", 0.81}, {"attended", 0.77}, {"attentive", 0.74}, {"attent", 0.71},
                    {"attends", 0.67}, {"intent", 0.64}, {"tension", 0.61}, {"attentions", 0.58}, {"focus", 0.55}
                };
            } else if (layer < 20) {
                sn.top_k = {
                    {"focus", 0.87}, {"mechanism", 0.82}, {"weight", 0.79}, {"query", 0.76}, {"context", 0.73},
                    {"relevance", 0.70}, {"salience", 0.67}, {"scoring", 0.64}, {"importance", 0.61}, {"selection", 0.58}
                };
            } else {
                sn.top_k = {
                    {"cognition", 0.81}, {"alignment", 0.78}, {"representation", 0.75}, {"synthesis", 0.72}, {"intelligence", 0.69},
                    {"integration", 0.66}, {"dynamics", 0.63}, {"computation", 0.60}, {"architecture", 0.57}, {"semantics", 0.54}
                };
            }
        } else {
            // Generic token fallback based on layer
            if (layer < 10) {
                sn.top_k = {
                    {tok + "s", 0.85}, {tok + "ing", 0.80}, {tok + "ed", 0.75}, {"the_" + tok, 0.70}, {"a_" + tok, 0.65},
                    {"sub_" + tok, 0.60}, {tok + "_val", 0.55}, {"un_" + tok, 0.50}, {tok + "er", 0.45}, {tok + "ly", 0.40}
                };
            } else if (layer < 20) {
                sn.top_k = {
                    {"similar_" + tok, 0.80}, {"related_" + tok, 0.75}, {tok + "_context", 0.70}, {"core_" + tok, 0.65}, {tok + "_state", 0.60},
                    {"process_" + tok, 0.55}, {"linked_" + tok, 0.50}, {"about_" + tok, 0.45}, {"near_" + tok, 0.40}, {tok + "_base", 0.35}
                };
            } else {
                sn.top_k = {
                    {"concept_" + tok, 0.75}, {"semantic_" + tok, 0.70}, {"abstract_" + tok, 0.65}, {"theory_" + tok, 0.60}, {"domain_" + tok, 0.55},
                    {"system_" + tok, 0.50}, {"general_" + tok, 0.45}, {"global_" + tok, 0.40}, {"schema_" + tok, 0.35}, {"model_" + tok, 0.30}
                };
            }
        }
        result.push_back(sn);
    }
    return result;
}

void MockTracer::generate_token_pass(int token_idx, int total_tokens, int64_t& global_event_id) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::normal_distribution<double> lat_dis(0.4, 0.1); // latency per layer
    std::uniform_real_distribution<double> stats_dis(-1.0, 1.0);

    // List of simulated tokens
    std::vector<std::string> token_strings = {
        "LLMScope", "is", "a", "production", "quality", "developer", "tool", "for", 
        "local", "large", "language", "models", "providing", "deep", "observability", 
        "into", "transformer", "attention", "mechanics", "layer", "latency", "profiles", 
        "activation", "tensor", "statistics", "and", "numerical", "anomalies", "instantly", "."
    };

    int current_seq_len = std::min(token_idx + 1, static_cast<int>(token_strings.size()));
    std::vector<std::string> active_tokens(token_strings.begin(), token_strings.begin() + current_seq_len);

    uint64_t timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    // Dynamically set device based on hardware monitor availability
    bool gpu_ok = device_monitor_.get_stats().gpu_available;
    std::string target_device = gpu_ok ? "CUDA:0" : "CPU";

    // 1. Embeddings
    {
        TelemetryEvent ev;
        ev.event_type = "layer_trace";
        ev.timestamp = timestamp_ms;
        ev.event_id = global_event_id++;
        ev.layer_name = "embed_tokens";
        ev.layer_type = "Embedding";
        ev.device = target_device;
        ev.latency_ms = 0.5 + std::abs(lat_dis(gen));
        ev.input_tensor = TensorInfo{{1, current_seq_len}, "int32", static_cast<size_t>(current_seq_len * 4), target_device};
        ev.output_tensor = TensorInfo{{1, current_seq_len, 4096}, "float16", static_cast<size_t>(current_seq_len * 4096 * 2), target_device};
        ev.activation_stats = TensorStats{0.01, 1.02, -2.5, 2.4, 0.0};
        ev.semantic_neighbors = generate_mock_neighbors(0, active_tokens);
        event_bus_.publish(ev);
    }

    // 2. Transformer Blocks (32 Layers)
    for (int l = 0; l < 32; ++l) {
        if (!running_) return;

        std::string layer_prefix = "layers." + std::to_string(l);

        // A. Input Norm
        {
            TelemetryEvent ev;
            ev.event_type = "layer_trace";
            ev.timestamp = timestamp_ms;
            ev.event_id = global_event_id++;
            ev.layer_name = layer_prefix + ".input_layernorm";
            ev.layer_type = "RMSNorm";
            ev.device = target_device;
            ev.latency_ms = 0.15 + std::abs(lat_dis(gen)) * 0.1;
            ev.input_tensor = TensorInfo{{1, current_seq_len, 4096}, "float16", static_cast<size_t>(current_seq_len * 4096 * 2), target_device};
            ev.output_tensor = TensorInfo{{1, current_seq_len, 4096}, "float16", static_cast<size_t>(current_seq_len * 4096 * 2), target_device};
            ev.activation_stats = TensorStats{0.005, 0.99, -1.0, 1.0, 0.0};
            event_bus_.publish(ev);
        }

        // B. Self Attention
        std::string device = target_device;
        // Mock CPU fallback for layer 14 on token 5
        if (l == 14 && token_idx == 5) {
            device = "CPU";
        }

        double latency_attn = 1.0 + std::abs(lat_dis(gen));
        
        TelemetryEvent ev;
        ev.event_type = "layer_trace";
        ev.timestamp = timestamp_ms;
        ev.event_id = global_event_id++;
        ev.layer_name = layer_prefix + ".self_attn";
        ev.layer_type = "SelfAttention";
        ev.device = device;
        ev.latency_ms = latency_attn;
        ev.input_tensor = TensorInfo{{1, current_seq_len, 4096}, "float16", static_cast<size_t>(current_seq_len * 4096 * 2), device};
        ev.output_tensor = TensorInfo{{1, current_seq_len, 4096}, "float16", static_cast<size_t>(current_seq_len * 4096 * 2), device};
        
        // Setup stats, injecting abnormalities
        double mean = 0.02;
        double variance = 0.48;
        double min_val = -3.2;
        double max_val = 3.5;
        double sparsity = 15.0;

        if (l == 22 && token_idx == 12) {
            // Exploding activations
            max_val = 12.8;
            min_val = -11.4;
            mean = 1.5;
        } 
        else if (l == 8 && token_idx == 18) {
            // Dead layer
            variance = 1e-9;
            mean = 0.0;
            min_val = 0.0;
            max_val = 0.0;
        } 
        else if (l == 15 && token_idx == 2) {
            // NaN
            mean = std::numeric_limits<double>::quiet_NaN();
            variance = std::numeric_limits<double>::quiet_NaN();
        }

        ev.activation_stats = TensorStats{mean, variance, min_val, max_val, sparsity};
        ev.semantic_neighbors = generate_mock_neighbors(l, active_tokens);
        event_bus_.publish(ev);

        // C. Attention Weights Matrix Event
        {
            TelemetryEvent weight_ev;
            weight_ev.event_type = "attention_weights";
            weight_ev.timestamp = timestamp_ms;
            
            AttentionData& ad = weight_ev.attention;
            ad.layer_name = layer_prefix + ".self_attn";
            ad.num_heads = 8; // 8 heads matching UI select
            ad.token_count = current_seq_len;
            ad.tokens = active_tokens;
            
            ad.matrices.resize(ad.num_heads, std::vector<std::vector<float>>(current_seq_len, std::vector<float>(current_seq_len, 0.0f)));
            
            // Populate attention matrices with distinctive patterns requested
            for (int h = 0; h < ad.num_heads; ++h) {
                for (int q = 0; q < current_seq_len; ++q) {
                    float sum = 0.0f;
                    
                    for (int k = 0; k < current_seq_len; ++k) {
                        float val = 0.0f;
                        if (h == 0) {
                            // Head 0: Local attention (band/diagonal of width 5)
                            float dist = std::abs(q - k);
                            if (dist <= 2) {
                                val = 1.0f - (dist * 0.2f);
                            } else {
                                val = 0.01f;
                            }
                        } else if (h == 1) {
                            // Head 1: Long range / symmetric V-shape (cross/X-shape)
                            if (k == q || k == (current_seq_len - 1) - q) {
                                val = 1.0f;
                            } else {
                                val = 0.02f;
                            }
                        } else if (h == 2) {
                            // Head 2: Global token (dense/uniform)
                            val = 1.0f;
                        } else {
                            // Other heads: Causal mask + diagonal decay with minor noise
                            if (k <= q) {
                                float dist = std::abs(q - k);
                                val = std::exp(-dist / (h - 1.0f)) + 0.05f;
                            } else {
                                val = 0.0f;
                            }
                        }
                        
                        ad.matrices[h][q][k] = val;
                        sum += val;
                    }
                    
                    // Softmax normalization
                    if (sum > 0.0f) {
                        for (int k = 0; k < current_seq_len; ++k) {
                            ad.matrices[h][q][k] /= sum;
                        }
                    }
                }
            }
            event_bus_.publish(weight_ev);
        }

        // D. Post Attention Norm
        {
            TelemetryEvent ev;
            ev.event_type = "layer_trace";
            ev.timestamp = timestamp_ms;
            ev.event_id = global_event_id++;
            ev.layer_name = layer_prefix + ".post_attention_layernorm";
            ev.layer_type = "RMSNorm";
            ev.device = target_device;
            ev.latency_ms = 0.15 + std::abs(lat_dis(gen)) * 0.1;
            ev.input_tensor = TensorInfo{{1, current_seq_len, 4096}, "float16", static_cast<size_t>(current_seq_len * 4096 * 2), target_device};
            ev.output_tensor = TensorInfo{{1, current_seq_len, 4096}, "float16", static_cast<size_t>(current_seq_len * 4096 * 2), target_device};
            ev.activation_stats = TensorStats{0.002, 0.995, -1.0, 1.0, 0.0};
            event_bus_.publish(ev);
        }

        // E. MLP Block
        {
            TelemetryEvent ev;
            ev.event_type = "layer_trace";
            ev.timestamp = timestamp_ms;
            ev.event_id = global_event_id++;
            ev.layer_name = layer_prefix + ".mlp";
            ev.layer_type = "MLP";
            ev.device = target_device;
            ev.latency_ms = 1.5 + std::abs(lat_dis(gen)) * 1.5;
            ev.input_tensor = TensorInfo{{1, current_seq_len, 4096}, "float16", static_cast<size_t>(current_seq_len * 4096 * 2), target_device};
            ev.output_tensor = TensorInfo{{1, current_seq_len, 4096}, "float16", static_cast<size_t>(current_seq_len * 4096 * 2), target_device};
            ev.activation_stats = TensorStats{0.04, 0.72, -5.2, 5.8, 48.5};
            ev.semantic_neighbors = generate_mock_neighbors(l, active_tokens);
            event_bus_.publish(ev);
        }
    }

    // 3. Output Norm
    {
        TelemetryEvent ev;
        ev.event_type = "layer_trace";
        ev.timestamp = timestamp_ms;
        ev.event_id = global_event_id++;
        ev.layer_name = "norm";
        ev.layer_type = "RMSNorm";
        ev.device = target_device;
        ev.latency_ms = 0.2 + std::abs(lat_dis(gen)) * 0.1;
        ev.input_tensor = TensorInfo{{1, current_seq_len, 4096}, "float16", static_cast<size_t>(current_seq_len * 4096 * 2), target_device};
        ev.output_tensor = TensorInfo{{1, current_seq_len, 4096}, "float16", static_cast<size_t>(current_seq_len * 4096 * 2), target_device};
        ev.activation_stats = TensorStats{0.003, 1.0, -1.0, 1.0, 0.0};
        event_bus_.publish(ev);
    }

    // 4. Output Head (LM Head)
    {
        TelemetryEvent ev;
        ev.event_type = "layer_trace";
        ev.timestamp = timestamp_ms;
        ev.event_id = global_event_id++;
        ev.layer_name = "lm_head";
        ev.layer_type = "LMHead";
        ev.device = target_device;
        ev.latency_ms = 0.8 + std::abs(lat_dis(gen)) * 0.5;
        ev.input_tensor = TensorInfo{{1, current_seq_len, 4096}, "float16", static_cast<size_t>(current_seq_len * 4096 * 2), target_device};
        ev.output_tensor = TensorInfo{{1, current_seq_len, 128256}, "float16", static_cast<size_t>(current_seq_len * 128256 * 2), target_device};
        ev.activation_stats = TensorStats{-0.02, 1.4, -8.2, 9.4, 0.0};
        event_bus_.publish(ev);
    }
}
