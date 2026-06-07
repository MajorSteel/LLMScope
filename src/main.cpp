#include <iostream>
#include <string>
#include <memory>
#include <spdlog/spdlog.h>
#include "telemetry/event_bus.hpp"
#include "telemetry/aggregator.hpp"
#include "telemetry/otlp_exporter.hpp"
#include "anomaly/anomaly_detector.hpp"
#include "storage/ring_buffer.hpp"
#include "replay/replay_manager.hpp"
#include "plugins/plugin_manager.hpp"
#include "instrumentation/device_monitor.hpp"
#include "instrumentation/telemetry_server.hpp"
#include "instrumentation/mock_tracer.hpp"
#include "web/web_server.hpp"
#ifndef NO_TUI
#include "tui/dashboard.hpp"
#endif

void print_usage() {
    std::cout << "LLMScope: Local LLM Instrumentation, Tracing & Teleplay Observability Platform\n"
              << "Usage: llmscope [options]\n"
              << "Options:\n"
              << "  -h, --help       Show this help message\n"
              << "  --web            Start the web dashboard server on http://localhost:8080\n"
              << "  --sim            Start stand-alone telemetry simulation immediately\n"
              << "  --port <port>    Specify the TCP telemetry receiver port (default: 5005)\n"
              << "  --web-port <port> Specify the Web Dashboard port (default: 8080)\n";
}

int main(int argc, char* argv[]) {
    // Configure default spdlog level
    spdlog::set_level(spdlog::level::info);
    spdlog::info("Initializing LLMScope Observability Platform...");

    bool start_web = false;
    bool start_sim = false;
    int receiver_port = 5005;
    int web_port = 8080;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage();
            return 0;
        } else if (arg == "--web") {
            start_web = true;
        } else if (arg == "--sim") {
            start_sim = true;
        } else if (arg == "--port" && i + 1 < argc) {
            receiver_port = std::stoi(argv[++i]);
        } else if (arg == "--web-port" && i + 1 < argc) {
            web_port = std::stoi(argv[++i]);
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            print_usage();
            return 1;
        }
    }

    // 1. Core Platform Instances
    auto event_bus = std::make_unique<EventBus>();
    auto ring_buffer = std::make_unique<RingBuffer>(10000);
    auto aggregator = std::make_unique<TelemetryAggregator>();
    auto anomaly_detector = std::make_unique<AnomalyDetector>(*event_bus);
    auto replay_manager = std::make_unique<ReplayManager>(*event_bus);
    
    // 2. Plugin Manager Setup
    auto plugin_manager = std::make_unique<PluginManager>(*event_bus);
    plugin_manager->register_plugin(std::make_unique<AttentionEntropyPlugin>());
    plugin_manager->register_plugin(std::make_unique<QuantizationMemoryPlugin>());

    // 3. Exporter Setup
    auto otlp_exporter = std::make_unique<OTLPExporter>("llmscope_otlp.json");

    // 4. Hardware Monitor
    auto device_monitor = DeviceMonitor::create();

    // 5. Server/Tracer Communications
    auto server = std::make_unique<TelemetryServer>(*event_bus, receiver_port);
    auto tracer = std::make_unique<MockTracer>(*event_bus, *device_monitor);

    // 6. Optionally initialize Web Server
    std::unique_ptr<WebServer> web_server;
    if (start_web) {
        web_server = std::make_unique<WebServer>(
            *event_bus, *ring_buffer, *aggregator, *anomaly_detector, *replay_manager, *device_monitor, web_port
        );
    }

#ifdef NO_TUI
    // Subscribe core components to EventBus when running headless
    event_bus->subscribe("*", [&ring_buffer, &aggregator, &anomaly_detector, &plugin_manager, &otlp_exporter](const TelemetryEvent& event) {
        ring_buffer->push(event);
        aggregator->process_event(event);
        anomaly_detector->process_event(event);
        otlp_exporter->process_event(event);
        plugin_manager->process_event(event);
    });
#endif

    // Start communications threads
    server->start();
    
    if (start_web && web_server) {
        web_server->start();
    }
    
    if (start_sim) {
        tracer->start();
    }

    // 7. Initialize & Run TUI Dashboard or Headless Web Loop
#ifndef NO_TUI
    try {
        TUIDashboard dashboard(
            *event_bus, *ring_buffer, *aggregator, *anomaly_detector, 
            *replay_manager, *plugin_manager, *device_monitor, 
            *server, *tracer, *otlp_exporter
        );
        
        dashboard.run();
    } catch (const std::exception& e) {
        spdlog::error("Critical error running TUI dashboard: {}", e.what());
    }
#else
    spdlog::info("Running in headless web server mode. Point your browser to http://localhost:8080");
    std::cout << "\n=========================================================\n";
    std::cout << "  LLMScope Headless Web Dashboard Mode Activated\n";
    std::cout << "  - Telemetry TCP Server: port " << receiver_port << "\n";
    std::cout << "  - Web Dashboard UI:     http://localhost:" << web_port << "\n";
    std::cout << "  Press Ctrl+C to stop the application.\n";
    std::cout << "=========================================================\n\n";
    
    // Periodically log statistics to terminal
    while (server->is_running() || (start_sim && tracer->is_running())) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        double tps = aggregator->get_tokens_per_sec();
        double lat = aggregator->get_avg_inference_latency();
        int total = aggregator->get_total_events_processed();
        int anomalies = anomaly_detector->get_alerts().size();
        
        // Use custom spdlog-friendly formatting by passing manually rounded/trimmed values
        double tps_rounded = static_cast<int>(tps * 100.0) / 100.0;
        double lat_rounded = static_cast<int>(lat * 10.0) / 10.0;
        
        spdlog::info("[LLMScope Stats] Speed: {} tok/s | Avg Latency: {} ms | Events: {} | Anomalies: {}", 
                     tps_rounded, lat_rounded, total, anomalies);
    }
#endif

    // 8. Graceful Shutdown & Exports
    spdlog::info("Shutting down LLMScope services...");
    tracer->stop();
    if (start_web && web_server) {
        web_server->stop();
    }
    server->stop();
    
    // Ensure trace buffers dump cleanly before program termination
    otlp_exporter->export_now();
    spdlog::info("LLMScope session shut down successfully.");
    return 0;
}
