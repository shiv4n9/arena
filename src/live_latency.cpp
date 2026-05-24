#include "live_latency.hpp"
#include <cmath>
#include <iostream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>
#endif

namespace arctic {

LiveLatency::LiveLatency(const std::string& target_ip, int target_port, size_t buffer_capacity)
    : target_ip_(target_ip), target_port_(target_port), running_(false),
      rtt_buffer_(buffer_capacity), mu_(-4.0), sigma_(0.5) {
}

LiveLatency::~LiveLatency() {
    stop();
}

void LiveLatency::start() {
    if (running_.exchange(true)) return;
    
    udp_thread_ = std::thread(&LiveLatency::udp_measurement_loop, this);
    mle_thread_ = std::thread(&LiveLatency::mle_fitting_loop, this);
}

void LiveLatency::stop() {
    if (!running_.exchange(false)) return;
    
    if (udp_thread_.joinable()) udp_thread_.join();
    if (mle_thread_.joinable()) mle_thread_.join();
}

double LiveLatency::get_time_ns() const {
#ifdef _WIN32
    LARGE_INTEGER count;
    QueryPerformanceCounter(&count);
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    return static_cast<double>(count.QuadPart) / freq.QuadPart * 1e9;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return ts.tv_sec * 1e9 + ts.tv_nsec;
#endif
}

void LiveLatency::udp_measurement_loop() {
    // Cross-platform thread affinity pinning
#ifdef _WIN32
    SetThreadAffinityMask(GetCurrentThread(), 1); // Pin to core 0
#else
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset); // Pin to core 0
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif

#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

#ifdef _WIN32
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) return; // Silent fail for baseline mock
#else
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return; // Silent fail for baseline mock
#endif

    // Set non-blocking
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);
#else
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(static_cast<uint16_t>(target_port_));
#ifdef _WIN32
    inet_pton(AF_INET, target_ip_.c_str(), &server_addr.sin_addr);
#else
    inet_pton(AF_INET, target_ip_.c_str(), &server_addr.sin_addr);
#endif

    char send_buf[64] = "ARCTIC_PING";
    char recv_buf[64];
    
    while (running_.load()) {
        double t_send = get_time_ns();
        
        // Send packet
        sendto(sock, send_buf, sizeof(send_buf), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
        
        // Busy-wait for response (or timeout after 5ms)
        double t_recv = 0.0;
        while (get_time_ns() - t_send < 5e6) { // 5ms timeout
            int bytes = recv(sock, recv_buf, sizeof(recv_buf), 0);
            if (bytes > 0) {
                t_recv = get_time_ns();
                break;
            }
        }
        
        if (t_recv > 0.0) {
            double rtt_seconds = (t_recv - t_send) / 1e9;
            // Push to lock-free SPSC buffer
            rtt_buffer_.push(rtt_seconds);
        }
        
        // Sleep 10ms between pings
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif
}

void LiveLatency::mle_fitting_loop() {
    // Welford's online algorithm for computing mean and variance of log(RTT)
    size_t count = 0;
    double mean_log = 0.0;
    double m2_log = 0.0;
    
    while (running_.load()) {
        double rtt_seconds = 0.0;
        
        // Consume all available samples
        while (rtt_buffer_.pop(rtt_seconds)) {
            if (rtt_seconds <= 0) continue;
            
            double log_rtt = std::log(rtt_seconds);
            count++;
            
            double delta = log_rtt - mean_log;
            mean_log += delta / count;
            double delta2 = log_rtt - mean_log;
            m2_log += delta * delta2;
            
            if (count > 1) {
                double var_log = m2_log / (count - 1);
                double stddev_log = std::sqrt(var_log);
                
                // Atomically expose parameters
                mu_.store(mean_log, std::memory_order_seq_cst);
                sigma_.store(stddev_log, std::memory_order_seq_cst);
            }
        }
        
        // Prevent high CPU usage when buffer is empty
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

} // namespace arctic
