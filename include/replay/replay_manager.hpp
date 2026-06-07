#pragma once
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include "telemetry/types.hpp"
#include "telemetry/event_bus.hpp"

class ReplayManager {
public:
    explicit ReplayManager(EventBus& event_bus);
    ~ReplayManager();

    // Load static telemetry events for replay
    void load_session(const std::vector<TelemetryEvent>& events);

    // Playback control
    void play();
    void pause();
    void step_forward();
    void step_backward();
    void jump_to(size_t index);

    bool is_playing() const { return playing_; }
    size_t current_index() const { return current_index_; }
    size_t total_events() const { return events_.size(); }
    void set_speed_ms(int ms) { speed_ms_ = ms; }
    int speed_ms() const { return speed_ms_; }

    void append_event(const TelemetryEvent& event);
    bool is_replay_mode() const;
    void set_replay_mode(bool enable);

private:
    void start_loop();
    void stop_loop();
    void replay_loop();

    EventBus& event_bus_;
    std::vector<TelemetryEvent> events_;
    std::atomic<size_t> current_index_{0};
    std::atomic<bool> playing_{false};
    std::atomic<int> speed_ms_{200}; // time between events during play
    std::atomic<bool> replay_mode_{false};

    std::thread loop_thread_;
    std::atomic<bool> run_loop_{false};
    mutable std::mutex mutex_;
};
