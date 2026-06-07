#include "telemetry/event_bus.hpp"

void EventBus::subscribe(const std::string& event_type, EventCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    subscribers_[event_type].push_back(callback);
}

void EventBus::publish(const TelemetryEvent& event) {
    std::vector<EventCallback> targets;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Add direct type subscribers
        auto it = subscribers_.find(event.event_type);
        if (it != subscribers_.end()) {
            targets.insert(targets.end(), it->second.begin(), it->second.end());
        }
        
        // Add global wildcard subscribers
        auto it_wild = subscribers_.find("*");
        if (it_wild != subscribers_.end()) {
            targets.insert(targets.end(), it_wild->second.begin(), it_wild->second.end());
        }
    }
    
    for (const auto& callback : targets) {
        if (callback) {
            callback(event);
        }
    }
}
