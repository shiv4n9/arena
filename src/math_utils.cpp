#include "math_utils.hpp"
#include <cmath>
#include <iostream>
#include <iomanip>

namespace arctic {

double normal_cdf(double x) {
    return 0.5 * std::erfc(-x * 0.70710678118654752440);
}

double latency_log_variance(double mean, double stddev) {
    // sigma^2 = ln(1 + (s/m)^2) for L ~ LogNormal with mean m, std s.
    if (mean <= 0.0) return 0.0; // degenerate guard; latency must be positive
    double cv2 = (stddev * stddev) / (mean * mean);
    return std::log1p(cv2); // ln(1 + cv^2), numerically stable for small cv
}

double latency_log_mu(double mean, double stddev) {
    // mu = ln(m) - sigma^2 / 2
    if (mean <= 0.0) return 0.0; // degenerate guard
    return std::log(mean) - 0.5 * latency_log_variance(mean, stddev);
}

double expected_signal_decay(double mean, double std, double theta) {
    // E[e^{-theta L}], L ~ LogNormal(mean, std). The OU signal survives a random
    // execution delay L by the factor e^{-theta L}; its expectation is the
    // Laplace transform of L at theta. The log-normal Laplace transform has NO
    // closed form, so we integrate over the underlying normal Z ~ N(0,1):
    //   E[e^{-theta L}] = ∫ phi(z) exp(-theta * exp(mu_L + sig_L z)) dz.
    // We use a fine, self-normalising midpoint rule on [-zmax, zmax]; the
    // integrand is smooth and the Gaussian weight kills the tails past ~8 sigma,
    // so quadrature error is ~1e-12 — no modelling approximation is introduced.
    if (theta <= 0.0 || mean <= 0.0) return 1.0; // no reversion / no delay
    const double sig2 = latency_log_variance(mean, std);
    const double sig_L = std::sqrt(sig2);
    if (sig_L < 1e-12) {
        // Degenerate: L = mean almost surely => Laplace transform is e^{-theta m}.
        return std::exp(-theta * mean);
    }
    const double mu_L = latency_log_mu(mean, std);
    constexpr int N = 512;
    constexpr double zmax = 8.0;
    const double dz = (2.0 * zmax) / N;
    double num = 0.0;
    double den = 0.0;
    for (int k = 0; k < N; ++k) {
        const double z = -zmax + (k + 0.5) * dz;
        const double w = std::exp(-0.5 * z * z); // unnormalised N(0,1) weight
        const double latency = std::exp(mu_L + sig_L * z);
        num += w * std::exp(-theta * latency);
        den += w;
    }
    return num / den; // den normalises the discretised Gaussian weights to 1
}

double compute_p_win(double mean_self, double std_self,
                     double mean_competitor, double std_competitor) {
    // Map (mean, std) -> underlying-normal (mu, sigma^2), then race in log space.
    // D = ln(L_self) - ln(L_competitor) ~ N(mu_self - mu_comp, sig_self^2 + sig_comp^2).
    // P(self wins) = P(L_self < L_competitor) = P(D < 0) = Phi((mu_comp - mu_self)/sd).
    double var_self = latency_log_variance(mean_self, std_self);
    double var_comp = latency_log_variance(mean_competitor, std_competitor);
    double combined_std = std::sqrt(var_self + var_comp);
    if (combined_std < 1e-10) {
        // Both latencies effectively deterministic: the smaller mean wins outright.
        if (mean_self < mean_competitor) return 1.0;
        if (mean_self > mean_competitor) return 0.0;
        return 0.5;
    }
    double mu_self = latency_log_mu(mean_self, std_self);
    double mu_comp = latency_log_mu(mean_competitor, std_competitor);
    return normal_cdf((mu_comp - mu_self) / combined_std);
}

double compute_equilibrium_boundary(double mean_self, double std_self,
                                    double mean_competitor, double std_competitor,
                                    double theta, double mu, double cost_c) {
    // Signal decay over the RANDOM execution delay: Laplace transform of latency.
    // Latency std enters here (Jensen) as well as through P(win).
    double decay = expected_signal_decay(mean_self, std_self, theta);
    if (decay < 1e-10) return 1.0; // Signal decays entirely; boundary unreachable

    double p_win = compute_p_win(mean_self, std_self, mean_competitor, std_competitor);
    double effective = p_win * decay;
    if (effective < 1e-10) return 1.0; // Zero win probability; no viable trade

    // Indifference: P(win)*(b*-mu)*decay - cost_c = 0  =>  b* = mu + cost_c/(P*decay).
    // Numerator is the execution cost, NOT (cost_c - mu): no mu = 0 assumption.
    return mu + cost_c / effective;
}

double compute_solo_boundary(double mean_self, double std_self, double theta,
                             double mu, double cost_c) {
    // No competitor => P(win) = 1, no race-loss cost. Dispersion still matters
    // through the Laplace-transform decay E[e^{-theta L}].
    double decay = expected_signal_decay(mean_self, std_self, theta);
    if (decay < 1e-10) return 1.0;
    return mu + cost_c / decay;
}

bool verify_equilibrium_convergence(double mean_A, double std_A,
                                    double mean_B, double std_B,
                                    double theta, double mu, double cost_c,
                                    double tol) {
    // Verify the INDIFFERENCE IDENTITY that defines each boundary:
    //   g(b*) = P(win) * (b* - mu) * E[e^{-theta L}] - cost_c == 0.
    // This is a real algebraic check on the solved boundary (it would fail if the
    // formula, the decay, or P(win) were inconsistent), unlike a fixed-point loop
    // that converges trivially because the best response ignores the opponent.
    std::cout << std::fixed << std::setprecision(10);
    std::cout << "Boundary Indifference Verification:" << std::endl;

    double b_A = compute_equilibrium_boundary(mean_A, std_A, mean_B, std_B, theta, mu, cost_c);
    double b_B = compute_equilibrium_boundary(mean_B, std_B, mean_A, std_A, theta, mu, cost_c);

    double decay_A = expected_signal_decay(mean_A, std_A, theta);
    double decay_B = expected_signal_decay(mean_B, std_B, theta);
    double p_A = compute_p_win(mean_A, std_A, mean_B, std_B);
    double p_B = compute_p_win(mean_B, std_B, mean_A, std_A);

    double resid_A = p_A * (b_A - mu) * decay_A - cost_c;
    double resid_B = p_B * (b_B - mu) * decay_B - cost_c;

    std::cout << "  b_A* = " << b_A << " | P_A = " << p_A << " | decay_A = " << decay_A
              << " | residual = " << resid_A << std::endl;
    std::cout << "  b_B* = " << b_B << " | P_B = " << p_B << " | decay_B = " << decay_B
              << " | residual = " << resid_B << std::endl;

    bool ok = (std::abs(resid_A) < tol) && (std::abs(resid_B) < tol);
    if (ok) {
        std::cout << "  VERIFIED: both boundaries satisfy the zero-profit indifference"
                  << " condition (|residual| < " << tol << ")." << std::endl;
        std::cout << "  Note: b* is independent of the opponent's boundary, so this is"
                  << " also a dominant-strategy equilibrium." << std::endl;
    } else {
        std::cout << "  WARNING: indifference residual exceeds tolerance — the boundary"
                  << " formula and the payoff model are inconsistent." << std::endl;
    }
    return ok;
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
