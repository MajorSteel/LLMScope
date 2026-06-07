#include "tui/dashboard.hpp"
#include "tui/panels.hpp"
#include "tui/knowledge_base.hpp"
#include "storage/session_store.hpp"
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <spdlog/spdlog.h>
#include <iostream>
#include <cmath>

static ftxui::ScreenInteractive* g_screen = nullptr;

TUIDashboard::TUIDashboard(EventBus& event_bus,
                           RingBuffer& ring_buffer,
                           TelemetryAggregator& aggregator,
                           AnomalyDetector& anomaly_detector,
                           ReplayManager& replay_manager,
                           PluginManager& plugin_manager,
                           DeviceMonitor& device_monitor,
                           TelemetryServer& server,
                           MockTracer& tracer,
                           OTLPExporter& exporter)
    : event_bus_(event_bus),
      ring_buffer_(ring_buffer),
      aggregator_(aggregator),
      anomaly_detector_(anomaly_detector),
      replay_manager_(replay_manager),
      plugin_manager_(plugin_manager),
      device_monitor_(device_monitor),
      server_(server),
      tracer_(tracer),
      exporter_(exporter) {
    
    // Default model metadata init
    active_model_.name = "No model connected";
    active_model_.layers = 0;
    active_model_.hidden_size = 0;
    active_model_.num_heads = 0;
    active_model_.vocab_size = 0;
    active_model_.quantization = "N/A";
    
    setup_event_subscriptions();

    // Setup dummy/comparison Run B for visualization comparison
    // This allows testing Comparative View immediately
    TelemetryEvent infoB;
    infoB.event_type = "model_info";
    infoB.model_info.name = "llama-3-8b";
    infoB.model_info.layers = 32;
    infoB.model_info.quantization = "FP16"; // Compare FP16 vs Q4_K_M
    comparison_run_b_.push_back(infoB);

    for (int i = 0; i < 15; ++i) {
        TelemetryEvent ev;
        ev.event_type = "layer_trace";
        ev.event_id = 200 + i;
        ev.layer_name = "layers." + std::to_string(i) + ".self_attn";
        ev.layer_type = "SelfAttention";
        ev.device = "CUDA:0";
        ev.latency_ms = 3.250; // slower (FP16)
        ev.activation_stats.sparsity = 5.0;
        comparison_run_b_.push_back(ev);
    }
}

void TUIDashboard::setup_event_subscriptions() {
    // Subscribe to EventBus updates
    event_bus_.subscribe("*", [this](const TelemetryEvent& event) {
        process_incoming_event(event);
        
        // Trigger UI redraw
        if (g_screen) {
            g_screen->PostEvent(ftxui::Event::Custom);
        }
    });
}

void TUIDashboard::process_incoming_event(const TelemetryEvent& event) {
    // 1. Add trace/model/anomalies to RingBuffer (skip attention matrices in standard ring to save heap memory)
    if (event.event_type != "attention_weights") {
        ring_buffer_.push(event);
    }

    // 2. Feed to Aggregator
    aggregator_.process_event(event);

    // 3. Feed to Anomaly Detector
    anomaly_detector_.process_event(event);

    // 4. Feed to Exporters & Plugins
    exporter_.process_event(event);
    plugin_manager_.process_event(event);

    // 5. Update cached details
    if (event.event_type == "model_info") {
        active_model_ = event.model_info;
    } 
    else if (event.event_type == "layer_trace") {
        // Automatically link active selected node to the trace event data
        std::string selected_layer_name = "";
        
        // Find selected layer from topology tree
        std::vector<std::string> tree_nodes = { "embed_tokens", "layers" };
        for (int i = 0; i < active_model_.layers; ++i) {
            tree_nodes.push_back("layers." + std::to_string(i));
            tree_nodes.push_back("layers." + std::to_string(i) + ".self_attn");
            tree_nodes.push_back("layers." + std::to_string(i) + ".mlp");
        }
        tree_nodes.push_back("norm");
        tree_nodes.push_back("lm_head");

        if (topology_selected_idx_ >= 0 && topology_selected_idx_ < static_cast<int>(tree_nodes.size())) {
            selected_layer_name = tree_nodes[topology_selected_idx_];
        }

        if (event.layer_name == selected_layer_name || selected_layer_name.find(event.layer_name) != std::string::npos) {
            selected_event_ = event;
        }
    }
    else if (event.event_type == "attention_weights") {
        // Automatically pull weights if layer matches selection
        std::string selected_layer_name = "";
        std::vector<std::string> tree_nodes = { "embed_tokens", "layers" };
        for (int i = 0; i < active_model_.layers; ++i) {
            tree_nodes.push_back("layers." + std::to_string(i));
            tree_nodes.push_back("layers." + std::to_string(i) + ".self_attn");
            tree_nodes.push_back("layers." + std::to_string(i) + ".mlp");
        }
        tree_nodes.push_back("norm");
        tree_nodes.push_back("lm_head");

        if (topology_selected_idx_ >= 0 && topology_selected_idx_ < static_cast<int>(tree_nodes.size())) {
            selected_layer_name = tree_nodes[topology_selected_idx_];
        }

        if (event.attention.layer_name == selected_layer_name || selected_layer_name.find(event.attention.layer_name) != std::string::npos) {
            active_attention_ = event.attention;
        }
    }
}

