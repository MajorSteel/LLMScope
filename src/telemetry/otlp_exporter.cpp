#include "telemetry/otlp_exporter.hpp"
#include <fstream>
#include <chrono>
#include <spdlog/spdlog.h>

OTLPExporter::OTLPExporter(const std::string& output_filepath) 
    : filepath_(output_filepath), log_records_(nlohmann::json::array()) {}

OTLPExporter::~OTLPExporter() {
    export_now();
}

void OTLPExporter::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    log_records_ = nlohmann::json::array();
}

void OTLPExporter::process_event(const TelemetryEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    nlohmann::json record;
    uint64_t nano_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    record["timeUnixNano"] = std::to_string(nano_time);
    
    // Map event types to OTLP representation
    if (event.event_type == "layer_trace") {
        record["severityText"] = "INFO";
        record["severityNumber"] = 9; // INFO
        record["body"] = {{"stringValue", "Layer Execution: " + event.layer_name}};
        
        nlohmann::json attrs = nlohmann::json::array();
        attrs.push_back({{"key", "event.type"}, {"value", {{"stringValue", "layer_trace"}}}});
        attrs.push_back({{"key", "layer.name"}, {"value", {{"stringValue", event.layer_name}}}});
        attrs.push_back({{"key", "layer.type"}, {"value", {{"stringValue", event.layer_type}}}});
        attrs.push_back({{"key", "device"}, {"value", {{"stringValue", event.device}}}});
        attrs.push_back({{"key", "latency.ms"}, {"value", {{"doubleValue", event.latency_ms}}}});
        attrs.push_back({{"key", "stats.mean"}, {"value", {{"doubleValue", event.activation_stats.mean}}}});
        attrs.push_back({{"key", "stats.sparsity"}, {"value", {{"doubleValue", event.activation_stats.sparsity}}}});
        
        // Add shape info as a string attribute
        std::string shape_str = "[";
        for (size_t i = 0; i < event.output_tensor.shape.size(); ++i) {
            shape_str += std::to_string(event.output_tensor.shape[i]);
            if (i < event.output_tensor.shape.size() - 1) shape_str += ",";
        }
        shape_str += "]";
        attrs.push_back({{"key", "tensor.shape"}, {"value", {{"stringValue", shape_str}}}});
        
        record["attributes"] = attrs;
    } 
    else if (event.event_type == "anomaly") {
        record["severityText"] = event.anomaly.severity;
        record["severityNumber"] = (event.anomaly.severity == "ERROR") ? 17 : 13; // ERROR vs WARN
        record["body"] = {{"stringValue", event.anomaly.description}};
        
        nlohmann::json attrs = nlohmann::json::array();
        attrs.push_back({{"key", "event.type"}, {"value", {{"stringValue", "anomaly"}}}});
        attrs.push_back({{"key", "layer.name"}, {"value", {{"stringValue", event.anomaly.layer_name}}}});
        attrs.push_back({{"key", "anomaly.severity"}, {"value", {{"stringValue", event.anomaly.severity}}}});
        
        record["attributes"] = attrs;
    }
    else if (event.event_type == "model_info") {
        record["severityText"] = "INFO";
        record["severityNumber"] = 9;
        record["body"] = {{"stringValue", "Model Initialization: " + event.model_info.name}};
        
        nlohmann::json attrs = nlohmann::json::array();
        attrs.push_back({{"key", "event.type"}, {"value", {{"stringValue", "model_info"}}}});
        attrs.push_back({{"key", "model.name"}, {"value", {{"stringValue", event.model_info.name}}}});
        attrs.push_back({{"key", "model.layers"}, {"value", {{"intValue", event.model_info.layers}}}});
        attrs.push_back({{"key", "model.quantization"}, {"value", {{"stringValue", event.model_info.quantization}}}});
        
        record["attributes"] = attrs;
    }
    else {
        return; // Don't log weights matrices in standard OTLP to avoid giant exports
    }
    
    log_records_.push_back(record);
}

bool OTLPExporter::export_now() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (log_records_.empty()) return true;

    try {
        nlohmann::json resource_log = {
            {"resource", {
                {"attributes", nlohmann::json::array({
                    {{"key", "service.name"}, {"value", {{"stringValue", "llmscope"}}}},
                    {{"key", "telemetry.sdk.language"}, {"value", {{"stringValue", "cpp"}}}}
                })}
            }},
            {"scopeLogs", nlohmann::json::array({
                {
                    {"scope", {{"name", "llmscope.collector"}, {"version", "1.0.0"}}},
                    {"logRecords", log_records_}
                }
            })}
        };

        nlohmann::json otlp_payload = {
            {"resourceLogs", nlohmann::json::array({resource_log})}
        };

        std::ofstream file(filepath_);
        if (!file.is_open()) {
            spdlog::error("Failed to open OTLP export file: {}", filepath_);
            return false;
        }
        
        file << otlp_payload.dump(4);
        spdlog::info("Successfully exported {} events to OTLP file: {}", log_records_.size(), filepath_);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Exception in OTLP export: {}", e.what());
        return false;
    }
}
