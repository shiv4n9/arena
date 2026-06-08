#pragma once

#include <cstddef>

namespace arctic {

// ─────────────────────────────────────────────────────────────────────────
// Latency parameterisation
// ─────────────────────────────────────────────────────────────────────────
// Latencies L_i are Log-Normal. We parameterise every public function by the
// quantities we can actually measure: the real-world MEAN (m_i) and STANDARD
// DEVIATION (s_i) of the latency itself — NOT the underlying log-space
// location/scale (mu, sigma).
//
// Standard ("convention A") moment relations for L ~ LogNormal(mu, sigma):
//   m = exp(mu + sigma^2 / 2)              (mean)
//   s = m * sqrt(exp(sigma^2) - 1)         (std dev)
// inverted (what the helpers below compute):
//   sigma^2 = ln(1 + (s/m)^2)
//   mu      = ln(m) - sigma^2 / 2
//
// Race physics: A beats B iff L_A < L_B iff ln(L_A) - ln(L_B) < 0, with
//   ln(L_A) - ln(L_B) ~ N(mu_A - mu_B, sigma_A^2 + sigma_B^2).
// No equal-mean / co-location assumption is baked in anywhere.

// ── Log-moment conversion helpers: (mean, std) -> underlying normal ───────
// sigma^2 of ln(L) given the latency mean and std.
double latency_log_variance(double mean, double stddev);
// mu of ln(L) given the latency mean and std.
double latency_log_mu(double mean, double stddev);

// standard normal cdf
double normal_cdf(double x);

// Expected signal-decay factor E[e^{-theta * L}] where L ~ LogNormal(mean, std).
// This is the Laplace transform of the latency distribution evaluated at theta.
// The OU signal mean-reverts during the (random) execution delay L, so the
// fraction of the signal that survives is E[e^{-theta L}], NOT e^{-theta E[L]}.
// By Jensen's inequality E[e^{-theta L}] >= e^{-theta E[L]}, so latency VARIANCE
// (not just the mean) erodes signal capture. The log-normal has no closed-form
// Laplace transform, so this is integrated numerically (no approximation beyond
// quadrature error). Latency std therefore enters BOTH P(win) and the decay.
double expected_signal_decay(double mean, double std, double theta);

// P(self wins the race) = P(L_self < L_competitor), fully general in the
// agents' latency means and stds:
//   P(win) = Phi( (mu_comp - mu_self) / sqrt(sigma_self^2 + sigma_comp^2) )
// where (mu_i, sigma_i) are derived from (mean_i, std_i) via the helpers above.
double compute_p_win(double mean_self, double std_self,
                     double mean_competitor, double std_competitor);

// Competitive action (sniping) boundary. The agent snipes a stale quote anchored
// at the OU mean `mu`; the captured edge is (b - mu) * E[e^{-theta L_self}] and
// the execution cost is `cost_c` (the half-spread crossed). Losing the race still
// costs `cost_c` (the order fills at the corrected price), so the indifference
// condition  P(win) * (b* - mu) * decay - cost_c = 0  gives:
//   b* = mu + cost_c / ( P(win) * E[e^{-theta L_self}] )
// No mu = 0 assumption: the numerator is the cost, not (cost - mu).
double compute_equilibrium_boundary(double mean_self, double std_self,
                                    double mean_competitor, double std_competitor,
                                    double theta, double mu, double cost_c);

// Boundary when trading alone (P(win) = 1, no race-loss cost):
//   b* = mu + cost_c / E[e^{-theta L_self}].
// Latency dispersion still matters here through the Laplace-transform decay.
double compute_solo_boundary(double mean_self, double std_self, double theta,
                             double mu, double cost_c);

// Verify the indifference identity that DEFINES the boundary:
//   P(win) * (b* - mu) * E[e^{-theta L}] - cost_c == 0  (to tolerance).
// This is a substantive algebraic check, not a tautological fixed-point loop.
bool verify_equilibrium_convergence(double mean_A, double std_A,
                                    double mean_B, double std_B,
                                    double theta, double mu, double cost_c,
                                    double tol = 1e-9);

// sharpe ratio
double compute_sharpe_ratio(const double* pnl_per_path, size_t n);

// lag-1 auto-correlation to check the sampler is working
double compute_lag1_autocorrelation(const double* series, size_t n);

} // namespace arctic
