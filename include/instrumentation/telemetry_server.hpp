#pragma once
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include "telemetry/event_bus.hpp"

class TelemetryServer {
public:
    explicit TelemetryServer(EventBus& event_bus, int port = 5005);
    ~TelemetryServer();

    // Start listening on background thread
    bool start();

    // Stop listening
    void stop();

    bool is_running() const { return running_; }
    bool is_client_connected() const { return client_connected_; }

private:
    void listen_loop();
    void handle_client(int64_t client_socket);

    EventBus& event_bus_;
    int port_;
    std::thread server_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> client_connected_{false};
    int64_t server_fd_ = -1; // Socket file descriptor
    mutable std::mutex mutex_;
};
