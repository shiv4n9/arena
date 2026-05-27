#pragma once

#include <cstddef>

namespace arctic {

// standard normal cdf
double normal_cdf(double x);

// chance we win the race. tighter variance = more wins.
double compute_p_win(double sig_self, double sig_competitor);

// calculates when we should pull the trigger based on signal decay and race risk
double compute_equilibrium_boundary(double sig_self, double sig_competitor,
                                     double theta, double mu_delta,
                                     double mu, double cost_c);

// boundary when we're the only ones trading (no race risk)
double compute_solo_boundary(double sig_self, double theta,
                              double mu_delta, double mu, double cost_c);

// check if our best-response math actually converges
bool verify_equilibrium_convergence(double sig_A, double sig_B,
                                     double theta, double mu_delta,
                                     double mu, double cost_c,
                                     double tol = 1e-12);

// sharpe ratio
double compute_sharpe_ratio(const double* pnl_per_path, size_t n);

// lag-1 auto-correlation to check the sampler is working
double compute_lag1_autocorrelation(const double* series, size_t n);

} // namespace arctic
