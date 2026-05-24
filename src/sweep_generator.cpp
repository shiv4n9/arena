#include "ou_sampler.hpp"
#include "single_agent.hpp"
#include "race_resolver.hpp"
#include <iostream>
#include <vector>
#include <iomanip>
#include <cmath>
#include <fstream>
#include <string>

struct SweepResult {
    double sigma_A;
    double sigma_B;
    double nash_b_A;
    double win_rate_A;
    double win_rate_B;
    double avg_pnl_A;
    double avg_pnl_Solo;
    double competitive_cost;
};

SweepResult run_sweep_step(double sigma_A, double sigma_B) {
    double theta = 2.0;
    double mu = 0.0;
    double sigma_V = 1.0;
    double dt = 0.01;
    int steps = 1000;
    int num_paths = 50000;
    double cost_c = 0.5;
    double mu_delta = -4.0;
    
    arctic::OUSampler sampler(theta, mu, sigma_V, dt);
    arctic::SingleAgent agent_solo(mu_delta, sigma_A, dt);
    arctic::SingleAgent agent_a(mu_delta, sigma_A, dt);
    arctic::SingleAgent agent_b(mu_delta, sigma_B, dt);
    arctic::RaceResolver resolver;
    
    std::mt19937_64 rng_ou(42);
    std::mt19937_64 rng_solo(100);
    std::mt19937_64 rng_a(100);
    std::mt19937_64 rng_b(200);
    
    double b_single = 1.0; 
    
    auto compute_nash_boundary = [](double b_base, double sig_self, double sig_competitor, double sig_v) {
        double k_compression = 0.5; 
        double variance_gap = sig_self - sig_competitor;
        if (variance_gap > 0) {
            return b_base - k_compression * sig_v * variance_gap;
        }
        return b_base;
    };
    
    double b_solo = b_single;
    double b_a = compute_nash_boundary(b_single, sigma_A, sigma_B, sigma_V); 
    double b_b = compute_nash_boundary(b_single, sigma_B, sigma_A, sigma_V); 
    
    double pnl_solo = 0.0;
    double pnl_a = 0.0;
    double pnl_b = 0.0;
    
    int trades_solo = 0;
    int trades_a = 0;
    int trades_b = 0;
    
    for (int p = 0; p < num_paths; ++p) {
        std::vector<double> v_history;
        v_history.reserve(steps);
        double v_t = mu;
        v_history.push_back(v_t);
        bool solo_acted = false;
        bool game_resolved = false;
        
        for (int i = 1; i < steps; ++i) {
            v_t = sampler.step(v_t, rng_ou);
            v_history.push_back(v_t);
            
            if (!solo_acted) {
                auto decision_solo = agent_solo.evaluate_action(v_history, i, b_solo, rng_solo);
                if (decision_solo.wants_to_act) {
                    pnl_solo += (v_t - cost_c);
                    trades_solo++;
                    solo_acted = true;
                }
            }
            
            if (!game_resolved) {
                auto dec_a = agent_a.evaluate_action(v_history, i, b_a, rng_a);
                auto dec_b = agent_b.evaluate_action(v_history, i, b_b, rng_b);
                
                if (dec_a.wants_to_act && dec_b.wants_to_act) {
                    auto result = resolver.resolve_race(dec_a.latency_drawn, dec_b.latency_drawn);
                    if (result.agent_a_won) {
                        pnl_a += (v_t - cost_c);
                        trades_a++;
                    } else if (result.agent_b_won) {
                        pnl_b += (v_t - cost_c);
                        trades_b++;
                    }
                    game_resolved = true;
                } else if (dec_a.wants_to_act) {
                    pnl_a += (v_t - cost_c);
                    trades_a++;
                    game_resolved = true;
                } else if (dec_b.wants_to_act) {
                    pnl_b += (v_t - cost_c);
                    trades_b++;
                    game_resolved = true;
                }
            }
        }
    }
    
    double win_rate_a = static_cast<double>(trades_a) / (trades_a + trades_b);
    double win_rate_b = static_cast<double>(trades_b) / (trades_a + trades_b);
    double avg_pnl_solo = trades_solo ? pnl_solo / num_paths : 0.0;
    double avg_pnl_a = trades_a ? pnl_a / num_paths : 0.0;
    double competitive_cost = (sigma_A >= sigma_B) ? (avg_pnl_solo - avg_pnl_a) : 0.0;
    
    return {sigma_A, sigma_B, b_a, win_rate_a, win_rate_b, avg_pnl_a, avg_pnl_solo, competitive_cost};
}

int main() {
    std::ofstream out("data/sweep_results.csv");
    out << "Sigma_A,Sigma_B,Nash_B_A,Win_Rate_A,Win_Rate_B,Avg_PnL_A,Avg_PnL_Solo,Competitive_Cost\n";
    
    double sigma_B = 0.1;
    
    std::cout << "Running Variance Gap Parameter Sweep...\n";
    for (double sigma_A = 0.1; sigma_A <= 1.51; sigma_A += 0.1) {
        std::cout << "Evaluating Sigma_A = " << sigma_A << "..." << std::flush;
        auto result = run_sweep_step(sigma_A, sigma_B);
        
        out << result.sigma_A << ","
            << result.sigma_B << ","
            << result.nash_b_A << ","
            << result.win_rate_A << ","
            << result.win_rate_B << ","
            << result.avg_pnl_A << ","
            << result.avg_pnl_Solo << ","
            << result.competitive_cost << "\n";
            
        std::cout << " Done. (Cost = " << result.competitive_cost << ")\n";
    }
    
    out.close();
    std::cout << "Sweep complete! Data written to data/sweep_results.csv\n";
    return 0;
}
