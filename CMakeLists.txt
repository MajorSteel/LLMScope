cmake_minimum_required(VERSION 3.14)
project(LLMScope LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Ensure our compatibility headers are searched first globally
include_directories(BEFORE include)

# Enable Folder support for Visual Studio
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

include(FetchContent)

# nlohmann_json dependency
message(STATUS "Fetching nlohmann_json...")
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.zip
)
FetchContent_MakeAvailable(json)



# FTXUI dependency
message(STATUS "Fetching FTXUI...")
FetchContent_Declare(
    ftxui
    URL https://github.com/ArthurSonzogni/FTXUI/archive/refs/tags/v5.0.0.zip
)
FetchContent_MakeAvailable(ftxui)

# Target executable (Full TUI version - C++20 compliant)
add_executable(llmscope
    src/main.cpp
    src/telemetry/event_bus.cpp
    src/telemetry/aggregator.cpp
    src/telemetry/attention_sampler.cpp
    src/telemetry/otlp_exporter.cpp
    src/plugins/plugin_manager.cpp
    src/anomaly/anomaly_detector.cpp
    src/storage/ring_buffer.cpp
    src/storage/session_store.cpp
    src/replay/replay_manager.cpp
    src/instrumentation/telemetry_server.cpp
    src/instrumentation/mock_tracer.cpp
    src/instrumentation/device_monitor.cpp
    src/web/web_server.cpp
    src/tui/knowledge_base.cpp
    src/tui/panels.cpp
    src/tui/dashboard.cpp
)

# Header include paths
target_include_directories(llmscope PRIVATE include)

# Link dependencies
target_link_libraries(llmscope PRIVATE
    ftxui::screen
    ftxui::dom
    ftxui::component
    nlohmann_json::nlohmann_json
)

# Platform-specific configurations
if(WIN32)
    # Winsock 2 library for networking on Windows
    target_link_libraries(llmscope PRIVATE ws2_32)
    target_compile_definitions(llmscope PRIVATE NOMINMAX _WIN32_WINNT=0x0601)
else()
    target_link_libraries(llmscope PRIVATE pthread)
endif()

# Target executable (Headless Web version - GCC 6.3.0/C++17 compliant)
add_executable(llmscope_web
    src/main.cpp
    src/telemetry/event_bus.cpp
    src/telemetry/aggregator.cpp
    src/telemetry/attention_sampler.cpp
    src/telemetry/otlp_exporter.cpp
    src/plugins/plugin_manager.cpp
    src/anomaly/anomaly_detector.cpp
    src/storage/ring_buffer.cpp
    src/storage/session_store.cpp
    src/replay/replay_manager.cpp
    src/instrumentation/telemetry_server.cpp
    src/instrumentation/mock_tracer.cpp
    src/instrumentation/device_monitor.cpp
    src/web/web_server.cpp
)

target_include_directories(llmscope_web PRIVATE include)
target_compile_definitions(llmscope_web PRIVATE NO_TUI)
target_link_libraries(llmscope_web PRIVATE
    nlohmann_json::nlohmann_json
)

if(WIN32)
    target_link_libraries(llmscope_web PRIVATE ws2_32)
    target_compile_definitions(llmscope_web PRIVATE NOMINMAX _WIN32_WINNT=0x0601)
else()
    target_link_libraries(llmscope_web PRIVATE pthread)
endif()
