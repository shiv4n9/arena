#pragma once

#include <vector>
#include <random>

namespace arctic {

// handles pulling the trigger based on signal and latency assumptions
struct AgentDecision {
    bool wants_to_act;
    double latency_drawn;
};

class SingleAgent {
public:
    // Latency is specified by its real-world MEAN and STANDARD DEVIATION; the
    // constructor converts these to the underlying log-normal (mu, sigma) used
    // for sampling, so the sampler matches the analytic math in math_utils.
    SingleAgent(double mean_latency, double std_latency, double dt);

    // Decide whether to fire. INFORMATION MODEL: send-side latency only — the
    // agent's market data is real-time (it observes V at `current_step`), and the
    // drawn latency `delta` delays ONLY the order's arrival (used downstream for
    // execution timing and race resolution). This is the Budish-Cramton-Shim
    // information structure and removes the spurious double-latency (observe-late
    // AND execute-late) that otherwise makes the analytic decay inconsistent.
    template<typename RNG>
    AgentDecision evaluate_action(const double* v_data, int v_size, int current_step, double stopping_boundary, RNG& rng) {
        double delta = lognormal_dist_(rng);            // order-transmission latency
        int idx = current_step;
        if (idx < 0) idx = 0;
        if (idx >= v_size) idx = v_size - 1;
        double observed_y = v_data[idx];                // real-time observation
        bool acts = observed_y >= stopping_boundary;
        return {acts, delta};
    }

    // vector wrapper just in case
    template<typename RNG>
    AgentDecision evaluate_action(const std::vector<double>& v_history, int current_step, double stopping_boundary, RNG& rng) {
        return evaluate_action(v_history.data(), static_cast<int>(v_history.size()), current_step, stopping_boundary, rng);
    }

private:
    double mean_latency_;
    double std_latency_;
    double dt_;
    std::lognormal_distribution<double> lognormal_dist_;
};

} // namespace arctic
