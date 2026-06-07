#pragma once
#include <string>
#include <thread>
#include <atomic>
#include "telemetry/event_bus.hpp"
#include "storage/ring_buffer.hpp"
#include "telemetry/aggregator.hpp"
#include "anomaly/anomaly_detector.hpp"
#include "replay/replay_manager.hpp"
#include "instrumentation/device_monitor.hpp"

class WebServer {
public:
    WebServer(EventBus& event_bus, 
              RingBuffer& ring_buffer, 
              TelemetryAggregator& aggregator, 
              AnomalyDetector& anomaly_detector,
              ReplayManager& replay_manager,
              DeviceMonitor& device_monitor,
              int port = 8080);
    ~WebServer();

    // Start background HTTP listener
    bool start();

    // Stop web server
    void stop();

    bool is_running() const { return running_; }

private:
    void listen_loop();
    void handle_client(int64_t client_socket);

    EventBus& event_bus_;
    RingBuffer& ring_buffer_;
    TelemetryAggregator& aggregator_;
    AnomalyDetector& anomaly_detector_;
    ReplayManager& replay_manager_;
    DeviceMonitor& device_monitor_;
    int port_;

    std::thread server_thread_;
    std::atomic<bool> running_{false};
    int64_t server_fd_ = -1;
};
