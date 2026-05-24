#include "single_agent.hpp"
#include <stdexcept>

namespace arctic {

SingleAgent::SingleAgent(double mu_delta, double sigma_delta, double dt)
    : mu_delta_(mu_delta), sigma_delta_(sigma_delta), dt_(dt),
      lognormal_dist_(mu_delta, sigma_delta) {
    
    if (dt <= 0.0) {
        throw std::invalid_argument("dt must be strictly positive.");
    }
    if (sigma_delta < 0.0) {
        throw std::invalid_argument("sigma_delta must be non-negative.");
    }
}

} // namespace arctic
