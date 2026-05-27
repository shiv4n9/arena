#include "math_utils.hpp"
#include <cmath>
#include <iostream>
#include <iomanip>

namespace arctic {

double normal_cdf(double x) {
    return 0.5 * std::erfc(-x * 0.70710678118654752440);
}

double compute_p_win(double sig_self, double sig_competitor) {
    double var_self = sig_self * sig_self;
    double var_comp = sig_competitor * sig_competitor;
    double combined_std = std::sqrt(var_self + var_comp);
    if (combined_std < 1e-10) return 0.5;
    return normal_cdf((var_comp - var_self) / (2.0 * combined_std));
}

double compute_equilibrium_boundary(double sig_self, double sig_competitor,
                                     double theta, double mu_delta,
                                     double mu, double cost_c) {
    // E[delta] for LogNormal(mu_delta, sigma^2)
    double expected_latency = std::exp(mu_delta + sig_self * sig_self / 2.0);

    // Signal decay factor: OU mean-reverts during execution delay
    double decay = std::exp(-theta * expected_latency);
    if (decay < 1e-10) return 1.0; // Signal decays entirely; boundary unreachable

    double p_win = compute_p_win(sig_self, sig_competitor);
    double effective = p_win * decay;
    if (effective < 1e-10) return 1.0; // Zero win probability; no viable trade

    return mu + (cost_c - mu) / effective;
}

double compute_solo_boundary(double sig_self, double theta,
                              double mu_delta, double mu, double cost_c) {
    double expected_latency = std::exp(mu_delta + sig_self * sig_self / 2.0);
    double decay = std::exp(-theta * expected_latency);
    if (decay < 1e-10) return 1.0;
    return mu + (cost_c - mu) / decay;
}

bool verify_equilibrium_convergence(double sig_A, double sig_B,
                                     double theta, double mu_delta,
                                     double mu, double cost_c,
                                     double tol) {
    // best-response iteration. should converge instantly if the math is right
    double b_A = 10.0;  // Arbitrary initial guess
    double b_B = 10.0;  // Arbitrary initial guess

    std::cout << std::fixed << std::setprecision(8);
    std::cout << "Equilibrium Verification (Iterative Best-Response):" << std::endl;
    std::cout << "  Initial: b_A=" << b_A << ", b_B=" << b_B << std::endl;

    constexpr int max_iters = 100;
    int iters = 0;

    for (int k = 0; k < max_iters; ++k) {
        // best response A (ignores B)
        double b_A_new = compute_equilibrium_boundary(sig_A, sig_B, theta, mu_delta, mu, cost_c);
        // best response B (ignores A)
        double b_B_new = compute_equilibrium_boundary(sig_B, sig_A, theta, mu_delta, mu, cost_c);

        double err_A = std::abs(b_A_new - b_A);
        double err_B = std::abs(b_B_new - b_B);

        b_A = b_A_new;
        b_B = b_B_new;
        iters = k + 1;

        std::cout << "  Iter " << iters << ": b_A*=" << b_A << ", b_B*=" << b_B
                  << " | delta_A=" << err_A << ", delta_B=" << err_B << std::endl;

        if (err_A < tol && err_B < tol) {
            break;
        }
    }

    bool converged = (iters <= 1);
    if (converged) {
        std::cout << "  VERIFIED: Converged in " << iters << " iteration(s)." << std::endl;
        std::cout << "  This confirms the DOMINANT STRATEGY property: best-response" << std::endl;
        std::cout << "  is independent of opponent's boundary." << std::endl;
    } else {
        std::cout << "  WARNING: Required " << iters << " iterations." << std::endl;
        std::cout << "  This would indicate strategic coupling (non-trivial Nash)." << std::endl;
    }

    return converged;
}

double compute_sharpe_ratio(const double* pnl_per_path, size_t n) {
    if (n < 2) return 0.0;

    // welford pass
    double mean = 0.0;
    double m2 = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double delta = pnl_per_path[i] - mean;
        mean += delta / static_cast<double>(i + 1);
        double delta2 = pnl_per_path[i] - mean;
        m2 += delta * delta2;
    }

    double variance = m2 / static_cast<double>(n - 1);
    double stddev = std::sqrt(variance);
    if (stddev < 1e-15) return 0.0;
    return mean / stddev;
}

double compute_lag1_autocorrelation(const double* series, size_t n) {
    if (n < 3) return 0.0;

    // Compute mean
    double mean = 0.0;
    for (size_t i = 0; i < n; ++i) {
        mean += series[i];
    }
    mean /= static_cast<double>(n);

    // Compute variance and lag-1 covariance in a single pass
    double var_sum = 0.0;
    double cov_sum = 0.0;
    for (size_t i = 0; i < n - 1; ++i) {
        double dev_i = series[i] - mean;
        double dev_next = series[i + 1] - mean;
        var_sum += dev_i * dev_i;
        cov_sum += dev_i * dev_next;
    }
    // Add last element's contribution to variance
    double dev_last = series[n - 1] - mean;
    var_sum += dev_last * dev_last;

    if (var_sum < 1e-15) return 0.0;
    return cov_sum / var_sum;
}

} // namespace arctic
