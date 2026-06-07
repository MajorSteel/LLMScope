#include "telemetry/attention_sampler.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>

AttentionSampler::AttentionSampler(Config config) : config_(config) {}

AttentionData AttentionSampler::compress(const AttentionData& raw_data) const {
    if (config_.type == CompressionType::None || raw_data.token_count <= 8) {
        return raw_data;
    }

    switch (config_.type) {
        case CompressionType::TopK:
            return apply_top_k(raw_data);
        case CompressionType::SlidingWindow:
            return apply_sliding_window(raw_data);
        case CompressionType::Downsample:
            return apply_downsample(raw_data);
        default:
            return raw_data;
    }
}

AttentionData AttentionSampler::apply_top_k(const AttentionData& raw) const {
    AttentionData result = raw;
    int k = std::min(config_.top_k, raw.token_count);
    
    for (auto& head : result.matrices) {
        for (auto& row : head) {
            // Find threshold of top k elements
            std::vector<float> sorted_row = row;
            std::sort(sorted_row.rbegin(), sorted_row.rend());
            float threshold = sorted_row[k - 1];
            
            // Zero out elements below threshold
            for (auto& val : row) {
                if (val < threshold) {
                    val = 0.0f;
                }
            }
        }
    }
    return result;
}

AttentionData AttentionSampler::apply_sliding_window(const AttentionData& raw) const {
    AttentionData result = raw;
    int w = config_.window_size;
    
    for (auto& head : result.matrices) {
        for (int q = 0; q < raw.token_count; ++q) {
            auto& row = head[q];
            for (int k = 0; k < raw.token_count; ++k) {
                if (std::abs(q - k) > w) {
                    row[k] = 0.0f;
                }
            }
        }
    }
    return result;
}

AttentionData AttentionSampler::apply_downsample(const AttentionData& raw) const {
    int res = std::min(config_.downsample_res, raw.token_count);
    if (res <= 0) return raw;

    AttentionData result;
    result.layer_name = raw.layer_name;
    result.num_heads = raw.num_heads;
    result.token_count = res;
    
    // Select a subset of tokens for labels
    for (int i = 0; i < res; ++i) {
        int idx = (i * raw.token_count) / res;
        result.tokens.push_back(raw.tokens[idx]);
    }

    result.matrices.resize(raw.num_heads, std::vector<std::vector<float>>(res, std::vector<float>(res, 0.0f)));

    float bin_size = static_cast<float>(raw.token_count) / res;

    for (int h = 0; h < raw.num_heads; ++h) {
        const auto& raw_head = raw.matrices[h];
        auto& res_head = result.matrices[h];
        
        for (int r = 0; r < res; ++r) {
            int r_start = static_cast<int>(r * bin_size);
            int r_end = std::min(static_cast<int>((r + 1) * bin_size), raw.token_count);
            r_end = std::max(r_end, r_start + 1);

            for (int c = 0; c < res; ++c) {
                int c_start = static_cast<int>(c * bin_size);
                int c_end = std::min(static_cast<int>((c + 1) * bin_size), raw.token_count);
                c_end = std::max(c_end, c_start + 1);

                // Average values in block
                float sum = 0.0f;
                int count = 0;
                for (int ri = r_start; ri < r_end; ++ri) {
                    for (int ci = c_start; ci < c_end; ++ci) {
                        sum += raw_head[ri][ci];
                        count++;
                    }
                }
                res_head[r][c] = (count > 0) ? (sum / count) : 0.0f;
            }
        }
    }

    return result;
}
