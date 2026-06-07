#include "instrumentation/device_monitor.hpp"
#include <iostream>
#include <cmath>
#include <random>

#ifdef _WIN32
#include <windows.h>

// NVML Structure layouts
struct nvmlUtilization_t {
    unsigned int gpu;
    unsigned int memory;
};

struct nvmlMemory_t {
    unsigned long long total;
    unsigned long long free;
    unsigned long long used;
};

// NVML Function Pointers
typedef int (*nvmlInit_t)();
typedef int (*nvmlShutdown_t)();
typedef int (*nvmlDeviceGetHandleByIndex_t)(unsigned int, void**);
typedef int (*nvmlDeviceGetName_t)(void*, char*, unsigned int);
typedef int (*nvmlDeviceGetUtilizationRates_t)(void*, nvmlUtilization_t*);
typedef int (*nvmlDeviceGetTemperature_t)(void*, int, unsigned int*);
typedef int (*nvmlDeviceGetMemoryInfo_t)(void*, nvmlMemory_t*);

static nvmlInit_t nvmlInit_fn = nullptr;
static nvmlShutdown_t nvmlShutdown_fn = nullptr;
static nvmlDeviceGetHandleByIndex_t nvmlDeviceGetHandleByIndex_fn = nullptr;
static nvmlDeviceGetName_t nvmlDeviceGetName_fn = nullptr;
static nvmlDeviceGetUtilizationRates_t nvmlDeviceGetUtilizationRates_fn = nullptr;
static nvmlDeviceGetTemperature_t nvmlDeviceGetTemperature_fn = nullptr;
static nvmlDeviceGetMemoryInfo_t nvmlDeviceGetMemoryInfo_fn = nullptr;

static FILETIME prev_idle_time;
static FILETIME prev_kernel_time;
static FILETIME prev_user_time;
static bool first_cpu_call = true;

double get_cpu_usage_win() {
    FILETIME idle_time, kernel_time, user_time;
    if (!GetSystemTimes(&idle_time, &kernel_time, &user_time)) return 0.0;
    
    if (first_cpu_call) {
        prev_idle_time = idle_time;
        prev_kernel_time = kernel_time;
        prev_user_time = user_time;
        first_cpu_call = false;
        return 0.0;
    }
    
    auto ft_to_uint64 = [](const FILETIME& ft) {
        return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    };
    
    uint64_t idle = ft_to_uint64(idle_time) - ft_to_uint64(prev_idle_time);
    uint64_t kernel = ft_to_uint64(kernel_time) - ft_to_uint64(prev_kernel_time);
    uint64_t user = ft_to_uint64(user_time) - ft_to_uint64(prev_user_time);
    
    prev_idle_time = idle_time;
    prev_kernel_time = kernel_time;
    prev_user_time = user_time;
    
    uint64_t total = kernel + user;
    if (total == 0) return 0.0;
    return (total - idle) * 100.0 / total;
}
#endif

// Null Device Monitor Implementation (CPU stats and mock GPU)
NullDeviceMonitor::NullDeviceMonitor() {}

