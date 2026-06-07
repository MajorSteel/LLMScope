#include "replay/replay_manager.hpp"
#include <chrono>
#include <spdlog/spdlog.h>

ReplayManager::ReplayManager(EventBus& event_bus) : event_bus_(event_bus) {
    start_loop();
    event_bus_.subscribe("*", [this](const TelemetryEvent& event) {
        append_event(event);
    });
}

ReplayManager::~ReplayManager() {
    stop_loop();
}

void ReplayManager::load_session(const std::vector<TelemetryEvent>& events) {
    std::lock_guard<std::mutex> lock(mutex_);
    events_ = events;
    current_index_ = 0;
    playing_ = false;
    
    // Publish the first event to initialize views
    if (!events_.empty()) {
        event_bus_.publish(events_[0]);
    }
}

void ReplayManager::play() {
    playing_ = true;
}

void ReplayManager::pause() {
    playing_ = false;
}

void ReplayManager::step_forward() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (events_.empty()) return;
    
    size_t idx = current_index_;
    if (idx < events_.size() - 1) {
        idx++;
        current_index_ = idx;
        event_bus_.publish(events_[idx]);
    } else {
        playing_ = false; // Stop playing at the end
    }
}

void ReplayManager::step_backward() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (events_.empty()) return;
    
    size_t idx = current_index_;
    if (idx > 0) {
        idx--;
        current_index_ = idx;
        event_bus_.publish(events_[idx]);
    }
}

void ReplayManager::jump_to(size_t index) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (events_.empty() || index >= events_.size()) return;
    
    current_index_ = index;
    event_bus_.publish(events_[index]);
}

void ReplayManager::start_loop() {
    run_loop_ = true;
    loop_thread_ = std::thread(&ReplayManager::replay_loop, this);
}

void ReplayManager::stop_loop() {
    run_loop_ = false;
    playing_ = false;
    if (loop_thread_.joinable()) {
        loop_thread_.join();
    }
}

void ReplayManager::replay_loop() {
    while (run_loop_) {
        if (playing_) {
            step_forward();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(speed_ms_));
    }
}

void ReplayManager::append_event(const TelemetryEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (replay_mode_) return;
    events_.push_back(event);
    current_index_ = events_.size() - 1;
}

bool ReplayManager::is_replay_mode() const {
    return replay_mode_;
}

void ReplayManager::set_replay_mode(bool enable) {
    replay_mode_ = enable;
}
