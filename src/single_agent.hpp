#pragma once

#include <vector>
#include <random>

namespace arctic {

/**
 * Single Agent Simulator for optimal stopping under latency uncertainty.
 * The agent observes Y(t) = V(t - \delta) where \delta ~ LogNormal(mu_delta, sigma_delta^2).
 */
struct AgentDecision {
    bool wants_to_act;
    double latency_drawn;
};

class SingleAgent {
public:
    SingleAgent(double mu_delta, double sigma_delta, double dt);

    template<typename RNG>
    AgentDecision evaluate_action(const std::vector<double>& v_history, int current_step, double stopping_boundary, RNG& rng) {
        double delta = lognormal_dist_(rng);
        int lag_steps = static_cast<int>(delta / dt_);
        int observation_index = current_step - lag_steps;
        
        if (observation_index < 0) {
            observation_index = 0;
        }
        
        double observed_y = v_history[observation_index];
        bool acts = observed_y >= stopping_boundary;
        return {acts, delta};
    }

private:
    double mu_delta_;
    double sigma_delta_;
    double dt_;
    std::lognormal_distribution<double> lognormal_dist_;
};

} // namespace arctic
