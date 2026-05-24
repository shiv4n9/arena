#pragma once

#include <vector>
#include <random>

namespace arctic {

/**
 * Single Agent Simulator for optimal stopping under latency uncertainty.
 * The agent observes Y(t) = V(t - \delta) where \delta ~ LogNormal(mu_delta, sigma_delta^2).
 */
class SingleAgent {
public:
    SingleAgent(double mu_delta, double sigma_delta, double dt);

    /**
     * Determine if the agent should act at the current time step.
     * @param v_history The exact history of the OU process.
     * @param current_step The current simulation step index.
     * @param stopping_boundary The theoretical optimal threshold to act.
     * @param rng The random number generator for drawing the latency delta.
     * @return true if the agent stops (acts), false otherwise.
     */
    template<typename RNG>
    bool evaluate_action(const std::vector<double>& v_history, int current_step, double stopping_boundary, RNG& rng) {
        // Draw latency
        double delta = lognormal_dist_(rng);
        
        // Convert latency to discrete steps
        int lag_steps = static_cast<int>(delta / dt_);
        int observation_index = current_step - lag_steps;
        
        // Handle boundary conditions (if latency looks before the start of simulation, assume V(0))
        if (observation_index < 0) {
            observation_index = 0;
        }
        
        double observed_y = v_history[observation_index];
        
        // The agent acts if the observed signal exceeds the theoretical boundary
        return observed_y >= stopping_boundary;
    }

private:
    double mu_delta_;
    double sigma_delta_;
    double dt_;
    std::lognormal_distribution<double> lognormal_dist_;
};

} // namespace arctic
