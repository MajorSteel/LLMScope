#pragma once
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include "telemetry/types.hpp"
#include "telemetry/event_bus.hpp"

class AnalyzerPlugin {
public:
    virtual ~AnalyzerPlugin() = default;
    virtual std::string get_name() const = 0;
    virtual void process_event(const TelemetryEvent& event, EventBus& bus) = 0;
};

class PluginManager {
public:
    explicit PluginManager(EventBus& event_bus);

    // Register a plugin
    void register_plugin(std::unique_ptr<AnalyzerPlugin> plugin);

    // Run all loaded plugins against a telemetry event
    void process_event(const TelemetryEvent& event);

    // Get names of all loaded plugins
    std::vector<std::string> get_loaded_plugins() const;

private:
    EventBus& event_bus_;
    std::vector<std::unique_ptr<AnalyzerPlugin>> plugins_;
    mutable std::mutex mutex_;
};

// Built-in plugins
class AttentionEntropyPlugin : public AnalyzerPlugin {
public:
    std::string get_name() const override { return "Attention Entropy Plugin"; }
    void process_event(const TelemetryEvent& event, EventBus& bus) override;
    
    // Get last calculated head entropies
    static std::vector<float> get_last_entropies() {
        std::lock_guard<std::mutex> lock(static_mutex_);
        return last_entropies_;
    }

private:
    static std::vector<float> last_entropies_;
    static std::mutex static_mutex_;
};

class QuantizationMemoryPlugin : public AnalyzerPlugin {
public:
    std::string get_name() const override { return "Quantization Memory Plugin"; }
    void process_event(const TelemetryEvent& event, EventBus& bus) override;

    // Returns: {fp32_mb, fp16_mb, int8_mb, q4_mb}
    struct MemoryProjection {
        double fp32_mb = 0.0;
        double fp16_mb = 0.0;
        double int8_mb = 0.0;
        double q4_mb = 0.0;
    };
    
    static MemoryProjection get_projection() {
        std::lock_guard<std::mutex> lock(static_mutex_);
        return last_projection_;
    }

private:
    static MemoryProjection last_projection_;
    static std::mutex static_mutex_;
};
