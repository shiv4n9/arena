// loopback UDP test. scheduler gives us horrible jitter here compared to real colo
// but good enough to test if our boundary adapts properly when the network freaks out

#include "live_latency.hpp"
#include "math_utils.hpp"
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <cmath>

int main() {
    std::cout << "Starting 60-Second Live Adaptation Test..." << std::endl;
    
    arctic::LiveLatency live_latency("127.0.0.1", 12345);
    live_latency.start();
    
    // don't just sleep. welford needs actual data points or it spits garbage
    constexpr size_t min_warmup_samples = 100;
    std::cout << "Waiting for " << min_warmup_samples << " latency samples..." << std::endl;
    int warmup_attempts = 0;
    while (live_latency.get_sample_count() < min_warmup_samples) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        warmup_attempts++;
        if (warmup_attempts > 100) {
            std::cout << "WARNING: Only " << live_latency.get_sample_count()
                      << " samples after 10s. Proceeding." << std::endl;
            break;
        }
    }
    std::cout << "Warmup complete: " << live_latency.get_sample_count() << " samples." << std::endl;
    
    std::ofstream out("data/live_adaptation.csv");
    out << "Time_s,Mean_A,Std_A,CV_A,Eq_b_A,Eq_b_B,P_Win,Signal_Decay\n";
    
    // Fixed model latency scale; the live network drives only the relative
    // dispersion (coefficient of variation). Competitor B holds a tight CV.
    const double model_mean_latency = 0.02;
    const double mean_B = 0.02;
    const double std_B = 0.004; // CV_B = 0.2
    double theta = 2.0;
    double mu = 0.0;
    double cost_c = 0.05; // = half-spread crossed when sniping (unified cost)

    for (int t = 0; t <= 60; ++t) {
        double live_mean = live_latency.get_mean_latency();
        double live_std = live_latency.get_std_latency();
        double cv_A = (live_mean > 0.0) ? live_std / live_mean : 0.0;
        
        if (cv_A < 0.01) cv_A = 0.01;
        if (cv_A > 2.0) cv_A = 2.0;
        
        double mean_A = model_mean_latency;
        double std_A = model_mean_latency * cv_A;
        
        double b_A = arctic::compute_equilibrium_boundary(mean_A, std_A, mean_B, std_B, theta, mu, cost_c);
        double b_B = arctic::compute_equilibrium_boundary(mean_B, std_B, mean_A, std_A, theta, mu, cost_c);
        double p_win = arctic::compute_p_win(mean_A, std_A, mean_B, std_B);
        double signal_decay = arctic::expected_signal_decay(mean_A, std_A, theta);
        
        out << t << "," << mean_A << "," << std_A << "," << cv_A
            << "," << b_A << "," << b_B << "," << p_win 
            << "," << signal_decay << "\n";
        
        if (t % 10 == 0) {
            std::cout << "t=" << t << "s | Std_A: " << std_A 
                      << " | b_A*: " << b_A << " | P(win): " << p_win 
                      << " | Decay: " << signal_decay 
                      << " | n=" << live_latency.get_sample_count() << std::endl;
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    live_latency.stop();
    out.close();
    std::cout << "Test Complete. Data written to data/live_adaptation.csv" << std::endl;
    return 0;
}
