#pragma once
#include <iostream>
#include <string>
#include <sstream>
#include <utility>

namespace spdlog {
    namespace level {
        enum level_enum {
            trace = 0,
            debug = 1,
            info = 2,
            warn = 3,
            err = 4,
            critical = 5,
            off = 6
        };
    }
    
    inline void set_level(level::level_enum /*level*/) {}

    // Base case for format recursion
    inline std::string format_str(const std::string& fmt) {
        return fmt;
    }

    // Recursive formatter replacing '{}' with arguments
    template<typename T, typename... Args>
    std::string format_str(const std::string& fmt, T&& first, Args&&... rest) {
        size_t pos = fmt.find("{}");
        if (pos == std::string::npos) {
            return fmt;
        }
        std::stringstream ss;
        ss << fmt.substr(0, pos) << first;
        return ss.str() + format_str(fmt.substr(pos + 2), std::forward<Args>(rest)...);
    }

    template<typename... Args>
    void info(const std::string& fmt, Args&&... args) {
        std::cout << "[INFO] " << format_str(fmt, std::forward<Args>(args)...) << std::endl;
    }

    template<typename... Args>
    void warn(const std::string& fmt, Args&&... args) {
        std::cout << "[WARN] " << format_str(fmt, std::forward<Args>(args)...) << std::endl;
    }

    template<typename... Args>
    void error(const std::string& fmt, Args&&... args) {
        std::cerr << "[ERROR] " << format_str(fmt, std::forward<Args>(args)...) << std::endl;
    }
}
