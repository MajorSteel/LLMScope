#pragma once
#include <vector>
#include <string>
#include <mutex>
#include "telemetry/types.hpp"
#include "telemetry/event_bus.hpp"

class AnomalyDetector {
public:
    struct Config {
        double exploding_threshold = 10.0;
        double dead_layer_variance_threshold = 1e-6;
        double high_sparsity_threshold = 95.0; // percentage

        Config() : exploding_threshold(10.0), dead_layer_variance_threshold(1e-6), high_sparsity_threshold(95.0) {}
    };

    explicit AnomalyDetector(EventBus& event_bus, Config config = Config());

    // Run audit checks on a telemetry event
    void process_event(const TelemetryEvent& event);

    // Retrieve active list of generated alerts
    std::vector<AnomalyAlert> get_alerts() const;
    void clear_alerts();

private:
    void audit_layer_trace(const TelemetryEvent& event);
    bool check_nan_inf(double val) const;

    EventBus& event_bus_;
    Config config_;
    std::vector<AnomalyAlert> alerts_;
    mutable std::mutex mutex_;
};
