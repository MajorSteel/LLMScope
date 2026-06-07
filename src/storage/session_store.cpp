#include "storage/session_store.hpp"
#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

bool SessionStore::save(const std::string& filepath, const std::vector<TelemetryEvent>& events) {
    try {
        std::ofstream file(filepath);
        if (!file.is_open()) {
            spdlog::error("Failed to open file for writing: {}", filepath);
            return false;
        }
        
        nlohmann::json j = events;
        file << j.dump(4);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Exception saving session: {}", e.what());
        return false;
    }
}

std::vector<TelemetryEvent> SessionStore::load(const std::string& filepath) {
    std::vector<TelemetryEvent> events;
    try {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            spdlog::error("Failed to open file for reading: {}", filepath);
            return events;
        }
        
        nlohmann::json j;
        file >> j;
        if (j.is_array()) {
            events = j.get<std::vector<TelemetryEvent>>();
        }
    } catch (const std::exception& e) {
        spdlog::error("Exception loading session: {}", e.what());
    }
    return events;
}
