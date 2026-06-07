#include "storage/ring_buffer.hpp"

RingBuffer::RingBuffer(size_t capacity) : capacity_(capacity), buffer_(capacity) {}

void RingBuffer::push(const TelemetryEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (event.event_type == "attention_weights") {
        latest_attns_[event.attention.layer_name] = event;
        return;
    }
    buffer_[write_ptr_] = event;
    write_ptr_ = (write_ptr_ + 1) % capacity_;
    if (size_ < capacity_) {
        size_++;
    }
}

std::vector<TelemetryEvent> RingBuffer::get_all() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<TelemetryEvent> result;
    result.reserve(size_ + latest_attns_.size());
    
    if (size_ < capacity_) {
        for (size_t i = 0; i < size_; ++i) {
            result.push_back(buffer_[i]);
        }
    } else {
        for (size_t i = 0; i < capacity_; ++i) {
            size_t idx = (write_ptr_ + i) % capacity_;
            result.push_back(buffer_[idx]);
        }
    }

    for (const auto& pair : latest_attns_) {
        result.push_back(pair.second);
    }
    return result;
}

TelemetryEvent RingBuffer::get_at(size_t index) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index >= size_) return {};
    
    if (size_ < capacity_) {
        return buffer_[index];
    } else {
        size_t idx = (write_ptr_ + index) % capacity_;
        return buffer_[idx];
    }
}

size_t RingBuffer::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return size_;
}

size_t RingBuffer::capacity() const {
    return capacity_;
}

void RingBuffer::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    write_ptr_ = 0;
    size_ = 0;
    latest_attns_.clear();
}
