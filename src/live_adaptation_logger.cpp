#include "live_latency.hpp"
#include "race_resolver.hpp"
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>

int main() {
    std::cout << "Starting 60-Second Live Adaptation Test..." << std::endl;
    
    arctic::LiveLatency live_latency("127.0.0.1", 12345);
    live_latency.start();
    
    std::ofstream out("data/live_adaptation.csv");
    out << "Time_s,Mu,Sigma,Nash_b_A\n";
    
    // Agent B properties (fixed)
    double sigma_B = 0.2;  
    double b_single = 1.0;
    double sigma_V = 1.0;

    auto compute_nash_boundary = [](double b_base, double sig_self, double sig_competitor, double sig_v) {
        double k_compression = 0.5; 
        double variance_gap = sig_self - sig_competitor;
        if (variance_gap > 0) {
            return b_base - k_compression * sig_v * variance_gap;
        }
        return b_base;
    };
    
    // We sample every 1 second for 60 seconds
    for (int t = 0; t <= 60; ++t) {
        double current_mu = live_latency.get_mu();
        double current_sigma = live_latency.get_sigma();
        
        if (current_sigma < 0.01) current_sigma = 0.01;
        
        double b_A = compute_nash_boundary(b_single, current_sigma, sigma_B, sigma_V);
        
        out << t << "," << current_mu << "," << current_sigma << "," << b_A << "\n";
        
        if (t % 10 == 0) {
            std::cout << "t=" << t << "s | Sigma: " << current_sigma << " | Boundary: " << b_A << std::endl;
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    live_latency.stop();
    out.close();
    std::cout << "Test Complete. Data written to data/live_adaptation.csv" << std::endl;
    return 0;
}
