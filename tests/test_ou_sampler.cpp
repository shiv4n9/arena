#include "ou_sampler.hpp"
#include <iostream>
#include <cmath>
#include <cassert>
#include <vector>

void test_stationary_moments() {
    double theta = 2.0;
    double mu = 100.0;
    double sigma_V = 5.0;
    double dt = 0.01;
    int num_steps = 100000;
    
    arctic::OUSampler sampler(theta, mu, sigma_V, dt);
    std::mt19937_64 rng(42); // deterministic seed for reproducibility
    
    // Burn-in period to reach stationarity
    double v_t = mu;
    for (int i = 0; i < 10000; ++i) {
        v_t = sampler.step(v_t, rng);
    }
    
    // Collect samples
    double sum = 0.0;
    double sum_sq = 0.0;
    
    for (int i = 0; i < num_steps; ++i) {
        v_t = sampler.step(v_t, rng);
        sum += v_t;
        sum_sq += v_t * v_t;
    }
    
    double empirical_mean = sum / num_steps;
    double empirical_var = (sum_sq / num_steps) - (empirical_mean * empirical_mean);
    
    double theoretical_mean = sampler.get_stationary_mean();
    double theoretical_var = sampler.get_stationary_variance();
    
    std::cout << "Empirical Mean: " << empirical_mean << " | Theoretical: " << theoretical_mean << std::endl;
    std::cout << "Empirical Var:  " << empirical_var << " | Theoretical: " << theoretical_var << std::endl;
    
    // Strict tolerance check (5% relative error for variance on 100k paths is safe)
    assert(std::abs(empirical_mean - theoretical_mean) < 0.1);
    assert(std::abs(empirical_var - theoretical_var) / theoretical_var < 0.05);
    
    std::cout << "test_stationary_moments passed.\n";
}

int main() {
    try {
        test_stationary_moments();
        std::cout << "All OU Sampler tests passed successfully.\n";
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
