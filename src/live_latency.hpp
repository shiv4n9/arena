#pragma once

#include "spsc_buffer.hpp"
#include <atomic>
#include <thread>
#include <string>

namespace arctic {

/**
 * Manages the live background measurement of UDP round-trip latency
 * and continuous online MLE fitting of the Log-Normal distribution.
 */
class LiveLatency {
public:
    LiveLatency(const std::string& target_ip, int target_port, size_t buffer_capacity = 1024);
    ~LiveLatency();

    // Start background threads
    void start();
    
    // Stop background threads gracefully
    void stop();

    // Atomically read the latest fitted parameters
    double get_mu() const { return mu_.load(std::memory_order_seq_cst); }
    double get_sigma() const { return sigma_.load(std::memory_order_seq_cst); }

private:
    void udp_measurement_loop();
    void mle_fitting_loop();
    
    // Cross platform precision timer
    double get_time_ns() const;

    std::string target_ip_;
    int target_port_;
    
    std::atomic<bool> running_;
    
    // SPSC buffer for passing measured RTTs (in seconds) to the fitting thread
    SPSCBuffer<double> rtt_buffer_;
    
    std::thread udp_thread_;
    std::thread mle_thread_;
    
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324)
#endif

    // Fitted parameters
    alignas(64) std::atomic<double> mu_;
    alignas(64) std::atomic<double> sigma_;

#ifdef _MSC_VER
#pragma warning(pop)
#endif
};

} // namespace arctic
