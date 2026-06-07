#pragma once
#include <thread>
#include <atomic>
#include "telemetry/event_bus.hpp"
#include "instrumentation/device_monitor.hpp"

class MockTracer {
public:
    MockTracer(EventBus& event_bus, DeviceMonitor& device_monitor);
    ~MockTracer();

    // Start simulation thread
    void start();

    // Stop simulation
    void stop();

    bool is_running() const { return running_; }

private:
    void simulation_loop();
    void generate_token_pass(int token_idx, int total_tokens, int64_t& global_event_id);

    EventBus& event_bus_;
    DeviceMonitor& device_monitor_;
    std::thread runner_thread_;
    std::atomic<bool> running_{false};
};
