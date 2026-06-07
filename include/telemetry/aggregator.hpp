#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include "telemetry/types.hpp"

struct LayerLatencyInfo {
    std::string name = "";
    std::string type = "";
    double avg_latency_ms = 0.0;
    double max_latency_ms = 0.0;
    int call_count = 0;
};

class TelemetryAggregator {
public:
    TelemetryAggregator();

    // Consume a telemetry event and update aggregates
    void process_event(const TelemetryEvent& event);

    // Stats queries
    double get_tokens_per_sec() const;
    double get_avg_inference_latency() const;
    std::vector<LayerLatencyInfo> get_slowest_layers(size_t limit = 10) const;
    std::unordered_map<std::string, int> get_device_distribution() const;
    size_t get_total_events_processed() const { return total_events_; }
    void reset();

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::vector<double>> latencies_by_layer_;
    std::unordered_map<std::string, std::string> layer_types_;
    std::unordered_map<std::string, int> device_counts_;
    
    size_t total_events_ = 0;
    int token_count_ = 0;
    double total_latency_ms_ = 0.0;
    
    // For rolling tokens/sec calculations
    std::chrono::steady_clock::time_point session_start_;
    std::chrono::steady_clock::time_point last_token_time_;
    bool first_token_ = true;
};
