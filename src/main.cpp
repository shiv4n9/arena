// main sim loop
// we use loopback udp for jitter testing because getting real colo packet captures
// onto this box is a nightmare. it's noisy but proves the boundary math works.

#include "ou_sampler.hpp"
#include "single_agent.hpp"
#include "race_resolver.hpp"
#include "live_latency.hpp"
#include "math_utils.hpp"
#include "arena_allocator.hpp"
#include "tsc_clock.hpp"
#include "order_book.hpp"
#include <iostream>
#include <vector>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <thread>
#include <chrono>
#include <numeric>
#include <memory>

void run_simulation(double mean_A, double std_A, double mean_B, double std_B,
                    const std::string& scenario_name) {
    double theta = 2.0;
    double mu = 0.0;
    double sigma_V = 1.0;
    double dt = 0.01;
    int steps = 1000;
    int num_paths = 50000;
    // Cost of crossing the spread = the half-spread the sniper pays. These MUST
    // be the same number: the analytic boundary is the zero-EV threshold of the
    // simulated payoff only if cost_c == half_spread.
    constexpr double half_spread = 0.05; // 5-tick half-spread
    double cost_c = half_spread;
    
    // verify math
    arctic::verify_equilibrium_convergence(mean_A, std_A, mean_B, std_B, theta, mu, cost_c);
    
    arctic::OUSampler sampler(theta, mu, sigma_V, dt);
    arctic::SingleAgent agent_solo(mean_A, std_A, dt);
    arctic::SingleAgent agent_a(mean_A, std_A, dt);
    arctic::SingleAgent agent_b(mean_B, std_B, dt);
    arctic::RaceResolver resolver;
    
    // bugfix: need different seeds here otherwise they pull the exact same
    // latency draws and the PnL diff is just noise correlation, not real competition
    std::mt19937_64 rng_ou(42);
    std::mt19937_64 rng_solo(101);  // Was 100 — shared with rng_a (BUG)
    std::mt19937_64 rng_a(201);     // Was 100 — shared with rng_solo (BUG)
    std::mt19937_64 rng_b(301);     // Was 200
    
    // Derived equilibrium boundaries
    double b_solo = arctic::compute_solo_boundary(mean_A, std_A, theta, mu, cost_c);
    double b_a = arctic::compute_equilibrium_boundary(mean_A, std_A, mean_B, std_B, theta, mu, cost_c); 
    double b_b = arctic::compute_equilibrium_boundary(mean_B, std_B, mean_A, std_A, theta, mu, cost_c); 
    
    double pnl_solo = 0.0;
    double pnl_a = 0.0;
    double pnl_b = 0.0;
    int trades_solo = 0;
    int trades_a = 0;
    int trades_b = 0;
    
    // Per-path PnL tracking for Sharpe ratio computation
    std::vector<double> pnl_per_path_solo(num_paths, 0.0);
    std::vector<double> pnl_per_path_a(num_paths, 0.0);
    
    // Stopping time tracking (step index at which agent first acts)
    std::vector<int> stop_time_solo;
    std::vector<int> stop_time_a;
    stop_time_solo.reserve(num_paths);
    stop_time_a.reserve(num_paths);
    
    // arena alloc. no new/delete in the loop so we don't blow up on latency
    arctic::ArenaAllocator arena(steps * sizeof(double) + 64); // +64 for alignment
    
    // limit order book. big heap alloc upfront, then we just clear/seed it per path
    auto lob = std::make_unique<arctic::OrderBook>(0.01, mu);
    constexpr int lob_depth = 10;        // 10 levels each side
    constexpr int32_t qty_per_level = 100;
    double total_slippage = 0.0;
    int lob_fills = 0;
    
    // timers
    arctic::TscClock tsc;
    auto t_start = std::chrono::steady_clock::now();
    uint64_t tsc_start = arctic::TscClock::rdtscp();
    
    for (int p = 0; p < num_paths; ++p) {
        // Allocate path from arena (O(1) bump pointer, zero syscalls)
        arena.reset();
        double* v_history = arena.allocate<double>(steps);
        
        // Generate OU path using exact transition kernel (zero discretization bias)
        v_history[0] = mu;
        for (int i = 1; i < steps; ++i) {
            v_history[i] = sampler.step(v_history[i - 1], rng_ou);
        }

        // Fair value at a FRACTIONAL (continuous) execution time t = i + delta/dt.
        // The order arrives at a real-valued instant, so we linearly interpolate the
        // discrete OU path instead of truncating delta/dt to an integer (removes the
        // sub-step rounding bias S1).
        auto v_at = [&](double t) -> double {
            if (t <= 0.0) return v_history[0];
            if (t >= static_cast<double>(steps - 1)) return v_history[steps - 1];
            int lo = static_cast<int>(t);
            double f = t - lo;
            return v_history[lo] * (1.0 - f) + v_history[lo + 1] * f;
        };
        
        bool solo_acted = false;
        bool game_resolved = false;
        int mm_update_freq = 5; // MM refreshes its (static, mean-anchored) book every 5 steps
        
        for (int i = 1; i < steps; ++i) {
            // Market Maker quotes a STATIC book anchored at the unconditional mean mu.
            // For a mean-reverting fundamental the rational static quote is mu +/-
            // half_spread; the transient deviation (V_i - mu) is the stale edge a
            // latency winner snipes before the MM can reprice.
            if (i % mm_update_freq == 0) {
                lob->clear();
                lob->seed_liquidity(mu, half_spread, lob_depth, qty_per_level);
            }

            if (!solo_acted) {
                auto decision_solo = agent_solo.evaluate_action(v_history, steps, i, b_solo, rng_solo);
                if (decision_solo.wants_to_act) {
                    // Continuous execution instant and the fair value realised there.
                    double exec_t = i + decision_solo.latency_drawn / dt;
                    double v_exec = v_at(exec_t);

                    // Snipe the stale mean-anchored quote: reseed the book at mu and
                    // lift the offer. Fill ~ mu + half_spread; PnL = V_exec - fill.
                    lob->clear();
                    lob->seed_liquidity(mu, half_spread, lob_depth, qty_per_level);
                    double best_ask_before = lob->get_best_ask_price();
                    lob->match_market_order(true, 1); // buy 1 lot
                    const arctic::Fill* fills = lob->get_fills();
                    double fill_price = (lob->get_fill_count() > 0)
                        ? lob->ticks_to_price(fills[0].price_ticks)
                        : best_ask_before;
                    double slippage = fill_price - best_ask_before;
                    total_slippage += slippage;
                    lob_fills++;

                    double path_pnl = v_exec - fill_price;
                    pnl_solo += path_pnl;
                    pnl_per_path_solo[p] = path_pnl;
                    trades_solo++;
                    stop_time_solo.push_back(i);
                    solo_acted = true;
                }
            }
            
            if (!game_resolved) {
                auto dec_a = agent_a.evaluate_action(v_history, steps, i, b_a, rng_a);
                auto dec_b = agent_b.evaluate_action(v_history, steps, i, b_b, rng_b);
                
                if (dec_a.wants_to_act && dec_b.wants_to_act) {
                    // CONTESTED RACE. Winner snipes the stale mu quote; loser arrives
                    // after the MM has repriced to the corrected fair, so it crosses a
                    // fresh spread and nets exactly -half_spread (loser-pays mechanism).
                    auto result = resolver.resolve_race(dec_a.latency_drawn, dec_b.latency_drawn);
                    double exec_t_a = i + dec_a.latency_drawn / dt;
                    double exec_t_b = i + dec_b.latency_drawn / dt;
                    double v_exec_a = v_at(exec_t_a);
                    double v_exec_b = v_at(exec_t_b);

                    if (result.agent_a_won) {
                        // Winner A: snipe stale mu quote.
                        lob->clear();
                        lob->seed_liquidity(mu, half_spread, lob_depth, qty_per_level);
                        lob->match_market_order(true, 1);
                        const arctic::Fill* fills = lob->get_fills();
                        double fill_price = (lob->get_fill_count() > 0)
                            ? lob->ticks_to_price(fills[0].price_ticks) : (mu + half_spread);
                        pnl_a += (v_exec_a - fill_price);
                        pnl_per_path_a[p] = v_exec_a - fill_price;
                        trades_a++;
                        stop_time_a.push_back(i);

                        // Loser B: book repriced to its corrected fair value.
                        lob->clear();
                        lob->seed_liquidity(v_exec_b, half_spread, lob_depth, qty_per_level);
                        lob->match_market_order(true, 1);
                        const arctic::Fill* lfills = lob->get_fills();
                        double lfill = (lob->get_fill_count() > 0)
                            ? lob->ticks_to_price(lfills[0].price_ticks) : (v_exec_b + half_spread);
                        pnl_b += (v_exec_b - lfill); // = -half_spread
                        trades_b++;
                    } else if (result.agent_b_won) {
                        // Winner B.
                        lob->clear();
                        lob->seed_liquidity(mu, half_spread, lob_depth, qty_per_level);
                        lob->match_market_order(true, 1);
                        const arctic::Fill* fills = lob->get_fills();
                        double fill_price = (lob->get_fill_count() > 0)
                            ? lob->ticks_to_price(fills[0].price_ticks) : (mu + half_spread);
                        pnl_b += (v_exec_b - fill_price);
                        trades_b++;

                        // Loser A.
                        lob->clear();
                        lob->seed_liquidity(v_exec_a, half_spread, lob_depth, qty_per_level);
                        lob->match_market_order(true, 1);
                        const arctic::Fill* lfills = lob->get_fills();
                        double lfill = (lob->get_fill_count() > 0)
                            ? lob->ticks_to_price(lfills[0].price_ticks) : (v_exec_a + half_spread);
                        double la_pnl = v_exec_a - lfill; // = -half_spread
                        pnl_a += la_pnl;
                        pnl_per_path_a[p] = la_pnl;
                        trades_a++;
                        stop_time_a.push_back(i);
                    }
                    game_resolved = true;
                } else if (dec_a.wants_to_act) {
                    // UNCONTESTED fire by A: no rival to lose to, so A snipes the
                    // stale mu quote uncontested (treated as winner).
                    double v_exec = v_at(i + dec_a.latency_drawn / dt);
                    lob->clear();
                    lob->seed_liquidity(mu, half_spread, lob_depth, qty_per_level);
                    lob->match_market_order(true, 1);
                    const arctic::Fill* fills = lob->get_fills();
                    double fill_price = (lob->get_fill_count() > 0)
                        ? lob->ticks_to_price(fills[0].price_ticks) : (mu + half_spread);
                    double path_pnl = v_exec - fill_price;
                    pnl_a += path_pnl;
                    pnl_per_path_a[p] = path_pnl;
                    trades_a++;
                    stop_time_a.push_back(i);
                    game_resolved = true;
                } else if (dec_b.wants_to_act) {
                    // UNCONTESTED fire by B.
                    double v_exec = v_at(i + dec_b.latency_drawn / dt);
                    lob->clear();
                    lob->seed_liquidity(mu, half_spread, lob_depth, qty_per_level);
                    lob->match_market_order(true, 1);
                    const arctic::Fill* fills = lob->get_fills();
                    double fill_price = (lob->get_fill_count() > 0)
                        ? lob->ticks_to_price(fills[0].price_ticks) : (mu + half_spread);
                    pnl_b += (v_exec - fill_price);
                    trades_b++;
                    game_resolved = true;
                }
            }
        }
    }
    
    auto t_end = std::chrono::steady_clock::now();
    uint64_t tsc_end = arctic::TscClock::rdtscp();
    double elapsed_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    double elapsed_ms_tsc = tsc.ticks_to_ns(tsc_end - tsc_start) / 1e6;
    double paths_per_sec = num_paths / (elapsed_ms / 1000.0);
    
    // stats
    double win_rate_a = (trades_a + trades_b > 0) ? static_cast<double>(trades_a) / (trades_a + trades_b) : 0.0;
    double win_rate_b = (trades_a + trades_b > 0) ? static_cast<double>(trades_b) / (trades_a + trades_b) : 0.0;
    double avg_pnl_solo = trades_solo ? pnl_solo / num_paths : 0.0;
    double avg_pnl_a = trades_a ? pnl_a / num_paths : 0.0;
    
    double p_win_a = arctic::compute_p_win(mean_A, std_A, mean_B, std_B);
    double sharpe_solo = arctic::compute_sharpe_ratio(pnl_per_path_solo.data(), num_paths);
    double sharpe_a = arctic::compute_sharpe_ratio(pnl_per_path_a.data(), num_paths);
    
    // Mean stopping times
    double mean_stop_solo = 0.0;
    if (!stop_time_solo.empty()) {
        mean_stop_solo = std::accumulate(stop_time_solo.begin(), stop_time_solo.end(), 0.0) 
                         / stop_time_solo.size();
    }
    double mean_stop_a = 0.0;
    if (!stop_time_a.empty()) {
        mean_stop_a = std::accumulate(stop_time_a.begin(), stop_time_a.end(), 0.0) 
                      / stop_time_a.size();
    }
    
    // Lag-1 autocorrelation of OU path (generate a dedicated validation path)
    arena.reset();
    double* validation_path = arena.allocate<double>(steps);
    validation_path[0] = mu;
    std::mt19937_64 rng_validation(999);
    for (int i = 1; i < steps; ++i) {
        validation_path[i] = sampler.step(validation_path[i - 1], rng_validation);
    }
    double empirical_rho1 = arctic::compute_lag1_autocorrelation(validation_path, steps);
    double theoretical_rho1 = std::exp(-theta * dt);
    
    // print
    std::cout << "\nARCTIC Simulation: " << scenario_name << std::endl;
    std::cout << "Agent A latency: mean=" << mean_A << ", std=" << std_A
              << "  |  Agent B latency: mean=" << mean_B << ", std=" << std_B << std::endl;
    std::cout << "Equilibrium b_A* = " << b_a << " | b_B* = " << b_b 
              << " | P(A wins race) = " << p_win_a << std::endl;
    std::cout << "────────────────────────────────────────────────────────" << std::endl;
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "Agent Solo | Trades: " << trades_solo << " | Avg PnL: " << avg_pnl_solo
              << " | Sharpe: " << sharpe_solo << " | Mean Stop: " << mean_stop_solo << std::endl;
    std::cout << "Agent A    | Trades: " << trades_a << " | Win Rate: " << win_rate_a * 100.0 
              << "% | Avg PnL: " << avg_pnl_a << " | Sharpe: " << sharpe_a 
              << " | Mean Stop: " << mean_stop_a << std::endl;
    std::cout << "Agent B    | Trades: " << trades_b << " | Win Rate: " << win_rate_b * 100.0 << "%" << std::endl;
    
    if (p_win_a < 0.5) {
        std::cout << "-> Competitive Cost (Solo - Agent A PnL): " << (avg_pnl_solo - avg_pnl_a) << std::endl;
    }
    
    std::cout << "────────────────────────────────────────────────────────" << std::endl;
    std::cout << "OU Path Validation: empirical rho_1=" << empirical_rho1 
              << " | theoretical exp(-theta*dt)=" << theoretical_rho1 << std::endl;
    std::cout << "Performance: " << num_paths << " paths in " << elapsed_ms << " ms ("
              << static_cast<int>(paths_per_sec) << " paths/sec)" << std::endl;
    std::cout << "TSC Timing:  " << elapsed_ms_tsc << " ms | TSC freq: "
              << tsc.get_estimated_freq_ghz() << " GHz" << std::endl;
    double avg_slippage = lob_fills > 0 ? total_slippage / lob_fills : 0.0;
    std::cout << "LOB Engine:  " << lob_fills << " fills | Avg Slippage: " 
              << avg_slippage << " | Spread: " << (2.0 * half_spread) 
              << " | Depth: " << lob_depth << " levels" << std::endl;
}

