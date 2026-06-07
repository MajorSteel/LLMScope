#pragma once
#include <vector>
#include <string>
#include "telemetry/types.hpp"

enum class CompressionType {
    None,
    TopK,
    SlidingWindow,
    Downsample
};

class AttentionSampler {
public:
    struct Config {
        CompressionType type = CompressionType::None;
        int top_k = 8;
        int window_size = 16;
        int downsample_res = 32; // e.g. downsample to 32x32
    };

    explicit AttentionSampler(Config config);

    // Apply compression to an attention data structure
    AttentionData compress(const AttentionData& raw_data) const;

private:
    AttentionData apply_top_k(const AttentionData& raw) const;
    AttentionData apply_sliding_window(const AttentionData& raw) const;
    AttentionData apply_downsample(const AttentionData& raw) const;

    Config config_;
};
