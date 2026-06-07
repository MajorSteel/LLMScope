#include "telemetry/aggregator.hpp"
#include <algorithm>
#include <numeric>

TelemetryAggregator::TelemetryAggregator() {
    reset();
}

void TelemetryAggregator::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    latencies_by_layer_.clear();
    layer_types_.clear();
    device_counts_.clear();
    total_events_ = 0;
    token_count_ = 0;
    total_latency_ms_ = 0.0;
    first_token_ = true;
}

void TelemetryAggregator::process_event(const TelemetryEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (event.event_type != "layer_trace") return;
    
    total_events_++;
    total_latency_ms_ += event.latency_ms;
    
    latencies_by_layer_[event.layer_name].push_back(event.latency_ms);
    layer_types_[event.layer_name] = event.layer_type;
    device_counts_[event.device]++;

    // We increment token count when we hit the output head (LMHead)
    if (event.layer_type == "LMHead" || event.layer_name.find("lm_head") != std::string::npos || event.layer_name.find("output") != std::string::npos) {
        token_count_++;
        last_token_time_ = std::chrono::steady_clock::now();
        if (first_token_) {
            session_start_ = std::chrono::steady_clock::now();
            first_token_ = false;
        }
    }
}

double TelemetryAggregator::get_tokens_per_sec() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (first_token_ || token_count_ == 0) {
        return 0.0;
    }
    
    auto now = std::chrono::steady_clock::now();
    
    // Freeze calculated speed if no new tokens have arrived for 3 seconds
    auto idle_seconds = std::chrono::duration_cast<std::chrono::seconds>(now - last_token_time_).count();
    auto end_time = now;
    if (idle_seconds > 3) {
        end_time = last_token_time_;
    }
    
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - session_start_).count();
    
    if (elapsed > 10) {
        return (token_count_ * 1000.0) / elapsed;
    }
    
    // Backup estimate using average latency of a full pass
    if (total_latency_ms_ > 0) {
        return (token_count_ * 1000.0) / total_latency_ms_;
    }
    return 0.0;
}

double TelemetryAggregator::get_avg_inference_latency() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (token_count_ == 0) {
        return total_latency_ms_;
    }
    return total_latency_ms_ / token_count_;
}

std::vector<LayerLatencyInfo> TelemetryAggregator::get_slowest_layers(size_t limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<LayerLatencyInfo> list;
    list.reserve(latencies_by_layer_.size());
    
    for (const auto& pair : latencies_by_layer_) {
        const auto& name = pair.first;
        const auto& latencies = pair.second;
        if (latencies.empty()) continue;
        
        double sum = std::accumulate(latencies.begin(), latencies.end(), 0.0);
        double avg = sum / latencies.size();
        double max_val = *std::max_element(latencies.begin(), latencies.end());
        
        LayerLatencyInfo info;
        info.name = name;
        info.type = layer_types_.at(name);
        info.avg_latency_ms = avg;
        info.max_latency_ms = max_val;
        info.call_count = static_cast<int>(latencies.size());
        
        list.push_back(info);
    }
    
    // Sort descending by average latency
    std::sort(list.begin(), list.end(), [](const LayerLatencyInfo& a, const LayerLatencyInfo& b) {
        return a.avg_latency_ms > b.avg_latency_ms;
    });
    
    if (list.size() > limit) {
        list.resize(limit);
    }
    
    return list;
}

std::unordered_map<std::string, int> TelemetryAggregator::get_device_distribution() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return device_counts_;
}