int main() {
    std::cout << "Initializing Live UDP Latency Measurement..." << std::endl;
    arctic::LiveLatency live_latency("127.0.0.1", 12345);
    live_latency.start();
    
    // wait for welford to warm up. sleeping for 2s is a prayer, not a measurement.
    constexpr size_t min_warmup_samples = 100;
    std::cout << "Warming up: waiting for " << min_warmup_samples << " RTT samples..." << std::endl;
    
    int warmup_attempts = 0;
    while (live_latency.get_sample_count() < min_warmup_samples) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        warmup_attempts++;
        if (warmup_attempts > 100) { // 10 second timeout
            std::cout << "WARNING: Only " << live_latency.get_sample_count() 
                      << " samples after 10s. Proceeding with available data." << std::endl;
            std::cout << "(Is the UDP echo server running? python tests/udp_echo_server.py)" << std::endl;
            break;
        }
    }
    std::cout << "Collected " << live_latency.get_sample_count() << " warmup samples." << std::endl;
    
    // The live loopback measures latency at a microsecond scale, far below the
    // model's time units. We therefore keep the latency *scale* fixed at a model
    // mean and let the live network drive only the relative dispersion
    // (coefficient of variation = std/mean), exactly the "adapt jitter, hold
    // scale" intent of the original demo — now expressed in (mean, std) terms.
    const double model_mean_latency = 0.02; // fixed model latency scale
    const double mean_B = 0.02;             // static competitor: same mean...
    const double std_B = 0.004;             // ...with a fixed 0.2 coefficient of variation
    
    for (int iter = 1; iter <= 5; ++iter) {
        std::cout << "\n=== Live Update Tick " << iter << " ===" << std::endl;
        
        double live_mean = live_latency.get_mean_latency();
        double live_std = live_latency.get_std_latency();
        double cv_live = (live_mean > 0.0) ? live_std / live_mean : 0.0;
        
        std::cout << "Fitted Live Latency: mean=" << live_mean << ", std=" << live_std 
                  << ", CV=" << cv_live << " (n=" << live_latency.get_sample_count() << ")" << std::endl;
        
        if (std::isnan(cv_live) || cv_live <= 0.0) {
            std::cout << "Insufficient samples for dispersion estimate. Skipping." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        
        // Clamp the live CV to a sane band; loopback heavy tails can spike it.
        if (cv_live < 0.01) cv_live = 0.01;
        if (cv_live > 2.0) cv_live = 2.0;
        
        double mean_A = model_mean_latency;
        double std_A = model_mean_latency * cv_live; // live jitter at fixed model scale
        
        run_simulation(mean_A, std_A, mean_B, std_B, "Live Dynamic Game Engine");
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    live_latency.stop();
    return 0;
}