void TUIDashboard::run() {
    auto screen = ftxui::ScreenInteractive::Fullscreen();
    g_screen = &screen;

    // Define TUI Layout renderer
    auto renderer = ftxui::Renderer([this]() {
        // Get hardware statistics
        SystemStats stats = device_monitor_.get_stats();

        // 1. Header Row
        auto header = hbox({
            text(" LLMSCOPE ") | bold | bgcolor(Color::Teal) | color(Color::Black),
            text(" 🗲 Local Observability & Telemetry ") | color(Color::Teal),
            text("  |  Mode: ") | bold,
            text(replay_mode_ ? "REPLAY [Session File]" : (server_.is_client_connected() ? "LIVE [TCP Client Connected]" : "STANDALONE [Simulation]")) | color(replay_mode_ ? Color::Yellow : (server_.is_client_connected() ? Color::Emerald : Color::Slate)),
            fill(),
            text("Tabs: [1]Dashboard [2]Evolution [3]CallTree [4]Compare [5]Quantization [6]Help") | dim
        }) | borderBottom;

        // 2. Tab Menu Row
        std::vector<std::string> tab_names = {
            " [1] Dashboard ",
            " [2] Attention Evolution & Journey ",
            " [3] Call Tree & Latencies ",
            " [4] Run Comparison (A/B) ",
            " [5] Quantization & Exporters ",
            " [6] Help & Knowledge Card "
        };
        
        Elements tab_elements;
        for (int i = 0; i < 6; ++i) {
            if (active_tab_ == i) {
                tab_elements.push_back(text(tab_names[i]) | bold | bgcolor(Color::Teal) | color(Color::Black));
            } else {
                tab_elements.push_back(text(tab_names[i]) | dim);
            }
            if (i < 5) tab_elements.push_back(separator());
        }
        auto tabs = hbox(std::move(tab_elements)) | borderBottom;

        // 3. Tab Contents
        Element content = text("");
        
        if (active_tab_ == 0) {
            // Dashboard Layout
            // Left: Topology
            // Middle: Live Stream
            // Right Column: Metrics Inspector, Anomaly Ledger, Performance
            auto left = tui::Panels::render_topology(active_model_, selected_event_.layer_name, topology_selected_idx_, active_focus_panel_ == 0);
            auto mid = tui::Panels::render_live_stream(ring_buffer_.get_all(), stream_selected_idx_, active_focus_panel_ == 1);
            
            auto right = vbox({
                tui::Panels::render_metrics_inspector(selected_event_, active_focus_panel_ == 2) | size(HEIGHT, EQUAL, 12),
                tui::Panels::render_anomaly_ledger(anomaly_detector_.get_alerts(), active_focus_panel_ == 3) | flex,
                tui::Panels::render_performance_dashboard(aggregator_, stats.cpu_usage, stats.ram_used_gb, stats.ram_total_gb, stats.gpu_utilization, stats.vram_used_gb, stats.vram_total_gb, active_focus_panel_ == 4) | size(HEIGHT, EQUAL, 12)
            }) | size(WIDTH, GREATER_THAN, 45);

            content = hbox({ left | size(WIDTH, EQUAL, 25), mid | flex, right });
        } 
        else if (active_tab_ == 1) {
            // Attention Evolution & Journey
            auto left = tui::Panels::render_attention_matrix(active_attention_, attn_head_idx_, attn_pan_x_, attn_pan_y_, attn_zoom_, attn_contrast_, active_focus_panel_ == 0);
            auto right = tui::Panels::render_token_journey(ring_buffer_.get_all(), journey_token_idx_, journey_scroll_offset_, active_focus_panel_ == 1);
            content = hbox({ left | flex, right | size(WIDTH, EQUAL, 50) });
        }
        else if (active_tab_ == 2) {
            // Call Tree / Flame Graph
            auto left = tui::Panels::render_flame_graph(ring_buffer_.get_all(), flame_expanded_nodes_, flame_selected_node_, active_focus_panel_ == 0);
            auto right = tui::Panels::render_performance_dashboard(aggregator_, stats.cpu_usage, stats.ram_used_gb, stats.ram_total_gb, stats.gpu_utilization, stats.vram_used_gb, stats.vram_total_gb, active_focus_panel_ == 1) | size(WIDTH, EQUAL, 50);
            content = hbox({ left | flex, right });
        }
        else if (active_tab_ == 3) {
            // Comparison view
            content = tui::Panels::render_comparison(ring_buffer_.get_all(), comparison_run_b_, "Active Session (Run A)", comparison_name_b_, true);
        }
        else if (active_tab_ == 4) {
            // Quantization Analysis and OpenTelemetry Export control
            auto proj = QuantizationMemoryPlugin::get_projection();
            std::stringstream p32, p16, p8, p4;
            p32 << std::fixed << std::setprecision(2) << (proj.fp32_mb / 1024.0) << " GB";
            p16 << std::fixed << std::setprecision(2) << (proj.fp16_mb / 1024.0) << " GB";
            p8 << std::fixed << std::setprecision(2) << (proj.int8_mb / 1024.0) << " GB";
            p4 << std::fixed << std::setprecision(2) << (proj.q4_mb / 1024.0) << " GB";

            auto quant_panel = window(text(" QUANTIZATION MEMORY PROJECTION ") | bold, vbox({
                text("Active Model Parameter Estimate Footprint:") | bold | color(Color::Yellow),
                text(""),
                hbox(text("  FP32 Precision (32-bit): ") | bold | dim, text(p32.str()) | color(Color::Red)),
                hbox(text("  FP16 Precision (16-bit): ") | bold | dim, text(p16.str()) | color(Color::Yellow)),
                hbox(text("  INT8 Quantized (8-bit) : ") | bold | dim, text(p8.str()) | color(Color::Teal)),
                hbox(text("  Q4_K_M Quantized (4-bit): ") | bold | dim, text(p4.str()) | color(Color::Emerald) | bold),
                text(""),
                text("Lower bits reduce RAM and VRAM footprint but introduce activation entropy shift.") | dim
            }));

            auto export_panel = window(text(" OPEN TELEMETRY & PERSISTENCE EXPORTS ") | bold, vbox({
                text("Exporter Integrations:") | bold | color(Color::Yellow),
                text(""),
                text("  Press [o] to export current active telemetry session to OTLP JSON (llmscope_otlp.json)."),
                text("  Press [s] to save telemetry events to LLMScope Session file (session.json)."),
                text("  Press [l] to load session.json back into Replay engine."),
                text(""),
                text("OTLP exports can be ingested into OpenTelemetry Collector / Jaeger.") | dim
            }));

            content = vbox({ quant_panel, text(""), export_panel }) | flex;
        }
        else if (active_tab_ == 5) {
            // Help Screen
            content = vbox({
                window(text(" KEYBOARD SHORTCUTS ") | bold | color(Color::Teal), vbox({
                    hbox(text("  [Tab]            ") | bold | color(Color::Yellow), text("Cycle focus panels")),
                    hbox(text("  [Shift+Tab]      ") | bold | color(Color::Yellow), text("Cycle focus panels reverse")),
                    hbox(text("  [1] - [6]        ") | bold | color(Color::Yellow), text("Switch active tabs directly")),
                    hbox(text("  [j] / [k]        ") | bold | color(Color::Yellow), text("Navigate Up / Down in lists (Vim keys)")),
                    hbox(text("  [Arrows/hjkl]    ") | bold | color(Color::Yellow), text("Pan attention visualizer (Tab 2)")),
                    hbox(text("  [+/-]            ") | bold | color(Color::Yellow), text("Increase/decrease attention heatmap contrast")),
                    hbox(text("  [</>]            ") | bold | color(Color::Yellow), text("Cycle attention heads (previous/next)")),
                    hbox(text("  [Enter] / [Space]") | bold | color(Color::Yellow), text("Select layer / toggle Flame Graph node collapse")),
                    hbox(text("  [?]              ") | bold | color(Color::Yellow), text("Toggle Concept Explanation Card modal")),
                    hbox(text("  [p]              ") | bold | color(Color::Yellow), text("Pause / Resume mock simulator generation")),
                    hbox(text("  [r]              ") | bold | color(Color::Yellow), text("Toggle Replay playback loop")),
                    hbox(text("  [q]              ") | bold | color(Color::Yellow), text("Quit LLMScope application"))
                })),
                text(""),
                window(text(" COMPONENT EXPLANATIONS ") | bold, vbox({
                    text("Navigate to Dashboard or Attention view, select any layer, and press [?] to get a dynamic math explanation based on live telemetry activations!") | dim
                }))
            }) | flex;
        }

        // 4. Footer Help Bar
        auto footer = hbox({
            text(" [Tab]: Cycle Focus | [1-6]: Tabs | [?]: Explain Layer | [P]: Pause Sim | [R]: Play Replay | [Q]: Quit ") | dim
        }) | borderTop;

        // 5. Popup Overlay (Knowledge Base Concept Explain Modal)
        if (show_knowledge_popup_) {
            std::string l_type = selected_event_.layer_type;
            if (l_type.empty() && !active_attention_.matrices.empty()) {
                l_type = "SelfAttention";
            }
            
            auto explanation = KnowledgeBase::get_explanation(l_type, selected_event_.layer_name == "" ? TelemetryEvent{ "attention_weights", 0, {}, 0, "", "", "", 0.0, {}, {}, {}, active_attention_, {} } : selected_event_);
            
            auto popup = vbox({
                hbox({ text(" " + explanation.title + " ") | bold | bgcolor(Color::Teal) | color(Color::Black), fill(), text(" [Press ? to Close] ") | dim }),
                separator(),
                text(""),
                text("FORMULA:") | bold | color(Color::Yellow),
                text("  " + explanation.mathematical_formula) | color(Color::Teal) | bold,
                text(""),
                text("DESCRIPTION:") | bold | color(Color::Yellow),
                paragraph(explanation.description),
                text(""),
                text("DYNAMIC RUNTIME AUDIT:") | bold | color(Color::Yellow),
                paragraph(explanation.dynamic_analysis) | color(Color::Emerald),
                text("")
            }) | border | size(WIDTH, EQUAL, 65) | bgcolor(Color::slate(30)) | center;
            
            return dbox({ vbox({ header, tabs, content, footer }) | dim, popup });
        }

        return vbox({ header, tabs, content, footer }) | flex;
    });

    // Handle Keyboard events
    auto component = ftxui::CatchEvent(renderer, [this](ftxui::Event event) {
        if (event == ftxui::Event::Character('q') || event == ftxui::Event::Escape) {
            screen.ExitLoopClosure()();
            return true;
        }

        // Tab hotkeys
        if (event == ftxui::Event::Character('1')) { active_tab_ = 0; active_focus_panel_ = 0; return true; }
        if (event == ftxui::Event::Character('2')) { active_tab_ = 1; active_focus_panel_ = 0; return true; }
        if (event == ftxui::Event::Character('3')) { active_tab_ = 2; active_focus_panel_ = 0; return true; }
        if (event == ftxui::Event::Character('4')) { active_tab_ = 3; active_focus_panel_ = 0; return true; }
        if (event == ftxui::Event::Character('5')) { active_tab_ = 4; active_focus_panel_ = 0; return true; }
        if (event == ftxui::Event::Character('6')) { active_tab_ = 5; active_focus_panel_ = 0; return true; }

        // Knowledge Base toggle
        if (event == ftxui::Event::Character('?')) {
            show_knowledge_popup_ = !show_knowledge_popup_;
            return true;
        }

        // Pause/Resume Simulation
        if (event == ftxui::Event::Character('p') || event == ftxui::Event::Character('P')) {
            if (tracer_.is_running()) {
                tracer_.stop();
            } else {
                tracer_.start();
            }
            return true;
        }

        // Play/Pause Replay
        if (event == ftxui::Event::Character('r') || event == ftxui::Event::Character('R')) {
            if (replay_manager_.is_playing()) {
                replay_manager_.pause();
            } else {
                replay_mode_ = true;
                replay_manager_.play();
            }
            return true;
        }

        // Save session file
        if (event == ftxui::Event::Character('s')) {
            SessionStore::save("session.json", ring_buffer_.get_all());
            return true;
        }

        // Load session file
        if (event == ftxui::Event::Character('l')) {
            auto events = SessionStore::load("session.json");
            if (!events.empty()) {
                replay_mode_ = true;
                replay_manager_.load_session(events);
            }
            return true;
        }

        // OTLP Export
        if (event == ftxui::Event::Character('o')) {
            exporter_.export_now();
            return true;
        }

        // Cycle focus panels inside active tab
        if (event == ftxui::Event::Tab) {
            if (active_tab_ == 0) active_focus_panel_ = (active_focus_panel_ + 1) % 5;
            else if (active_tab_ == 1) active_focus_panel_ = (active_focus_panel_ + 1) % 2;
            else if (active_tab_ == 2) active_focus_panel_ = (active_focus_panel_ + 1) % 2;
            return true;
        }
        if (event == ftxui::Event::TabBack) {
            if (active_tab_ == 0) active_focus_panel_ = (active_focus_panel_ + 4) % 5;
            else if (active_tab_ == 1) active_focus_panel_ = (active_focus_panel_ + 1) % 2;
            else if (active_tab_ == 2) active_focus_panel_ = (active_focus_panel_ + 1) % 2;
            return true;
        }

        // --- Tab 0 (Dashboard) Navigation ---
        if (active_tab_ == 0) {
            if (active_focus_panel_ == 0) { // Topology explorer
                std::vector<std::string> tree_nodes = { "embed_tokens", "layers" };
                for (int i = 0; i < active_model_.layers; ++i) {
                    tree_nodes.push_back("layers." + std::to_string(i));
                    tree_nodes.push_back("layers." + std::to_string(i) + ".self_attn");
                    tree_nodes.push_back("layers." + std::to_string(i) + ".mlp");
                }
                tree_nodes.push_back("norm");
                tree_nodes.push_back("lm_head");

                int max_idx = static_cast<int>(tree_nodes.size()) - 1;
                
                if (event == ftxui::Event::Character('j') || event == ftxui::Event::ArrowDown) {
                    topology_selected_idx_ = std::min(max_idx, topology_selected_idx_ + 1);
                    return true;
                }
                if (event == ftxui::Event::Character('k') || event == ftxui::Event::ArrowUp) {
                    topology_selected_idx_ = std::max(0, topology_selected_idx_ - 1);
                    return true;
                }
                if (event == ftxui::Event::Return || event == ftxui::Event::Special(" ")) {
                    // Update cache for active layer inspect
                    std::string node = tree_nodes[topology_selected_idx_];
                    
                    // Search ring buffer for matching past events to load in cache
                    auto events = ring_buffer_.get_all();
                    for (const auto& ev : events) {
                        if (ev.layer_name == node) {
                            selected_event_ = ev;
                            break;
                        }
                    }
                    return true;
                }
            }
            else if (active_focus_panel_ == 1) { // Live stream navigator
                int max_idx = static_cast<int>(ring_buffer_.size()) - 1;
                if (event == ftxui::Event::Character('j') || event == ftxui::Event::ArrowDown) {
                    stream_selected_idx_ = std::min(max_idx, stream_selected_idx_ + 1);
                    // Fetch corresponding event from ring buffer
                    auto all_ev = ring_buffer_.get_all();
                    if (!all_ev.empty() && stream_selected_idx_ >= 0 && stream_selected_idx_ < static_cast<int>(all_ev.size())) {
                        selected_event_ = all_ev[all_ev.size() - 1 - stream_selected_idx_];
                    }
                    return true;
                }
                if (event == ftxui::Event::Character('k') || event == ftxui::Event::ArrowUp) {
                    stream_selected_idx_ = std::max(0, stream_selected_idx_ - 1);
                    auto all_ev = ring_buffer_.get_all();
                    if (!all_ev.empty() && stream_selected_idx_ >= 0 && stream_selected_idx_ < static_cast<int>(all_ev.size())) {
                        selected_event_ = all_ev[all_ev.size() - 1 - stream_selected_idx_];
                    }
                    return true;
                }
            }
        }
        
        // --- Tab 1 (Attention & Token Journey) Navigation ---
        if (active_tab_ == 1) {
            if (active_focus_panel_ == 0) { // Heatmap matrix panning
                if (event == ftxui::Event::ArrowLeft || event == ftxui::Event::Character('h')) {
                    attn_pan_x_ = std::max(0, attn_pan_x_ - 1);
                    return true;
                }
                if (event == ftxui::Event::ArrowRight || event == ftxui::Event::Character('l')) {
                    attn_pan_x_++;
                    return true;
                }
                if (event == ftxui::Event::ArrowUp || event == ftxui::Event::Character('k')) {
                    attn_pan_y_ = std::max(0, attn_pan_y_ - 1);
                    return true;
                }
                if (event == ftxui::Event::ArrowDown || event == ftxui::Event::Character('j')) {
                    attn_pan_y_++;
                    return true;
                }
                if (event == ftxui::Event::Character('+')) {
                    attn_contrast_ = std::min(5.0f, attn_contrast_ + 0.2f);
                    return true;
                }
                if (event == ftxui::Event::Character('-')) {
                    attn_contrast_ = std::max(0.2f, attn_contrast_ - 0.2f);
                    return true;
                }
                if (event == ftxui::Event::Character('[')) {
                    attn_zoom_ = std::max(-4, attn_zoom_ - 1);
                    return true;
                }
                if (event == ftxui::Event::Character(']')) {
                    attn_zoom_ = std::min(24, attn_zoom_ + 1);
                    return true;
                }
                if (event == ftxui::Event::Character('<') || event == ftxui::Event::Character(',')) {
                    attn_head_idx_ = std::max(0, attn_head_idx_ - 1);
                    return true;
                }
                if (event == ftxui::Event::Character('>') || event == ftxui::Event::Character('.')) {
                    attn_head_idx_ = std::min(active_attention_.num_heads - 1, attn_head_idx_ + 1);
                    return true;
                }
            } 
            else if (active_focus_panel_ == 1) { // Token journey index cycling
                if (event == ftxui::Event::Character('j') || event == ftxui::Event::ArrowDown) {
                    journey_token_idx_++;
                    return true;
                }
                if (event == ftxui::Event::Character('k') || event == ftxui::Event::ArrowUp) {
                    journey_token_idx_ = std::max(0, journey_token_idx_ - 1);
                    return true;
                }
            }
        }

        // --- Tab 2 (Call Tree Flame Graph) Navigation ---
        if (active_tab_ == 2 && active_focus_panel_ == 0) {
            std::vector<std::string> nodes = { "Inference", "embed_tokens", "layers" };
            for (int i = 0; i < active_model_.layers; ++i) {
                std::string lp = "layers." + std::to_string(i);
                nodes.push_back(lp);
                nodes.push_back(lp + ".input_layernorm");
                nodes.push_back(lp + ".self_attn");
                nodes.push_back(lp + ".post_attention_layernorm");
                nodes.push_back(lp + ".mlp");
            }
            nodes.push_back("norm");
            nodes.push_back("lm_head");

            // Find current selection position
            auto it = std::find(nodes.begin(), nodes.end(), flame_selected_node_);
            int idx = (it != nodes.end()) ? std::distance(nodes.begin(), it) : 0;

            if (event == ftxui::Event::Character('j') || event == ftxui::Event::ArrowDown) {
                idx = std::min(static_cast<int>(nodes.size()) - 1, idx + 1);
                flame_selected_node_ = nodes[idx];
                return true;
            }
            if (event == ftxui::Event::Character('k') || event == ftxui::Event::ArrowUp) {
                idx = std::max(0, idx - 1);
                flame_selected_node_ = nodes[idx];
                return true;
            }
            if (event == ftxui::Event::Return || event == ftxui::Event::Special(" ")) {
                flame_expanded_nodes_[flame_selected_node_] = !flame_expanded_nodes_[flame_selected_node_];
                return true;
            }
        }

        return false;
    });

    screen.Loop(component);
}
