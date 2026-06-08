#include <emscripten/bind.h>
#include <emscripten/emscripten.h>
#include <atomic>
#include <vector>
#include <cmath>
#include <random>

using namespace emscripten;

class ArcticWasmEngine {
private:
    int num_points;
    double dt;
    
    // OU process parameters
    double theta = 2.0;
    double mu = 0.0;
    double sigma_V = 1.0;
    double cost_c = 0.05;  // = half-spread crossed when sniping (HFT-realistic)
    double model_mean_latency = 0.02;  // fixed latency MEAN; live feed drives dispersion
    
    // cached OU math
    double ou_decay;          // exp(-theta * dt)
    double ou_conditional_std; // sigma_V * sqrt((1 - exp(-2*theta*dt)) / (2*theta))
    
    // circular buffers for the ui
    std::vector<float> ou_signal;
    std::vector<float> boundary_a;
    std::vector<float> boundary_b;
    
    int head = 0;
    double current_v = 0.0;
    std::mt19937_64 rng;
    std::normal_distribution<double> norm_dist;
    
    // welford running stats
    size_t welford_n = 0;
    double welford_mean = 0.0;
    double welford_m2 = 0.0;
    
    // cache for js
    float cached_boundary_a_val = 1.0f;
    float cached_boundary_b_val = 1.0f;
    float cached_p_win = 0.5f;
    float cached_signal_decay_a = 1.0f;
    
    // lock-free read from webrtc
    std::atomic<float>* live_sigma_ptr = nullptr;

    // normal cdf
    static double normal_cdf(double x) {
        return 0.5 * std::erfc(-x * 0.70710678118654752440);
    }

    // Latency convention A: ln(L_i) ~ N(mu_i, sig_i^2), parameterised by the
    // real-world latency MEAN m and STD s. Log-space moments are recovered via
    //   sig_i^2 = ln(1 + (s/m)^2),  mu_i = ln(m) - sig_i^2/2,
    // and E[L_i] = m directly.
    static double latency_log_variance(double mean, double stddev) {
        if (mean <= 0.0) return 0.0;
        double cv = stddev / mean;
        return std::log1p(cv * cv);
    }
    static double latency_log_mu(double mean, double stddev) {
        return std::log(mean) - 0.5 * latency_log_variance(mean, stddev);
    }

    // Expected signal-decay E[e^{-theta L}] = Laplace transform of the log-normal
    // latency. No closed form -> self-normalising Gaussian midpoint quadrature.
    // By Jensen this is >= exp(-theta * mean), so dispersion erodes signal capture.
    static double expected_signal_decay(double mean, double std, double theta) {
        if (theta <= 0.0 || mean <= 0.0) return 1.0;
        double sig2 = latency_log_variance(mean, std);
        double sig_L = std::sqrt(sig2);
        if (sig_L < 1e-12) return std::exp(-theta * mean);
        double mu_L = latency_log_mu(mean, std);
        constexpr int N = 512;
        constexpr double zmax = 8.0;
        const double dz = (2.0 * zmax) / N;
        double num = 0.0, den = 0.0;
        for (int k = 0; k < N; ++k) {
            double z = -zmax + (k + 0.5) * dz;
            double w = std::exp(-0.5 * z * z);
            double latency = std::exp(mu_L + sig_L * z);
            num += w * std::exp(-theta * latency);
            den += w;
        }
        return num / den;
    }

    // P(self wins race) = Phi((mu_comp - mu_self) / sqrt(var_self + var_comp)).
    double compute_p_win(double mean_self, double std_self,
                         double mean_comp, double std_comp) const {
        double mu_self = latency_log_mu(mean_self, std_self);
        double mu_comp = latency_log_mu(mean_comp, std_comp);
        double var_self = latency_log_variance(mean_self, std_self);
        double var_comp = latency_log_variance(mean_comp, std_comp);
        double combined_std = std::sqrt(var_self + var_comp);
        if (combined_std < 1e-10) {
            if (mean_self < mean_comp) return 1.0;
            if (mean_self > mean_comp) return 0.0;
            return 0.5;
        }
        return normal_cdf((mu_comp - mu_self) / combined_std);
    }

    // math for the trigger (sniping) boundary
    // dominant strategy so we don't care what they do.
    double compute_equilibrium_boundary(double mean_self, double std_self,
                                        double mean_comp, double std_comp) const {
        // Signal surviving the random delay: Laplace transform of latency.
        double decay = expected_signal_decay(mean_self, std_self, theta);
        if (decay < 1e-10) {
            return 1.0; // Latency so high the signal decays entirely; fallback
        }
        
        double p_win = compute_p_win(mean_self, std_self, mean_comp, std_comp);
        
        // where payoff hits zero. ignores opponent's boundary.
        // P(win)*(b*-mu)*decay - cost_c = 0  =>  b* = mu + cost_c / (p_win * decay).
        // Numerator is the cost, NOT (cost - mu): no mu = 0 assumption.
        double effective_decay = p_win * decay;
        if (effective_decay < 1e-10) {
            return 1.0; // Can't win; use fallback
        }
        
        double b_star = mu + cost_c / effective_decay;
        return b_star;
    }

public:
    ArcticWasmEngine(int points, double delta_t) 
        : num_points(points), dt(delta_t), rng(42), norm_dist(0.0, 1.0) 
    {
        // Precompute exact OU transition kernel constants
        ou_decay = std::exp(-theta * dt);
        double var_term = (1.0 - std::exp(-2.0 * theta * dt)) / (2.0 * theta);
        ou_conditional_std = sigma_V * std::sqrt(var_term);
        
        ou_signal.resize(num_points, 0.0f);
        boundary_a.resize(num_points, 0.0f);
        boundary_b.resize(num_points, 0.0f);
    }
    
