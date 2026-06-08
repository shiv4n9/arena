#include "ou_sampler.hpp"
#include "single_agent.hpp"
#include "race_resolver.hpp"
#include "math_utils.hpp"
#include "arena_allocator.hpp"
#include "tsc_clock.hpp"
#include <iostream>
#include <vector>
#include <iomanip>
#include <cmath>
#include <fstream>
#include <string>
#include <chrono>
#include <numeric>

struct SweepResult {
    double mean_A;
    double std_A;
    double mean_B;
    double std_B;
    double eq_b_A;
    double eq_b_B;
    double p_win_A;
    double win_rate_A;
    double win_rate_B;
    double avg_pnl_A;
    double avg_pnl_Solo;
    double competitive_cost;
    double sharpe_A;
    double mean_stop_time_A;
    double elapsed_ms;
};

SweepResult run_sweep_step(double mean_A, double std_A, double mean_B, double std_B) {
    double theta = 2.0;
    double mu = 0.0;
    double sigma_V = 1.0;
    double dt = 0.01;
    int steps = 1000;
    int num_paths = 50000;
    // Cost of crossing the spread; must equal the half-spread so the analytic
    // boundary is the zero-EV threshold of the simulated payoff.
    constexpr double half_spread = 0.05;
    double cost_c = half_spread;
    
    arctic::OUSampler sampler(theta, mu, sigma_V, dt);
    arctic::SingleAgent agent_solo(mean_A, std_A, dt);
    arctic::SingleAgent agent_a(mean_A, std_A, dt);
    arctic::SingleAgent agent_b(mean_B, std_B, dt);
    arctic::RaceResolver resolver;
    
    // bugfix: keep seeds separate or it breaks the PnL diff math
    std::mt19937_64 rng_ou(42);
    std::mt19937_64 rng_solo(101);
    std::mt19937_64 rng_a(201);
    std::mt19937_64 rng_b(301);
    
    double b_solo = arctic::compute_solo_boundary(mean_A, std_A, theta, mu, cost_c);
    double b_a = arctic::compute_equilibrium_boundary(mean_A, std_A, mean_B, std_B, theta, mu, cost_c);
    double b_b = arctic::compute_equilibrium_boundary(mean_B, std_B, mean_A, std_A, theta, mu, cost_c);
    double p_win_a = arctic::compute_p_win(mean_A, std_A, mean_B, std_B);
    
    double pnl_solo = 0.0;
    double pnl_a = 0.0;
    double pnl_b = 0.0;
    int trades_solo = 0;
    int trades_a = 0;
    int trades_b = 0;
    
    // pnl tracker for sharpe
    std::vector<double> pnl_per_path_a(num_paths, 0.0);
    
    // track when we pull the trigger
    std::vector<int> stop_times_a;
    stop_times_a.reserve(num_paths);
    
    // arena alloc. keep new/delete out of the hot path.
    arctic::ArenaAllocator arena(steps * sizeof(double) + 64);
    
    auto t_start = std::chrono::steady_clock::now();
    
    for (int p = 0; p < num_paths; ++p) {
        arena.reset();
        double* v_history = arena.allocate<double>(steps);
        
        // exact OU step
        v_history[0] = mu;
        for (int i = 1; i < steps; ++i) {
            v_history[i] = sampler.step(v_history[i - 1], rng_ou);
        }

        // Fair value at fractional execution time t = i + delta/dt (interpolated).
        auto v_at = [&](double t) -> double {
            if (t <= 0.0) return v_history[0];
            if (t >= static_cast<double>(steps - 1)) return v_history[steps - 1];
            int lo = static_cast<int>(t);
            double f = t - lo;
            return v_history[lo] * (1.0 - f) + v_history[lo + 1] * f;
        };
        
        bool solo_acted = false;
        bool game_resolved = false;
        
        for (int i = 1; i < steps; ++i) {
            if (!solo_acted) {
                auto decision_solo = agent_solo.evaluate_action(v_history, steps, i, b_solo, rng_solo);
                if (decision_solo.wants_to_act) {
                    // Snipe stale mean-anchored quote: PnL = V_exec - (mu + half_spread).
                    double v_exec = v_at(i + decision_solo.latency_drawn / dt);
                    pnl_solo += (v_exec - (mu + half_spread));
                    trades_solo++;
                    solo_acted = true;
                }
            }
            
            if (!game_resolved) {
                auto dec_a = agent_a.evaluate_action(v_history, steps, i, b_a, rng_a);
                auto dec_b = agent_b.evaluate_action(v_history, steps, i, b_b, rng_b);
                
                if (dec_a.wants_to_act && dec_b.wants_to_act) {
                    // Contested race: winner snipes stale mu quote; loser pays -half_spread.
                    auto result = resolver.resolve_race(dec_a.latency_drawn, dec_b.latency_drawn);
                    double v_exec_a = v_at(i + dec_a.latency_drawn / dt);
                    double v_exec_b = v_at(i + dec_b.latency_drawn / dt);
                    if (result.agent_a_won) {
                        double path_pnl = v_exec_a - (mu + half_spread);
                        pnl_a += path_pnl;
                        pnl_per_path_a[p] = path_pnl;
                        trades_a++;
                        stop_times_a.push_back(i);
                        pnl_b += -half_spread; // loser fills at corrected fair + spread
                        trades_b++;
                    } else if (result.agent_b_won) {
                        pnl_b += (v_exec_b - (mu + half_spread));
                        trades_b++;
                        double la_pnl = -half_spread; // A loses
                        pnl_a += la_pnl;
                        pnl_per_path_a[p] = la_pnl;
                        trades_a++;
                        stop_times_a.push_back(i);
                    }
                    game_resolved = true;
                } else if (dec_a.wants_to_act) {
                    // Uncontested fire by A (treated as winner).
                    double v_exec = v_at(i + dec_a.latency_drawn / dt);
                    double path_pnl = v_exec - (mu + half_spread);
                    pnl_a += path_pnl;
                    pnl_per_path_a[p] = path_pnl;
                    trades_a++;
                    stop_times_a.push_back(i);
                    game_resolved = true;
                } else if (dec_b.wants_to_act) {
                    double v_exec = v_at(i + dec_b.latency_drawn / dt);
                    pnl_b += (v_exec - (mu + half_spread));
                    trades_b++;
                    game_resolved = true;
                }
            }
        }
    }
    
    auto t_end = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    
    double win_rate_a = (trades_a + trades_b > 0) ? static_cast<double>(trades_a) / (trades_a + trades_b) : 0.0;
    double win_rate_b = (trades_a + trades_b > 0) ? static_cast<double>(trades_b) / (trades_a + trades_b) : 0.0;
    double avg_pnl_solo = trades_solo ? pnl_solo / num_paths : 0.0;
    double avg_pnl_a = trades_a ? pnl_a / num_paths : 0.0;
    // Cost of facing a competitor versus trading alone (can be negative when the
    // dispersion profile actually favours A in the race).
    double competitive_cost = avg_pnl_solo - avg_pnl_a;
    
    double sharpe_a = arctic::compute_sharpe_ratio(pnl_per_path_a.data(), num_paths);
    
    double mean_stop_a = 0.0;
    if (!stop_times_a.empty()) {
        mean_stop_a = std::accumulate(stop_times_a.begin(), stop_times_a.end(), 0.0)
                      / stop_times_a.size();
    }
    
    return {mean_A, std_A, mean_B, std_B, b_a, b_b, p_win_a, win_rate_a, win_rate_b, 
            avg_pnl_a, avg_pnl_solo, competitive_cost, sharpe_a, mean_stop_a, elapsed};
}

