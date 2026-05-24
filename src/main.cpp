#include "ou_sampler.hpp"
#include "single_agent.hpp"
#include <iostream>
#include <vector>
#include <numeric>

int main() {
    // Process parameters
    double theta = 2.0;
    double mu = 0.0;
    double sigma_V = 1.0;
    double dt = 0.01;
    int steps = 1000;
    int num_paths = 10000;
    double cost_c = 0.5;
    
    // Latency parameters
    double mu_delta = -4.0; // Expected latency exp(-4) ~ 0.018s
    double sigma_delta = 0.000001; // Effectively zero variance for theoretical baseline test
    
    arctic::OUSampler sampler(theta, mu, sigma_V, dt);
    arctic::SingleAgent agent(mu_delta, sigma_delta, dt);
    
    std::mt19937_64 rng(1337);
    
    // For this demonstration, we assume a static optimal boundary 
    // In Week 2 this was derived theoretically.
    double theoretical_boundary = 1.0; 
    
    double total_pnl = 0.0;
    int total_trades = 0;
    
    for (int p = 0; p < num_paths; ++p) {
        std::vector<double> v_history;
        v_history.reserve(steps);
        
        double v_t = mu; // start at mean
        v_history.push_back(v_t);
        
        bool acted = false;
        
        for (int i = 1; i < steps; ++i) {
            v_t = sampler.step(v_t, rng);
            v_history.push_back(v_t);
            
            if (agent.evaluate_action(v_history, i, theoretical_boundary, rng)) {
                // Agent decides to act based on delayed observation.
                // The actual execution happens at current true state V_t.
                double payoff = v_t - cost_c;
                total_pnl += payoff;
                total_trades++;
                acted = true;
                break;
            }
        }
    }
    
    double avg_pnl = total_trades > 0 ? (total_pnl / num_paths) : 0.0;
    
    std::cout << "ARCTIC Simulation: Single Agent Baseline" << std::endl;
    std::cout << "Paths run: " << num_paths << std::endl;
    std::cout << "Trades executed: " << total_trades << std::endl;
    std::cout << "Average PnL per path: " << avg_pnl << std::endl;
    
    return 0;
}
