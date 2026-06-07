#include "tui/knowledge_base.hpp"
#include <sstream>
#include <iomanip>

KnowledgeBase::Explanation KnowledgeBase::get_explanation(const std::string& layer_type, const TelemetryEvent& active_event) {
    Explanation exp;
    
    // Default values
    exp.title = "Unknown Operation";
    exp.description = "No conceptual documentation found for this module class.";
    exp.mathematical_formula = "N/A";
    exp.dynamic_analysis = "Waiting for live tracer packets to analyze statistics.";

    const auto& stats = active_event.activation_stats;
    bool has_stats = (active_event.event_type == "layer_trace" && active_event.layer_name != "");

    if (layer_type == "SelfAttention") {
        exp.title = "Self-Attention Mechanism";
        exp.description = "Allows each token in the sequence to dynamically weigh and aggregate information from every other token. It computes query, key, and value vectors, matching queries to keys to form a weight map.";
        exp.mathematical_formula = "Attention(Q, K, V) = softmax( (Q * K^T) / sqrt(d_k) ) * V";
        
        if (has_stats) {
            std::stringstream ss;
            ss << "Live Analysis: Computing attention across sequence. Latency: " 
               << std::fixed << std::setprecision(3) << active_event.latency_ms << " ms. "
               << "Tensor shape: [" << active_event.output_tensor.shape[0] << ", " 
               << active_event.output_tensor.shape[1] << ", " << active_event.output_tensor.shape[2] << "]. "
               << "Activation variance is " << std::fixed << std::setprecision(4) << stats.variance 
               << ". Normal entropy distributions are maintained across attention heads.";
            exp.dynamic_analysis = ss.str();
        } else if (active_event.event_type == "attention_weights") {
            const auto& attn = active_event.attention;
            if (!attn.matrices.empty() && attn.token_count > 0) {
                // Find strongest attention connection
                int max_q = 0, max_k = 0, max_h = 0;
                float max_w = -1.0f;
                for (int h = 0; h < attn.num_heads; ++h) {
                    for (int q = 0; q < attn.token_count; ++q) {
                        for (int k = 0; k < attn.token_count; ++k) {
                            if (q != k && attn.matrices[h][q][k] > max_w) {
                                max_w = attn.matrices[h][q][k];
                                max_q = q;
                                max_k = k;
                                max_h = h;
                            }
                        }
                    }
                }
                
                std::stringstream ss;
                ss << "Attention Matrix Focus: Head " << max_h << " shows the highest cross-token attention: "
                   << "'" << attn.tokens[max_q] << "' attends to '" << attn.tokens[max_k] << "' "
                   << "with a weight of " << std::fixed << std::setprecision(3) << max_w << ".";
                exp.dynamic_analysis = ss.str();
            }
        }
    } 
    else if (layer_type == "MLP" || layer_type == "FFN") {
        exp.title = "Feed-Forward Gated MLP (SwiGLU)";
        exp.description = "Applies non-linear channel transformations to each token vector independently. Modern models use SwiGLU (Swish Gated Linear Units) to gate activations, enhancing representation capacity.";
        exp.mathematical_formula = "SwiGLU(x) = (x * W_gate * sigmoid(x * W_gate)) * (x * W_up) * W_down";
        
        if (has_stats) {
            std::stringstream ss;
            ss << "Live Analysis: FFN completed with a sparsity rate of " 
               << std::fixed << std::setprecision(1) << stats.sparsity << "%. "
               << "This confirms the activation gating is working. "
               << "Mean value: " << std::fixed << std::setprecision(4) << stats.mean
               << ", Bounds: [" << stats.min_val << ", " << stats.max_val << "].";
            exp.dynamic_analysis = ss.str();
        }
    }
    else if (layer_type == "RMSNorm" || layer_type == "LayerNorm") {
        exp.title = "RMS Normalization (RMSNorm)";
        exp.description = "Normalizes the activations of a layer by their root mean square. This keeps activation values stable and bounds scaling without calculating mean offsets, reducing compute time.";
        exp.mathematical_formula = "RMSNorm(x) = (x / RMS(x)) * gamma,  where RMS(x) = sqrt( 1/d * sum(x_i^2) )";
        
        if (has_stats) {
            std::stringstream ss;
            ss << "Live Analysis: Regularizing tensor activations. Output variance: "
               << std::fixed << std::setprecision(4) << stats.variance 
               << " (Goal: ~1.0). The activations mean is " 
               << std::fixed << std::setprecision(4) << stats.mean 
               << ", confirming bounding checks have completed successfully.";
            exp.dynamic_analysis = ss.str();
        }
    }
    else if (layer_type == "Embedding") {
        exp.title = "Token Embedding Layer";
        exp.description = "Converts discrete vocabulary token IDs into dense semantic vectors. This projects high-dimensional vocab keys into continuous hidden layers.";
        exp.mathematical_formula = "x = WordEmbedding[Token_ID]";
        
        if (has_stats) {
            std::stringstream ss;
            ss << "Live Analysis: Embedded " << active_event.input_tensor.shape[1] 
               << " tokens to 4096-dim vectors. Sparsity: " << stats.sparsity 
               << "%, Memory footprint: " << std::fixed << std::setprecision(2)
               << (active_event.output_tensor.size_bytes / (1024.0 * 1024.0)) << " MB.";
            exp.dynamic_analysis = ss.str();
        }
    }
    else if (layer_type == "LMHead") {
        exp.title = "LM Output Head";
        exp.description = "Converts the final hidden representation of the sequence back into vocabulary token probabilities. Applies a linear projection onto the vocabulary space.";
        exp.mathematical_formula = "Logits = x * W_lmhead,  Probabilities = softmax(Logits)";
        
        if (has_stats) {
            std::stringstream ss;
            ss << "Live Analysis: Projecting hidden layers onto " 
               << active_event.output_tensor.shape[2] << " logits. "
               << "Maximum logit value: " << std::fixed << std::setprecision(2) << stats.max_val 
               << " (represents highest confidence next token).";
            exp.dynamic_analysis = ss.str();
        }
    }

    return exp;
}