int main() {
    std::ofstream out("data/sweep_results.csv");
    out << "Mean_A,Std_A,Mean_B,Std_B,Eq_B_A,Eq_B_B,P_Win_A,Win_Rate_A,Win_Rate_B,"
        << "Avg_PnL_A,Avg_PnL_Solo,Competitive_Cost,Sharpe_A,Mean_StopTime_A,Elapsed_ms\n";
    
    // Fixed latency MEAN for both agents; we sweep agent A's relative dispersion
    // (coefficient of variation = std/mean) to probe the equal-mean / varying-jitter
    // regime. B holds a fixed, tight CV of 0.1.
    const double mean_lat = 0.02;
    const double std_B = mean_lat * 0.1;
    const double mean_A = mean_lat;
    const double mean_B = mean_lat;
    
    std::cout << "Running Dispersion Sweep at fixed latency mean (50k paths/step)...\n";
    for (double cv_A = 0.1; cv_A <= 1.51; cv_A += 0.1) {
        double std_A = mean_lat * cv_A;
        std::cout << "Evaluating CV_A = " << cv_A << " (std_A=" << std_A << ")..." << std::flush;
        auto result = run_sweep_step(mean_A, std_A, mean_B, std_B);
        
        out << result.mean_A << ","
            << result.std_A << ","
            << result.mean_B << ","
            << result.std_B << ","
            << result.eq_b_A << ","
            << result.eq_b_B << ","
            << result.p_win_A << ","
            << result.win_rate_A << ","
            << result.win_rate_B << ","
            << result.avg_pnl_A << ","
            << result.avg_pnl_Solo << ","
            << result.competitive_cost << ","
            << result.sharpe_A << ","
            << result.mean_stop_time_A << ","
            << result.elapsed_ms << "\n";
            
        std::cout << " Done. (P_win=" << result.p_win_A 
                  << ", Cost=" << result.competitive_cost 
                  << ", Sharpe=" << result.sharpe_A
                  << ", " << result.elapsed_ms << "ms)\n";
    }
    
    out.close();
    std::cout << "Sweep complete! Data written to data/sweep_results.csv\n";
    return 0;
}
