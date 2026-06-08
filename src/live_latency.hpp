#pragma once

#include "spsc_buffer.hpp"
#include <atomic>
#include <thread>
#include <string>
#include <cmath>

namespace arctic {

// runs a background thread to ping loopback and fit the variance
// jitter is gonna look high because it's local but good for testing the math
class LiveLatency {
public:
    LiveLatency(const std::string& target_ip, int target_port, size_t buffer_capacity = 1024);
    ~LiveLatency();

    // boot threads
    void start();
    
    // kill threads
    void stop();

    // get mu safely (mean of ln(latency); log-space MLE)
    double get_mu() const { return mu_.load(std::memory_order_acquire); }
    
    // get sigma safely (std of ln(latency); log-space MLE)
    double get_sigma() const { return sigma_.load(std::memory_order_acquire); }

    // Real-world latency MEAN derived from the log-normal fit:
    //   m = exp(mu + sigma^2 / 2)
    double get_mean_latency() const {
        double mu = mu_.load(std::memory_order_acquire);
        double sig = sigma_.load(std::memory_order_acquire);
        return std::exp(mu + 0.5 * sig * sig);
    }

    // Real-world latency STANDARD DEVIATION derived from the log-normal fit:
    //   s = m * sqrt(exp(sigma^2) - 1)
    double get_std_latency() const {
        double mu = mu_.load(std::memory_order_acquire);
        double sig = sigma_.load(std::memory_order_acquire);
        double m = std::exp(mu + 0.5 * sig * sig);
        return m * std::sqrt(std::expm1(sig * sig));
    }

    // valid samples
    size_t get_sample_count() const { return sample_count_.load(std::memory_order_acquire); }

private:
    void udp_measurement_loop();
    void mle_fitting_loop();
    
    // nano timer
    double get_time_ns() const;

    std::string target_ip_;
    int target_port_;
    
    std::atomic<bool> running_;
    
    // lock-free pipe for rtt data
    SPSCBuffer<double> rtt_buffer_;
    
    std::thread udp_thread_;
    std::thread mle_thread_;
    
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324)
#endif

    // padded to avoid cache thrashing
    alignas(64) std::atomic<double> mu_;
    alignas(64) std::atomic<double> sigma_;
    alignas(64) std::atomic<size_t> sample_count_;

#ifdef _MSC_VER
#pragma warning(pop)
#endif
};

} // namespace arctic