    // hook up shared memory from js
    void bind_latency_buffer(uintptr_t ptr) {
        live_sigma_ptr = reinterpret_cast<std::atomic<float>*>(ptr);
    }
    
    // called on raf. runs the exact OU math
    void step_frame(int steps_per_frame) {
        float cv_a = 0.1f; // fallback coefficient of variation
        if (live_sigma_ptr != nullptr) {
            // lock-free read; the live feed publishes a dispersion (CV) signal
            cv_a = live_sigma_ptr->load(std::memory_order_acquire);
        }
        
        // Clamp to reasonable range
        if (cv_a < 0.01f) cv_a = 0.01f;
        if (cv_a > 2.0f) cv_a = 2.0f;
        
        // Competitor holds a fixed CV of 0.2
        float cv_b = 0.2f;
        
        // Map dispersions onto the fixed model latency scale (mean, std)
        double mean_a = model_mean_latency;
        double std_a = model_mean_latency * static_cast<double>(cv_a);
        double mean_b = model_mean_latency;
        double std_b = model_mean_latency * static_cast<double>(cv_b);
        
        // run the math for both
        double b_A_val = compute_equilibrium_boundary(mean_a, std_a, mean_b, std_b);
        double b_B_val = compute_equilibrium_boundary(mean_b, std_b, mean_a, std_a);
        
        // Cache for JS readback
        cached_boundary_a_val = static_cast<float>(b_A_val);
        cached_boundary_b_val = static_cast<float>(b_B_val);
        
        // Cache auxiliary diagnostics
        cached_p_win = static_cast<float>(compute_p_win(mean_a, std_a, mean_b, std_b));
        cached_signal_decay_a = static_cast<float>(expected_signal_decay(mean_a, std_a, theta));
        
        for (int i = 0; i < steps_per_frame; ++i) {
            // exact OU step
            double Z = norm_dist(rng);
            current_v = mu + (current_v - mu) * ou_decay + ou_conditional_std * Z;
            
            // update stats
            welford_n++;
            double delta = current_v - welford_mean;
            welford_mean += delta / static_cast<double>(welford_n);
            double delta2 = current_v - welford_mean;
            welford_m2 += delta * delta2;
            
            ou_signal[head] = static_cast<float>(current_v);
            boundary_a[head] = cached_boundary_a_val;
            boundary_b[head] = cached_boundary_b_val;
            
            head = (head + 1) % num_points;
        }
    }
    
    // give js pointers so it can copy the arrays fast
    uintptr_t get_ou_signal_ptr() const { return reinterpret_cast<uintptr_t>(ou_signal.data()); }
    uintptr_t get_boundary_a_ptr() const { return reinterpret_cast<uintptr_t>(boundary_a.data()); }
    uintptr_t get_boundary_b_ptr() const { return reinterpret_cast<uintptr_t>(boundary_b.data()); }
    
    int get_head() const { return head; }
    
    // getters
    float get_current_v() const { return static_cast<float>(current_v); }
    float get_boundary_a_val() const { return cached_boundary_a_val; }
    float get_boundary_b_val() const { return cached_boundary_b_val; }
    float get_p_win() const { return cached_p_win; }
    float get_signal_decay() const { return cached_signal_decay_a; }
    
    // welford stats
    float get_signal_mean() const { return static_cast<float>(welford_mean); }
    float get_signal_variance() const { 
        if (welford_n < 2) return 0.0f;
        return static_cast<float>(welford_m2 / (welford_n - 1)); 
    }
    float get_theoretical_variance() const {
        return static_cast<float>((sigma_V * sigma_V) / (2.0 * theta));
    }
};

EMSCRIPTEN_BINDINGS(arctic_wasm) {
    class_<ArcticWasmEngine>("ArcticWasmEngine")
        .constructor<int, double>()
        .function("bind_latency_buffer", &ArcticWasmEngine::bind_latency_buffer)
        .function("step_frame", &ArcticWasmEngine::step_frame)
        .function("get_ou_signal_ptr", &ArcticWasmEngine::get_ou_signal_ptr)
        .function("get_boundary_a_ptr", &ArcticWasmEngine::get_boundary_a_ptr)
        .function("get_boundary_b_ptr", &ArcticWasmEngine::get_boundary_b_ptr)
        .function("get_head", &ArcticWasmEngine::get_head)
        .function("get_current_v", &ArcticWasmEngine::get_current_v)
        .function("get_boundary_a_val", &ArcticWasmEngine::get_boundary_a_val)
        .function("get_boundary_b_val", &ArcticWasmEngine::get_boundary_b_val)
        .function("get_p_win", &ArcticWasmEngine::get_p_win)
        .function("get_signal_decay", &ArcticWasmEngine::get_signal_decay)
        .function("get_signal_mean", &ArcticWasmEngine::get_signal_mean)
        .function("get_signal_variance", &ArcticWasmEngine::get_signal_variance)
        .function("get_theoretical_variance", &ArcticWasmEngine::get_theoretical_variance);
}
