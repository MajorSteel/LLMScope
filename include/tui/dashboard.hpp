#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include "telemetry/event_bus.hpp"
#include "storage/ring_buffer.hpp"
#include "telemetry/aggregator.hpp"
#include "anomaly/anomaly_detector.hpp"
#include "replay/replay_manager.hpp"
#include "instrumentation/device_monitor.hpp"
#include "instrumentation/telemetry_server.hpp"
#include "instrumentation/mock_tracer.hpp"
#include "plugins/plugin_manager.hpp"
#include "telemetry/otlp_exporter.hpp"

class TUIDashboard {
public:
    TUIDashboard(EventBus& event_bus,
                 RingBuffer& ring_buffer,
                 TelemetryAggregator& aggregator,
                 AnomalyDetector& anomaly_detector,
                 ReplayManager& replay_manager,
                 PluginManager& plugin_manager,
                 DeviceMonitor& device_monitor,
                 TelemetryServer& server,
                 MockTracer& tracer,
                 OTLPExporter& exporter);

    // Enter rendering loops
    void run();

private:
    void setup_event_subscriptions();
    void process_incoming_event(const TelemetryEvent& event);

    EventBus& event_bus_;
    RingBuffer& ring_buffer_;
    TelemetryAggregator& aggregator_;
    AnomalyDetector& anomaly_detector_;
    ReplayManager& replay_manager_;
    PluginManager& plugin_manager_;
    DeviceMonitor& device_monitor_;
    TelemetryServer& server_;
    MockTracer& tracer_;
    OTLPExporter& exporter_;

    // TUI State Variables
    int active_tab_ = 0;
    int active_focus_panel_ = 0; // index of panel in focus inside active tab
    bool show_knowledge_popup_ = false;
    bool replay_mode_ = false;

    // Panel Selection Indices
    int topology_selected_idx_ = 0;
    int stream_selected_idx_ = 0;
    std::string flame_selected_node_ = "Inference";
    std::unordered_map<std::string, bool> flame_expanded_nodes_;

    // Attention viewport attributes
    int attn_head_idx_ = 0;
    int attn_pan_x_ = 0;
    int attn_pan_y_ = 0;
    int attn_zoom_ = 0;
    float attn_contrast_ = 1.0f;

    // Token Journey selection
    int journey_token_idx_ = 0;
    int journey_scroll_offset_ = 0;

    // Model and event caches
    ModelInfo active_model_;
    TelemetryEvent selected_event_;
    AttentionData active_attention_;

    // Second run storage for comparisons
    std::vector<TelemetryEvent> comparison_run_b_;
    std::string comparison_name_b_ = "Simulated Run B";
};
