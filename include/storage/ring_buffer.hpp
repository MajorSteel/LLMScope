#pragma once
#include <vector>
#include <mutex>
#include "telemetry/types.hpp"

#include <unordered_map>
#include <string>

class RingBuffer {
public:
    explicit RingBuffer(size_t capacity = 10000);

    // Add event, evicting the oldest if full
    void push(const TelemetryEvent& event);

    // Retrieve all active events in chronological order (oldest to newest)
    std::vector<TelemetryEvent> get_all() const;

    // Get an event by chronological index (0 = oldest, size() - 1 = newest)
    TelemetryEvent get_at(size_t index) const;

    size_t size() const;
    size_t capacity() const;
    void clear();

private:
    size_t capacity_;
    std::vector<TelemetryEvent> buffer_;
    size_t write_ptr_ = 0;
    size_t size_ = 0;
    std::unordered_map<std::string, TelemetryEvent> latest_attns_;
    mutable std::mutex mutex_;
};
