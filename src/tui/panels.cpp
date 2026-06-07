#include "tui/panels.hpp"
#include "plugins/plugin_manager.hpp"
#include <ftxui/dom/elements.hpp>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <cmath>

namespace tui {

using namespace ftxui;

// Helper to draw a horizontal progress bar
Element make_progress_bar(double percentage, Color color) {
    int width = 15;
    int filled = static_cast<int>((percentage / 100.0) * width);
    filled = std::clamp(filled, 0, width);
    std::string bar = "";
    for (int i = 0; i < filled; ++i) bar += "█";
    for (int i = filled; i < width; ++i) bar += "░";
    return text(bar) | color;
}

Element Panels::render_topology(const ModelInfo& model, const std::string& active_layer, int selected_idx, bool focused) {
    Elements items;
    
    // Header Info
    items.push_back(text("Model: " + model.name) | bold | color(Color::Emerald));
    items.push_back(text("Layers: " + std::to_string(model.layers)) | dim);
    items.push_back(text("Hidden size: " + std::to_string(model.hidden_size)) | dim);
    items.push_back(text("Heads: " + std::to_string(model.num_heads)) | dim);
    items.push_back(text("Quantization: " + model.quantization) | color(Color::Yellow));
    items.push_back(separator());

    // Generate flat tree list
    std::vector<std::string> tree_nodes = {
        "embed_tokens",
        "layers"
    };
    for (int i = 0; i < model.layers; ++i) {
        tree_nodes.push_back(" ├─ layers." + std::to_string(i));
        tree_nodes.push_back(" │  ├─ self_attn");
        tree_nodes.push_back(" │  └─ mlp");
    }
    tree_nodes.push_back("norm");
    tree_nodes.push_back("lm_head");

    // Render tree nodes, highlighting selected and active captures
    for (size_t i = 0; i < tree_nodes.size(); ++i) {
        std::string node = tree_nodes[i];
        
        bool is_selected = (static_cast<int>(i) == selected_idx);
        
        // Check if this node name is inside the active layer trace path
        bool is_active_target = false;
        std::string raw_node_name = node;
        // Strip tree indentation characters for match check
        size_t indent_pos = raw_node_name.find("layers.");
        if (indent_pos != std::string::npos) {
            std::string layer_num = raw_node_name.substr(indent_pos);
            if (active_layer.find(layer_num) != std::string::npos) {
                is_active_target = true;
            }
        } else if (active_layer == "embed_tokens" && node == "embed_tokens") {
            is_active_target = true;
        } else if (active_layer == "lm_head" && node == "lm_head") {
            is_active_target = true;
        }

        Element item_el = text(node);
        if (is_active_target) {
            item_el = hbox(item_el, text(" [CAPTURE TARGET]") | color(Color::Teal));
        }

        if (is_selected) {
            if (focused) {
                item_el = item_el | bold | bgcolor(Color::Blue) | color(Color::White);
            } else {
                item_el = item_el | bold | bgcolor(Color::GrayDark) | color(Color::White);
            }
        } else if (is_active_target) {
            item_el = item_el | color(Color::Teal) | bold;
        }

        items.push_back(item_el);
    }

    auto win = vbox(std::move(items)) | yframe | flex;
    if (focused) {
        return window(text(" 1. MODEL TOPOLOGY (Focused) ") | bold | color(Color::Teal), win);
    }
    return window(text(" 1. MODEL TOPOLOGY ") | bold, win);
}

Element Panels::render_live_stream(const std::vector<TelemetryEvent>& events, int selected_idx, bool focused) {
    Elements rows;
    
    // Filter out layer traces
    std::vector<TelemetryEvent> traces;
    for (const auto& ev : events) {
        if (ev.event_type == "layer_trace") {
            traces.push_back(ev);
        }
    }
    
    // Header
    rows.push_back(hbox({
        text("ID") | bold | size(WIDTH, EQUAL, 6),
        text("TIMESTAMP") | bold | size(WIDTH, EQUAL, 12),
        text("LAYER TYPE") | bold | size(WIDTH, EQUAL, 15),
        text("LAYER NAME") | bold | flex,
        text("DEVICE") | bold | size(WIDTH, EQUAL, 10),
        text("LATENCY") | bold | size(WIDTH, EQUAL, 10)
    }) | borderBottom);

    // List in reverse order (newest first)
    int display_idx = 0;
    for (auto it = traces.rbegin(); it != traces.rend(); ++it, ++display_idx) {
        const auto& ev = *it;
        bool is_selected = (display_idx == selected_idx);

        Color dev_color = (ev.device.find("CUDA") != std::string::npos) ? Color::Teal : Color::Slate;
        
        std::stringstream ss;
        ss << std::fixed << std::setprecision(3) << ev.latency_ms << " ms";
        
        std::string ts = "";
        if (ev.timestamp > 0) {
            auto time_sec = ev.timestamp / 1000;
            auto ms = ev.timestamp % 1000;
            std::time_t t = time_sec;
            char time_buf[16];
            std::strftime(time_buf, sizeof(time_buf), "%H:%M:%S", std::localtime(&t));
            ts = std::string(time_buf) + "." + std::to_string(ms);
        }

        auto row = hbox({
            text(std::to_string(ev.event_id)) | size(WIDTH, EQUAL, 6) | dim,
            text(ts) | size(WIDTH, EQUAL, 12) | dim,
            text(ev.layer_type) | size(WIDTH, EQUAL, 15),
            text(ev.layer_name) | flex,
            text(ev.device) | size(WIDTH, EQUAL, 10) | color(dev_color),
            text(ss.str()) | size(WIDTH, EQUAL, 10) | color(Color::Teal) | bold
        });

        if (is_selected) {
            if (focused) {
                row = row | bgcolor(Color::Blue) | color(Color::White);
            } else {
                row = row | bgcolor(Color::GrayDark) | color(Color::White);
            }
        }

        rows.push_back(row);
    }

    if (traces.empty()) {
        rows.push_back(text("No telemetry packets streamed yet. Start inference or simulation.") | dim | text_style(center));
    }

    auto win = vbox(std::move(rows)) | yframe | flex;
    if (focused) {
        return window(text(" 2. LIVE PACKET STREAM (Focused) ") | bold | color(Color::Teal), win);
    }
    return window(text(" 2. LIVE PACKET STREAM ") | bold, win);
}

Element Panels::render_attention_matrix(const AttentionData& attn, int head_idx, int pan_x, int pan_y, int zoom, float contrast, bool focused) {
    Elements lines;

    if (attn.matrices.empty() || head_idx >= attn.num_heads || attn.token_count == 0) {
        auto empty_win = vbox({
            text("No active attention weights selected.") | dim,
            text("Highlight a SelfAttention layer in Topology or run trace to view.") | dim
        }) | center | flex;
        
        if (focused) {
            return window(text(" 3. ATTENTION MATRIX VISUALIZER (Focused) ") | bold | color(Color::Teal), empty_win);
        }
        return window(text(" 3. ATTENTION MATRIX VISUALIZER ") | bold, empty_win);
    }

    const auto& matrix = attn.matrices[head_idx];
    int size = attn.token_count;

    // Viewport calculation
    int viewport_size = 8 + zoom; // default viewport is 8x8, adjustable
    viewport_size = std::clamp(viewport_size, 4, 32);

    int start_r = std::clamp(pan_y, 0, std::max(0, size - viewport_size));
    int start_c = std::clamp(pan_x, 0, std::max(0, size - viewport_size));
    int end_r = std::min(size, start_r + viewport_size);
    int end_c = std::min(size, start_c + viewport_size);

    // Viewport title info
    std::string title_info = " [Layer: " + attn.layer_name + " | Head: " + std::to_string(head_idx) + "]";
    std::string viewport_info = " Viewport: [" + std::to_string(start_c) + "-" + std::to_string(end_c-1) + "] x [" + std::to_string(start_r) + "-" + std::to_string(end_r-1) + "]";

    lines.push_back(hbox({
        text("Tokens:" ) | bold,
        text(" [Use +/- to scale contrast, Arrows/hjkl to pan matrix]") | dim | flex,
        text(viewport_info) | color(Color::Yellow)
    }));
    lines.push_back(separator());

    // Compute Shannon Entropy for active head
    double entropy = 0.0;
    for (int r = 0; r < size; ++r) {
        for (int c = 0; c < size; ++c) {
            float w = matrix[r][c];
            if (w > 1e-6f) {
                entropy -= w * std::log2(w);
            }
        }
    }
    entropy = entropy / size; // average over queries

    // Table Header
    Elements header_cells = { text("Query / Key") | bold | size(WIDTH, EQUAL, 12) | borderRight };
    for (int c = start_c; c < end_c; ++c) {
        std::string cell_label = "[" + attn.tokens[c] + "]";
        if (cell_label.size() > 8) cell_label = cell_label.substr(0, 6) + "..";
        header_cells.push_back(text(cell_label) | size(WIDTH, EQUAL, 8) | text_style(center));
    }
    lines.push_back(hbox(std::move(header_cells)) | borderBottom);

    // Rows
    for (int r = start_r; r < end_r; ++r) {
        std::string row_label = attn.tokens[r];
        if (row_label.size() > 10) row_label = row_label.substr(0, 8) + "..";
        
        Elements row_cells = { text(row_label) | size(WIDTH, EQUAL, 12) | borderRight };
        
        for (int c = start_c; c < end_c; ++c) {
            float weight = matrix[r][c];
            
            // Apply contrast scaler
            float scaled_weight = weight * contrast;
            
            // Choose shade blocks
            std::string shade = "  ";
            Color cell_color = Color::Default;
            if (scaled_weight >= 0.35f) {
                shade = "██";
                cell_color = Color::Teal;
            } else if (scaled_weight >= 0.18f) {
                shade = "▓▓";
                cell_color = Color::Teal;
            } else if (scaled_weight >= 0.08f) {
                shade = "▒▒";
                cell_color = Color::Teal;
            } else if (scaled_weight >= 0.02f) {
                shade = "░░";
                cell_color = Color::Slate;
            }

            row_cells.push_back(text(shade) | size(WIDTH, EQUAL, 8) | color(cell_color) | text_style(center));
        }
        lines.push_back(hbox(std::move(row_cells)));
    }

    lines.push_back(separator());
    
    // Add entropy representation in summary
    std::stringstream ent_ss;
    ent_ss << "Average Head Entropy: " << std::fixed << std::setprecision(3) << entropy 
           << " bits. Contrast Level: " << std::fixed << std::setprecision(1) << contrast << "x";
    lines.push_back(text(ent_ss.str()) | color(Color::Emerald) | bold);

    auto win = vbox(std::move(lines)) | flex;
    if (focused) {
        return window(text(" 3. ATTENTION MATRIX" + title_info + " (Focused) ") | bold | color(Color::Teal), win);
    }
    return window(text(" 3. ATTENTION MATRIX" + title_info) | bold, win);
}

Element Panels::render_metrics_inspector(const TelemetryEvent& event, bool focused) {
    Elements items;

    if (event.layer_name.empty() || event.event_type != "layer_trace") {
        auto empty_win = text("No layer selected. Select a layer in model topology to inspect runtime tensors.") | dim | center | flex;
        if (focused) {
            return window(text(" 4. RUNTIME METRICS INSPECTOR (Focused) ") | bold | color(Color::Teal), empty_win);
        }
        return window(text(" 4. RUNTIME METRICS INSPECTOR ") | bold, empty_win);
    }

    const auto& stats = event.activation_stats;
    const auto& inp = event.input_tensor;
    const auto& out = event.output_tensor;

    auto make_shape_str = [](const std::vector<int64_t>& shape) {
        std::string s = "[";
        for (size_t i = 0; i < shape.size(); ++i) {
            s += std::to_string(shape[i]);
            if (i < shape.size() - 1) s += ", ";
        }
        s += "]";
        return s;
    };

    items.push_back(hbox(text("Layer Path : ") | bold, text(event.layer_name) | color(Color::Emerald)));
    items.push_back(hbox(text("Layer Type : ") | bold, text(event.layer_type)));
    items.push_back(hbox(text("Device     : ") | bold, text(event.device) | color(event.device.find("CUDA") != std::string::npos ? Color::Teal : Color::Slate)));
    items.push_back(separator());

    items.push_back(text("TENSOR METADATA:") | bold | color(Color::Yellow));
    items.push_back(hbox(text("  Input Shape  : ") | bold | dim, text(make_shape_str(inp.shape))));
    items.push_back(hbox(text("  Output Shape : ") | bold | dim, text(make_shape_str(out.shape))));
    items.push_back(hbox(text("  Data Type    : ") | bold | dim, text(out.dtype)));
    
    double out_mb = out.size_bytes / (1024.0 * 1024.0);
    std::stringstream sz_ss;
    sz_ss << std::fixed << std::setprecision(3) << out_mb << " MB (" << out.size_bytes << " bytes)";
    items.push_back(hbox(text("  Output Size  : ") | bold | dim, text(sz_ss.str())));
    items.push_back(separator());

    items.push_back(text("ACTIVATION STATISTICS:") | bold | color(Color::Yellow));
    
    std::stringstream m_ss, v_ss, min_ss, max_ss;
    m_ss << std::fixed << std::setprecision(5) << stats.mean;
    v_ss << std::scientific << std::setprecision(4) << stats.variance;
    min_ss << std::fixed << std::setprecision(4) << stats.min_val;
    max_ss << std::fixed << std::setprecision(4) << stats.max_val;

    items.push_back(hbox(text("  Mean         : ") | bold | dim, text(m_ss.str()) | mono));
    items.push_back(hbox(text("  Variance     : ") | bold | dim, text(v_ss.str()) | mono));
    items.push_back(hbox(text("  Range (Min)  : ") | bold | dim, text(min_ss.str()) | mono));
    items.push_back(hbox(text("  Range (Max)  : ") | bold | dim, text(max_ss.str()) | mono));

    // Sparsity Rate representation
    items.push_back(hbox({
        text("  Sparsity Rate: ") | bold | dim,
        make_progress_bar(stats.sparsity, Color::Yellow),
        text(" " + std::to_string(static_cast<int>(stats.sparsity)) + "%") | bold
    }));

    auto win = vbox(std::move(items)) | yframe | flex;
    if (focused) {
        return window(text(" 4. RUNTIME METRICS INSPECTOR (Focused) ") | bold | color(Color::Teal), win);
    }
    return window(text(" 4. RUNTIME METRICS INSPECTOR ") | bold, win);
}

Element Panels::render_anomaly_ledger(const std::vector<AnomalyAlert>& alerts, bool focused) {
    Elements items;

    for (auto it = alerts.rbegin(); it != alerts.rend(); ++it) {
        const auto& alert = *it;
        Color tag_color = (alert.severity == "ERROR") ? Color::Red : Color::Yellow;
        
        auto tag = text("[" + alert.severity + "]") | bold | color(tag_color);
        auto ts = text(alert.timestamp) | dim;
        auto header = hbox({ tag, text(" "), text(alert.layer_name) | bold, text(" "), ts | right });
        auto desc = text("  " + alert.description) | dim;
        
        items.push_back(vbox({ header, desc, separator() }));
    }

    if (alerts.empty()) {
        items.push_back(text("No numerical anomalies or OOM warnings flagged.") | dim | center | flex);
    }

    auto win = vbox(std::move(items)) | yframe | flex;
    if (focused) {
        return window(text(" 5. NUMERICAL ANOMALY LEDGER (Focused) ") | bold | color(Color::Teal), win);
    }
    return window(text(" 5. NUMERICAL ANOMALY LEDGER ") | bold, win);
}

Element Panels::render_performance_dashboard(const TelemetryAggregator& aggregator, double cpu, double ram, double ram_tot, double gpu, double vram, double vram_tot, bool focused) {
    Elements items;

    // Row 1: System Hardware Loads
    std::stringstream ram_ss, vram_ss;
    ram_ss << std::fixed << std::setprecision(1) << ram << "/" << ram_tot << " GB";
    vram_ss << std::fixed << std::setprecision(1) << vram << "/" << vram_tot << " GB";

    auto system_row = hbox({
        vbox({
            hbox(text("CPU Load : ") | bold | dim, make_progress_bar(cpu, Color::Teal), text(" " + std::to_string(static_cast<int>(cpu)) + "%") | bold),
            hbox(text("Sys RAM  : ") | bold | dim, make_progress_bar((ram/ram_tot)*100.0, Color::Slate), text(" " + ram_ss.str()) | bold)
        }) | flex,
        separator(),
        vbox({
            hbox(text("GPU Load : ") | bold | dim, make_progress_bar(gpu, Color::Teal), text(" " + std::to_string(static_cast<int>(gpu)) + "%") | bold),
            hbox(text("GPU VRAM : ") | bold | dim, make_progress_bar((vram/vram_tot)*100.0, Color::Slate), text(" " + vram_ss.str()) | bold)
        }) | flex
    });
    items.push_back(system_row);
    items.push_back(separator());

    // Row 2: Performance stats
    std::stringstream tps_ss, lat_ss;
    tps_ss << std::fixed << std::setprecision(2) << aggregator.get_tokens_per_sec();
    lat_ss << std::fixed << std::setprecision(1) << aggregator.get_avg_inference_latency();

    items.push_back(hbox({
        text("Gen Speed: ") | bold, text(tps_ss.str() + " tokens/sec") | color(Color::Emerald) | bold,
        text("  |  Avg Latency: ") | bold, text(lat_ss.str() + " ms/token") | color(Color::Teal) | bold,
        text("  |  Total Events: ") | bold, text(std::to_string(aggregator.get_total_events_processed())) | dim
    }));
    items.push_back(separator());

    // Row 3: Slowest Layers latency heatmap
    items.push_back(text("LATENCY BOTTLENECK PROFILE (Slowest Layers):") | bold | color(Color::Yellow));
    auto slowest = aggregator.get_slowest_layers(5);
    
    double max_avg = 0.0;
    for (const auto& s : slowest) {
        max_avg = std::max(max_avg, s.avg_latency_ms);
    }

    for (const auto& s : slowest) {
        double ratio = (max_avg > 0.0) ? (s.avg_latency_ms / max_avg * 100.0) : 0.0;
        
        std::stringstream ss;
        ss << std::fixed << std::setprecision(3) << s.avg_latency_ms << " ms";
        
        items.push_back(hbox({
            text(s.name) | size(WIDTH, EQUAL, 35) | dim,
            make_progress_bar(ratio, Color::Teal),
            text(" " + ss.str()) | bold | color(Color::Teal)
        }));
    }

    if (slowest.empty()) {
        items.push_back(text("Profiler compiling averages...") | dim);
    }

    auto win = vbox(std::move(items)) | yframe | flex;
    if (focused) {
        return window(text(" 6. PERFORMANCE DASHBOARD (Focused) ") | bold | color(Color::Teal), win);
    }
    return window(text(" 6. PERFORMANCE DASHBOARD ") | bold, win);
}

Element Panels::render_flame_graph(const std::vector<TelemetryEvent>& events, const std::unordered_map<std::string, bool>& expanded_nodes, const std::string& selected_node, bool focused) {
    Elements rows;
    
    // Group and average event times to build hierarchy representation
    std::unordered_map<std::string, double> avg_times;
    std::unordered_map<std::string, int> counts;
    for (const auto& ev : events) {
        if (ev.event_type == "layer_trace") {
            avg_times[ev.layer_name] += ev.latency_ms;
            counts[ev.layer_name]++;
        }
    }
    for (auto& [name, sum] : avg_times) {
        sum /= counts[name];
    }

    // Build hierarchical tree
    // Root total time estimation
    double total_inference_time = 0.0;
    if (avg_times.count("embed_tokens")) total_inference_time += avg_times["embed_tokens"];
    if (avg_times.count("norm")) total_inference_time += avg_times["norm"];
    if (avg_times.count("lm_head")) total_inference_time += avg_times["lm_head"];
    
    double sum_layers = 0.0;
    for (int i = 0; i < 32; ++i) {
        std::string prefix = "layers." + std::to_string(i);
        double layer_sum = 0.0;
        if (avg_times.count(prefix + ".input_layernorm")) layer_sum += avg_times[prefix + ".input_layernorm"];
        if (avg_times.count(prefix + ".self_attn")) layer_sum += avg_times[prefix + ".self_attn"];
        if (avg_times.count(prefix + ".post_attention_layernorm")) layer_sum += avg_times[prefix + ".post_attention_layernorm"];
        if (avg_times.count(prefix + ".mlp")) layer_sum += avg_times[prefix + ".mlp"];
        sum_layers += layer_sum;
        avg_times[prefix] = layer_sum; // cache group sum
    }
    total_inference_time += sum_layers;
    avg_times["layers"] = sum_layers;
    avg_times["Inference"] = total_inference_time;

    // Recursive helper to render nodes
    auto render_node_helper = [&](auto& self, const std::string& path, const std::string& label, int indent) -> void {
        double t = avg_times[path];
        double ratio = (total_inference_time > 0.0) ? (t / total_inference_time * 100.0) : 0.0;
        
        std::stringstream ss;
        ss << std::fixed << std::setprecision(2) << t << " ms (" << std::fixed << std::setprecision(1) << ratio << "%)";
        
        std::string indent_str = "";
        for (int i = 0; i < indent; ++i) indent_str += "  ";
        
        bool has_children = (path == "Inference" || path == "layers" || path.find("layers.") == 0 && path.find(".") == path.rfind("."));
        bool is_expanded = expanded_nodes.count(path) ? expanded_nodes.at(path) : true;
        
        std::string prefix = "";
        if (has_children) {
            prefix = is_expanded ? "▼ " : "▶ ";
        } else {
            prefix = "● ";
        }

        // Render bar representing call weight
        int bar_width = 10;
        int filled = static_cast<int>((ratio / 100.0) * bar_width);
        std::string bar = "";
        for (int i = 0; i < filled; ++i) bar += "█";
        for (int i = filled; i < bar_width; ++i) bar += " ";

        bool is_selected = (path == selected_node);

        auto row = hbox({
            text(indent_str + prefix + label) | flex,
            text(bar) | color(Color::Teal),
            text(" " + ss.str()) | size(WIDTH, EQUAL, 20) | bold | color(Color::Yellow)
        });

        if (is_selected) {
            if (focused) {
                row = row | bgcolor(Color::Blue) | color(Color::White);
            } else {
                row = row | bgcolor(Color::GrayDark) | color(Color::White);
            }
        }

        rows.push_back(row);

        // Render children
        if (has_children && is_expanded) {
            if (path == "Inference") {
                self(self, "embed_tokens", "embed_tokens", indent + 1);
                self(self, "layers", "layers (Transformer blocks)", indent + 1);
                self(self, "norm", "output_norm", indent + 1);
                self(self, "lm_head", "lm_head", indent + 1);
            } 
            else if (path == "layers") {
                for (int i = 0; i < 32; ++i) {
                    std::string layer_path = "layers." + std::to_string(i);
                    if (avg_times.count(layer_path) && avg_times[layer_path] > 0.0) {
                        self(self, layer_path, "layer_" + std::to_string(i), indent + 1);
                    }
                }
            }
            else { // specific layer.i
                self(self, path + ".input_layernorm", "input_layernorm", indent + 1);
                self(self, path + ".self_attn", "self_attention", indent + 1);
                self(self, path + ".post_attention_layernorm", "post_attention_layernorm", indent + 1);
                self(self, path + ".mlp", "mlp_block", indent + 1);
            }
        }
    };

    if (total_inference_time > 0.0) {
        render_node_helper(render_node_helper, "Inference", "Inference Loop", 0);
    } else {
        rows.push_back(text("No trace data accumulated. Start simulation to construct flame tree.") | dim);
    }

    auto win = vbox(std::move(rows)) | yframe | flex;
    if (focused) {
        return window(text(" INTERACTIVE FLAME TREE PROFILE (Focused) ") | bold | color(Color::Teal), win);
    }
    return window(text(" INTERACTIVE FLAME TREE PROFILE ") | bold, win);
}

Element Panels::render_token_journey(const std::vector<TelemetryEvent>& events, int token_idx, int scroll_offset, bool focused) {
    Elements rows;

    // Filter events corresponding to this token iteration
    // We group events into "token generation cycles".
    // Each cycle begins with embed_tokens and ends with lm_head.
    std::vector<std::vector<TelemetryEvent>> cycles;
    std::vector<TelemetryEvent> current_cycle;
    
    for (const auto& ev : events) {
        if (ev.event_type != "layer_trace") continue;
        if (ev.layer_name == "embed_tokens") {
            if (!current_cycle.empty()) {
                cycles.push_back(current_cycle);
                current_cycle.clear();
            }
        }
        current_cycle.push_back(ev);
    }
    if (!current_cycle.empty()) {
        cycles.push_back(current_cycle);
    }

    if (cycles.empty() || token_idx >= static_cast<int>(cycles.size())) {
        auto empty_win = text("No token journey maps logged yet. Select token after running inference.") | dim | center | flex;
        if (focused) {
            return window(text(" TOKEN ACTIVATION JOURNEY (Focused) ") | bold | color(Color::Teal), empty_win);
        }
        return window(text(" TOKEN ACTIVATION JOURNEY ") | bold, empty_win);
    }

    const auto& token_cycle = cycles[token_idx];

    rows.push_back(hbox({
        text("Token Index: ") | bold, text("#" + std::to_string(token_idx)) | color(Color::Yellow) | bold,
        text(" | Path Stage Flow: ") | bold | dim
    }));
    rows.push_back(separator());

    // Render detailed step journey
    for (size_t i = 0; i < token_cycle.size(); ++i) {
        const auto& ev = token_cycle[i];
        const auto& stats = ev.activation_stats;
        
        std::stringstream ss_m, ss_mx, ss_sp;
        ss_m << std::fixed << std::setprecision(4) << stats.mean;
        ss_mx << std::fixed << std::setprecision(4) << stats.max_val;
        ss_sp << std::fixed << std::setprecision(1) << stats.sparsity << "%";

        auto row = hbox({
            text("Stage " + std::to_string(i)) | size(WIDTH, EQUAL, 8) | color(Color::Teal) | bold,
            text("➔ " + ev.layer_name) | size(WIDTH, EQUAL, 32) | bold,
            text(" [Mean: " + ss_m.str()) | dim,
            text(" | Max: " + ss_mx.str()) | dim,
            text(" | Sparsity: " + ss_sp.str() + "]") | color(Color::Yellow)
        });
        
        rows.push_back(row);
    }

    auto win = vbox(std::move(rows)) | yframe | flex;
    if (focused) {
        return window(text(" TOKEN ACTIVATION JOURNEY (Focused) ") | bold | color(Color::Teal), win);
    }
    return window(text(" TOKEN ACTIVATION JOURNEY ") | bold, win);
}

Element Panels::render_comparison(const std::vector<TelemetryEvent>& runA, const std::vector<TelemetryEvent>& runB, const std::string& nameA, const std::string& nameB, bool focused) {
    Elements rows;

    auto analyze_run = [](const std::vector<TelemetryEvent>& events) {
        double avg_lat = 0.0;
        double max_lat = 0.0;
        int layers_cnt = 0;
        int anomalies = 0;
        double sum_sparsity = 0.0;
        int count_sparsity = 0;
        double total_time = 0.0;

        for (const auto& ev : events) {
            if (ev.event_type == "layer_trace") {
                avg_lat += ev.latency_ms;
                max_lat = std::max(max_lat, ev.latency_ms);
                layers_cnt++;
                total_time += ev.latency_ms;
                sum_sparsity += ev.activation_stats.sparsity;
                count_sparsity++;
            } else if (ev.event_type == "anomaly") {
                anomalies++;
            }
        }
        if (layers_cnt > 0) avg_lat /= layers_cnt;
        double sparsity = (count_sparsity > 0) ? (sum_sparsity / count_sparsity) : 0.0;

        return std::make_tuple(avg_lat, max_lat, anomalies, sparsity, total_time);
    };

    auto [avgA, maxA, anomA, spA, timeA] = analyze_run(runA);
    auto [avgB, maxB, anomB, spB, timeB] = analyze_run(runB);

    rows.push_back(hbox({
        text("COMPARATIVE METRICS") | bold | size(WIDTH, EQUAL, 25) | borderRight,
        text(" RUN A: " + nameA) | bold | flex | color(Color::Teal) | text_style(center),
        separator(),
        text(" RUN B: " + nameB) | bold | flex | color(Color::Yellow) | text_style(center)
    }) | borderBottom);

    auto add_row = [&](const std::string& metric, const std::string& valA, const std::string& valB, Color colorA = Color::Default, Color colorB = Color::Default) {
        rows.push_back(hbox({
            text(metric) | size(WIDTH, EQUAL, 25) | borderRight | bold | dim,
            text(" " + valA) | flex | color(colorA) | text_style(center),
            separator(),
            text(" " + valB) | flex | color(colorB) | text_style(center)
        }));
    };

    std::stringstream a1, b1, a2, b2, a3, b3, a4, b4;
    a1 << std::fixed << std::setprecision(2) << avgA << " ms";
    b1 << std::fixed << std::setprecision(2) << avgB << " ms";
    a2 << std::fixed << std::setprecision(2) << maxA << " ms";
    b2 << std::fixed << std::setprecision(2) << maxB << " ms";
    a3 << std::fixed << std::setprecision(1) << spA << "%";
    b3 << std::fixed << std::setprecision(1) << spB << "%";
    a4 << std::fixed << std::setprecision(1) << timeA << " ms";
    b4 << std::fixed << std::setprecision(1) << timeB << " ms";

    add_row("Avg Layer Latency", a1.str(), b1.str(), Color::Teal, Color::Yellow);
    add_row("Max Layer Latency", a2.str(), b2.str());
    add_row("Accumulated Latency", a4.str(), b4.str());
    add_row("Avg Sparsity Rate", a3.str(), b3.str());
    add_row("Flagged Anomalies", std::to_string(anomA), std::to_string(b1.str() != "" ? anomB : 0), 
            (anomA > 0) ? Color::Red : Color::Emerald, 
            (anomB > 0) ? Color::Red : Color::Emerald);

    auto win = vbox(std::move(rows)) | flex;
    if (focused) {
        return window(text(" COMPARATIVE RUN ANALYSIS (Focused) ") | bold | color(Color::Teal), win);
    }
    return window(text(" COMPARATIVE RUN ANALYSIS ") | bold, win);
}

} // namespace tui
