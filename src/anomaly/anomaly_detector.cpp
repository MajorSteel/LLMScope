#include "anomaly/anomaly_detector.hpp"
#include <cmath>
#include <chrono>
#include <iomanip>
#include <sstream>

AnomalyDetector::AnomalyDetector(EventBus& event_bus, Config config) 
    : event_bus_(event_bus), config_(config) {}

bool AnomalyDetector::check_nan_inf(double val) const {
    return std::isnan(val) || std::isinf(val);
}

static std::string get_current_timestamp_str() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    
    // Get milliseconds
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ) % 1000;

    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%H:%M:%S")
       << "." << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

void AnomalyDetector::process_event(const TelemetryEvent& event) {
    if (event.event_type == "layer_trace") {
        audit_layer_trace(event);
    }
}

void AnomalyDetector::audit_layer_trace(const TelemetryEvent& event) {
    const auto& stats = event.activation_stats;
    std::vector<AnomalyAlert> generated_alerts;
    
    std::string timestamp = get_current_timestamp_str();

    // 1. NaN or Inf check
    if (check_nan_inf(stats.mean) || check_nan_inf(stats.min_val) || check_nan_inf(stats.max_val) || check_nan_inf(stats.variance)) {
        AnomalyAlert alert;
        alert.timestamp = timestamp;
        alert.severity = "ERROR";
        alert.layer_name = event.layer_name;
        alert.description = "Numerical Instability: NaN/Inf detected in activations.";
        generated_alerts.push_back(alert);
    }

    // 2. Exploding activations check
    if (std::abs(stats.max_val) > config_.exploding_threshold || std::abs(stats.min_val) > config_.exploding_threshold) {
        AnomalyAlert alert;
        alert.timestamp = timestamp;
        alert.severity = "WARNING";
        alert.layer_name = event.layer_name;
        
        std::stringstream ss;
        ss << "Exploding Activation: Val max " << std::fixed << std::setprecision(2) 
           << stats.max_val << " / min " << stats.min_val 
           << " exceeds threshold " << config_.exploding_threshold;
        alert.description = ss.str();
        generated_alerts.push_back(alert);
    }

    // 3. Dead layer check
    if (stats.variance < config_.dead_layer_variance_threshold && 
        event.layer_type != "Embedding" && event.layer_type != "LayerNorm" && event.layer_type != "RMSNorm") {
        AnomalyAlert alert;
        alert.timestamp = timestamp;
        alert.severity = "WARNING";
        alert.layer_name = event.layer_name;
        
        std::stringstream ss;
        ss << "Dead Layer: Near-zero activation variance (" << std::scientific 
           << std::setprecision(2) << stats.variance << ")";
        alert.description = ss.str();
        generated_alerts.push_back(alert);
    }

    // 4. Excessive Sparsity check
    if (stats.sparsity > config_.high_sparsity_threshold) {
        AnomalyAlert alert;
        alert.timestamp = timestamp;
        alert.severity = "WARNING";
        alert.layer_name = event.layer_name;
        
        std::stringstream ss;
        ss << "High Sparsity Alert: Activations are " << std::fixed << std::setprecision(1) 
           << stats.sparsity << "% sparse (threshold " << config_.high_sparsity_threshold << "%)";
        alert.description = ss.str();
        generated_alerts.push_back(alert);
    }

    // 5. CUDA Fallback Warning
    if (event.device == "CPU" && event.layer_name.find("attn") != std::string::npos && 
        event.event_id > 10) { // arbitrary threshold to avoid startup warnings
        AnomalyAlert alert;
        alert.timestamp = timestamp;
        alert.severity = "WARNING";
        alert.layer_name = event.layer_name;
        alert.description = "Performance Fallback: CPU fallback processing attention layer.";
        generated_alerts.push_back(alert);
    }

    // Publish all alerts to the event bus
    if (!generated_alerts.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& alert : generated_alerts) {
            alerts_.push_back(alert);
            
            // Generate and publish the anomaly event
            TelemetryEvent anomaly_event;
            anomaly_event.event_type = "anomaly";
            anomaly_event.timestamp = event.timestamp;
            anomaly_event.anomaly = alert;
            
            event_bus_.publish(anomaly_event);
        }
    }
}

std::vector<AnomalyAlert> AnomalyDetector::get_alerts() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return alerts_;
}

void AnomalyDetector::clear_alerts() {
    std::lock_guard<std::mutex> lock(mutex_);
    alerts_.clear();
}
