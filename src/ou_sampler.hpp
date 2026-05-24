#pragma once

#include <random>

namespace arctic {

/**
 * Exact sampler for the Ornstein-Uhlenbeck process:
 * dV_t = theta * (mu - V_t) dt + sigma_V dW_t
 * 
 * We use the exact analytical solution to avoid Euler-Maruyama discretization bias.
 */
class OUSampler {
public:
    OUSampler(double theta, double mu, double sigma_V, double dt);

    // Get the stationary distribution mean
    double get_stationary_mean() const;

    // Get the stationary distribution variance
    double get_stationary_variance() const;

    // Advance the process by one step dt and return the new value
    template<typename RNG>
    double step(double v_t, RNG& rng) {
        return mean_term_1_ + (v_t - mu_) * mean_term_2_ + std_dev_ * norm_dist_(rng);
    }

private:
    double theta_;
    double mu_;
    double sigma_V_;
    double dt_;

    // Precomputed constants for the exact step
    double mean_term_1_; // mu
    double mean_term_2_; // exp(-theta * dt)
    double std_dev_;     // sigma_V * sqrt((1 - exp(-2 * theta * dt)) / (2 * theta))

    std::normal_distribution<double> norm_dist_;
};

} // namespace arctic
