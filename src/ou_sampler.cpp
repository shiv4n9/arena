#include "ou_sampler.hpp"
#include <cmath>
#include <stdexcept>

namespace arctic {

OUSampler::OUSampler(double theta, double mu, double sigma_V, double dt)
    : theta_(theta), mu_(mu), sigma_V_(sigma_V), dt_(dt), norm_dist_(0.0, 1.0) {
    
    if (theta <= 0.0) {
        throw std::invalid_argument("theta must be strictly positive for mean reversion.");
    }
    if (sigma_V <= 0.0) {
        throw std::invalid_argument("sigma_V must be strictly positive.");
    }
    if (dt <= 0.0) {
        throw std::invalid_argument("dt must be strictly positive.");
    }

    mean_term_1_ = mu_;
    mean_term_2_ = std::exp(-theta_ * dt_);
    
    double var_term = (1.0 - std::exp(-2.0 * theta_ * dt_)) / (2.0 * theta_);
    std_dev_ = sigma_V_ * std::sqrt(var_term);
}

double OUSampler::get_stationary_mean() const {
    return mu_;
}

double OUSampler::get_stationary_variance() const {
    return (sigma_V_ * sigma_V_) / (2.0 * theta_);
}

} // namespace arctic