SystemStats NullDeviceMonitor::get_stats() {
    SystemStats stats;
    stats.gpu_available = false;
    stats.gpu_name = "N/A";
    
#ifdef _WIN32
    // Windows CPU/RAM retrieval
    MEMORYSTATUSEX mem_status;
    mem_status.dwLength = sizeof(mem_status);
    if (GlobalMemoryStatusEx(&mem_status)) {
        stats.ram_total_gb = mem_status.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
        stats.ram_used_gb = (mem_status.ullTotalPhys - mem_status.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
    }
    stats.cpu_usage = get_cpu_usage_win();
#else
    // Linux/other mock defaults
    stats.ram_total_gb = 16.0;
    stats.ram_used_gb = 6.4;
    stats.cpu_usage = 12.5;
#endif

    // Update simulated GPU rates
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<double> dis(-2.0, 2.0);
    double new_util = mock_gpu_util_ + dis(gen);
    mock_gpu_util_ = (new_util < 10.0) ? 10.0 : ((new_util > 95.0) ? 95.0 : new_util);
    double new_temp = mock_gpu_temp_ + dis(gen) * 0.5;
    mock_gpu_temp_ = (new_temp < 45.0) ? 45.0 : ((new_temp > 85.0) ? 85.0 : new_temp);

    stats.gpu_utilization = mock_gpu_util_;
    stats.gpu_temp = mock_gpu_temp_;
    stats.vram_total_gb = 8.0;
    stats.vram_used_gb = 4.2;
    return stats;
}

#ifdef _WIN32
// Real NVML dynamic monitor
NVMLDeviceMonitor::NVMLDeviceMonitor() {
    initialized_ = load_nvml();
}

NVMLDeviceMonitor::~NVMLDeviceMonitor() {
    close_nvml();
}

bool NVMLDeviceMonitor::load_nvml() {
    // Try to load nvml.dll from system folders
    nvml_lib_ = LoadLibraryA("nvml.dll");
    if (!nvml_lib_) {
        // Try alternate locations e.g. NVIDIA Corporation default
        nvml_lib_ = LoadLibraryA("C:\\Windows\\System32\\nvml.dll");
    }
    
    if (!nvml_lib_) return false;
    
    // Bind functions
    nvmlInit_fn = (nvmlInit_t)GetProcAddress((HMODULE)nvml_lib_, "nvmlInit");
    nvmlShutdown_fn = (nvmlShutdown_t)GetProcAddress((HMODULE)nvml_lib_, "nvmlShutdown");
    nvmlDeviceGetHandleByIndex_fn = (nvmlDeviceGetHandleByIndex_t)GetProcAddress((HMODULE)nvml_lib_, "nvmlDeviceGetHandleByIndex");
    nvmlDeviceGetName_fn = (nvmlDeviceGetName_t)GetProcAddress((HMODULE)nvml_lib_, "nvmlDeviceGetName");
    nvmlDeviceGetUtilizationRates_fn = (nvmlDeviceGetUtilizationRates_t)GetProcAddress((HMODULE)nvml_lib_, "nvmlDeviceGetUtilizationRates");
    nvmlDeviceGetTemperature_fn = (nvmlDeviceGetTemperature_t)GetProcAddress((HMODULE)nvml_lib_, "nvmlDeviceGetTemperature");
    nvmlDeviceGetMemoryInfo_fn = (nvmlDeviceGetMemoryInfo_t)GetProcAddress((HMODULE)nvml_lib_, "nvmlDeviceGetMemoryInfo");
    
    if (!nvmlInit_fn || !nvmlShutdown_fn || !nvmlDeviceGetHandleByIndex_fn || 
        !nvmlDeviceGetName_fn || !nvmlDeviceGetUtilizationRates_fn || 
        !nvmlDeviceGetTemperature_fn || !nvmlDeviceGetMemoryInfo_fn) {
        close_nvml();
        return false;
    }
    
    // Initialize NVML
    if (nvmlInit_fn() != 0) {
        close_nvml();
        return false;
    }
    
    // Get first device handle
    if (nvmlDeviceGetHandleByIndex_fn(0, &device_handle_) != 0) {
        nvmlShutdown_fn();
        close_nvml();
        return false;
    }
    
    return true;
}

void NVMLDeviceMonitor::close_nvml() {
    if (nvml_lib_) {
        if (initialized_ && nvmlShutdown_fn) {
            nvmlShutdown_fn();
        }
        FreeLibrary((HMODULE)nvml_lib_);
        nvml_lib_ = nullptr;
    }
    initialized_ = false;
    device_handle_ = nullptr;
}

SystemStats NVMLDeviceMonitor::get_stats() {
    SystemStats stats;
    stats.gpu_available = true;
    
    // System RAM & CPU
    MEMORYSTATUSEX mem_status;
    mem_status.dwLength = sizeof(mem_status);
    if (GlobalMemoryStatusEx(&mem_status)) {
        stats.ram_total_gb = mem_status.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
        stats.ram_used_gb = (mem_status.ullTotalPhys - mem_status.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
    }
    stats.cpu_usage = get_cpu_usage_win();

    // Query NVML
    if (initialized_ && device_handle_) {
        char name_buf[64] = "NVIDIA GPU";
        if (nvmlDeviceGetName_fn(device_handle_, name_buf, sizeof(name_buf)) == 0) {
            stats.gpu_name = name_buf;
        } else {
            stats.gpu_name = "NVIDIA GPU";
        }
        
        nvmlUtilization_t utils;
        if (nvmlDeviceGetUtilizationRates_fn(device_handle_, &utils) == 0) {
            stats.gpu_utilization = utils.gpu;
        }
        
        unsigned int temp = 0;
        if (nvmlDeviceGetTemperature_fn(device_handle_, 0 /* NVML_TEMPERATURE_GPU */, &temp) == 0) {
            stats.gpu_temp = temp;
        }
        
        nvmlMemory_t mem;
        if (nvmlDeviceGetMemoryInfo_fn(device_handle_, &mem) == 0) {
            stats.vram_total_gb = mem.total / (1024.0 * 1024.0 * 1024.0);
            stats.vram_used_gb = mem.used / (1024.0 * 1024.0 * 1024.0);
        }
    } else {
        // Safe fallback
        stats.gpu_available = false;
        stats.gpu_name = "N/A";
    }
    
    return stats;
}
#endif

std::unique_ptr<DeviceMonitor> DeviceMonitor::create() {
#ifdef _WIN32
    auto monitor = std::make_unique<NVMLDeviceMonitor>();
    if (monitor->is_initialized()) {
        return monitor;
    }
#endif
    return std::make_unique<NullDeviceMonitor>();
}
