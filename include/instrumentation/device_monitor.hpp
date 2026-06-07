#pragma once
#include <string>
#include <memory>

struct SystemStats {
    double cpu_usage = 0.0;       // %
    double ram_used_gb = 0.0;
    double ram_total_gb = 0.0;
    
    bool gpu_available = false;
    std::string gpu_name = "N/A";
    double gpu_utilization = 0.0; // %
    double gpu_temp = 0.0;        // Celsius
    double vram_used_gb = 0.0;
    double vram_total_gb = 0.0;
};

class DeviceMonitor {
public:
    virtual ~DeviceMonitor() = default;
    virtual SystemStats get_stats() = 0;
    
    // Static factory to create appropriate monitor
    static std::unique_ptr<DeviceMonitor> create();
};

class NullDeviceMonitor : public DeviceMonitor {
public:
    NullDeviceMonitor();
    SystemStats get_stats() override;
    
private:
    double mock_gpu_util_ = 25.0;
    double mock_gpu_temp_ = 58.0;
};

#ifdef _WIN32
class NVMLDeviceMonitor : public DeviceMonitor {
public:
    NVMLDeviceMonitor();
    ~NVMLDeviceMonitor() override;
    SystemStats get_stats() override;
    bool is_initialized() const { return initialized_; }

private:
    bool load_nvml();
    void close_nvml();

    void* nvml_lib_ = nullptr;
    bool initialized_ = false;
    void* device_handle_ = nullptr;
};
#endif
