#pragma once
#include <string>
#include <vector>
#include "telemetry/types.hpp"

class SessionStore {
public:
    // Save a list of telemetry events to a file
    static bool save(const std::string& filepath, const std::vector<TelemetryEvent>& events);

    // Load a list of telemetry events from a file
    static std::vector<TelemetryEvent> load(const std::string& filepath);
};
