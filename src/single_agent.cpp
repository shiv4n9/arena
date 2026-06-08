#include "single_agent.hpp"
#include "math_utils.hpp"
#include <cmath>
#include <stdexcept>

namespace arctic {

SingleAgent::SingleAgent(double mean_latency, double std_latency, double dt)
    : mean_latency_(mean_latency), std_latency_(std_latency), dt_(dt),
      lognormal_dist_(latency_log_mu(mean_latency, std_latency),
                      std::sqrt(latency_log_variance(mean_latency, std_latency))) {

    if (dt <= 0.0) {
        throw std::invalid_argument("dt must be strictly positive.");
    }
    if (mean_latency <= 0.0) {
        throw std::invalid_argument("mean_latency must be strictly positive.");
    }
    if (std_latency < 0.0) {
        throw std::invalid_argument("std_latency must be non-negative.");
    }
}

} // namespace arctic
