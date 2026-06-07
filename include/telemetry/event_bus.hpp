#pragma once
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "telemetry/types.hpp"

class EventBus {
public:
    using EventCallback = std::function<void(const TelemetryEvent&)>;

    // Subscribe to a specific event type (e.g., "layer_trace", "*")
    void subscribe(const std::string& event_type, EventCallback callback);

    // Publish an event to all interested subscribers
    void publish(const TelemetryEvent& event);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, std::vector<EventCallback>> subscribers_;
};
