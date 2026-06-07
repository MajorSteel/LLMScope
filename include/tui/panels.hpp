#pragma once
#include <ftxui/dom/elements.hpp>
#include <vector>
#include <string>
#include <unordered_map>
#include "telemetry/types.hpp"
#include "telemetry/aggregator.hpp"
#include "anomaly/anomaly_detector.hpp"

namespace tui {

using namespace ftxui;

class Panels {
public:
    // 1. Model Topology panel (Tree representation)
    static Element render_topology(const ModelInfo& model, 
                                   const std::string& active_layer, 
                                   int selected_idx, 
                                   bool focused);

    // 2. Live Stream grid (List of events)
    static Element render_live_stream(const std::vector<TelemetryEvent>& events, 
                                      int selected_idx, 
                                      bool focused);

    // 3. Attention Visualizer Heatmap
    static Element render_attention_matrix(const AttentionData& attn, 
                                           int head_idx, 
                                           int pan_x, 
                                           int pan_y, 
                                           int zoom, 
                                           float contrast, 
                                           bool focused);

    // 4. Runtime Metrics Inspector
    static Element render_metrics_inspector(const TelemetryEvent& event, bool focused);

    // 5. Numerical Anomaly Ledger
    static Element render_anomaly_ledger(const std::vector<AnomalyAlert>& alerts, bool focused);

    // 6. Performance Dashboard (timings and CPU/GPU stats)
    static Element render_performance_dashboard(const TelemetryAggregator& aggregator, 
                                                double cpu, double ram, double ram_tot, 
                                                double gpu, double vram, double vram_tot, 
                                                bool focused);

    // 7. Flame Graph / Call Tree panel
    static Element render_flame_graph(const std::vector<TelemetryEvent>& events, 
                                      const std::unordered_map<std::string, bool>& expanded_nodes,
                                      const std::string& selected_node,
                                      bool focused);

    // 8. Token Journey tracing panel
    static Element render_token_journey(const std::vector<TelemetryEvent>& events, 
                                        int token_idx, 
                                        int scroll_offset,
                                        bool focused);

    // 9. Comparative Run Viewer
    static Element render_comparison(const std::vector<TelemetryEvent>& runA, 
                                     const std::vector<TelemetryEvent>& runB, 
                                     const std::string& nameA, 
                                     const std::string& nameB,
                                     bool focused);
};

} // namespace tui
