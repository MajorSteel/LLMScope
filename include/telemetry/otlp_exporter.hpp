#pragma once
#include <string>
#include <vector>
#include <mutex>
#include "telemetry/types.hpp"

class OTLPExporter {
public:
    explicit OTLPExporter(const std::string& output_filepath = "llmscope_otlp.json");
    ~OTLPExporter();

    // Consume a telemetry event, convert it, and append to exporter batch
    void process_event(const TelemetryEvent& event);

    // Export accumulated records to file
    bool export_now();

    void clear();

private:
    std::string filepath_;
    nlohmann::json log_records_;
    mutable std::mutex mutex_;
};
