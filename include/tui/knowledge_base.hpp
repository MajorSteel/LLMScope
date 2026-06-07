#pragma once
#include <string>
#include "telemetry/types.hpp"

class KnowledgeBase {
public:
    struct Explanation {
        std::string title;
        std::string description;
        std::string mathematical_formula;
        std::string dynamic_analysis;
    };

    // Retrieve conceptual and math explanations for a component,
    // optionally evaluating the active event's tensor statistics.
    static Explanation get_explanation(const std::string& layer_type, const TelemetryEvent& active_event = {});
};
