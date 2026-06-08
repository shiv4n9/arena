#include <gtest/gtest.h>
#include "ou_sampler.hpp"
#include "math_utils.hpp"
#include <random>
#include <cmath>

TEST(OUSamplerTest, StationaryMoments) {
    double theta = 2.0;
    double mu = 100.0;
    double sigma_V = 5.0;
    double dt = 0.01;
    int num_steps = 100000;
    
    arctic::OUSampler sampler(theta, mu, sigma_V, dt);
    std::mt19937_64 rng(42); 
    
    // Burn-in
    double v_t = mu;
    for (int i = 0; i < 10000; ++i) {
        v_t = sampler.step(v_t, rng);
    }
    
    double sum = 0.0;
    double sum_sq = 0.0;
    for (int i = 0; i < num_steps; ++i) {
        v_t = sampler.step(v_t, rng);
        sum += v_t;
        sum_sq += v_t * v_t;
    }
    
    double empirical_mean = sum / num_steps;
    double empirical_var = (sum_sq / num_steps) - (empirical_mean * empirical_mean);
    
    double theoretical_mean = sampler.get_stationary_mean();
    double theoretical_var = sampler.get_stationary_variance();
    
    EXPECT_NEAR(empirical_mean, theoretical_mean, 0.1);
    EXPECT_NEAR(empirical_var, theoretical_var, theoretical_var * 0.05);
}

TEST(MathUtilsTest, ComputePWin) {
    // Latency convention A: ln(L) ~ N(mu, sig^2), parameterised by the real-world
    // latency MEAN m and STD s. The race is decided by g_i = exp(mu_i); at equal
    // mean a *higher* std lowers mu_i (rare spikes inflate the mean, so the typical
    // packet is faster), which RAISES the win probability.
    const double m = 0.02; // shared latency mean for the equal-mean cases

    // Equal mean and equal std => exactly a coin flip.
    EXPECT_DOUBLE_EQ(arctic::compute_p_win(m, 0.004, m, 0.004), 0.5);

    // Equal mean, self has LOWER jitter => win probability < 0.5 (wins less).
    EXPECT_LT(arctic::compute_p_win(m, 0.002, m, 0.008), 0.5);

    // Equal mean, self has HIGHER jitter => win probability > 0.5 (wins more).
    EXPECT_GT(arctic::compute_p_win(m, 0.008, m, 0.002), 0.5);

    // A genuine mean-latency (speed) edge dominates: a faster mean beats a
    // slower competitor regardless of the jitter trade-off.
    EXPECT_GT(arctic::compute_p_win(0.01, 0.003, 0.02, 0.002), 0.5);
}

TEST(MathUtilsTest, ComputeEquilibriumBoundary) {
    double mean_A = 0.02, std_A = 0.002;  // tight jitter
    double mean_B = 0.02, std_B = 0.008;  // loose jitter
    double theta = 2.0;
    double mu = 0.0;
    double cost_c = 0.05; // = half-spread (HFT-realistic, unified with the LOB)

    double b_A = arctic::compute_equilibrium_boundary(mean_A, std_A, mean_B, std_B, theta, mu, cost_c);
    double b_B = arctic::compute_equilibrium_boundary(mean_B, std_B, mean_A, std_A, theta, mu, cost_c);

    // Equal mean => equal signal decay. A has the TIGHTER jitter, so under
    // convention A it wins the race LESS often (P(win) < 0.5) and must therefore
    // demand a LARGER signal before firing: b_A > b_B.
    EXPECT_GT(b_A, b_B);
    EXPECT_GT(b_A, cost_c);
}

TEST(MathUtilsTest, ExpectedSignalDecayLaplace) {
    double theta = 2.0;
    double mean = 0.02;

    // Degenerate (zero dispersion): the Laplace transform collapses to e^{-theta m}.
    EXPECT_NEAR(arctic::expected_signal_decay(mean, 0.0, theta),
                std::exp(-theta * mean), 1e-9);

    // Jensen's inequality: E[e^{-theta L}] >= e^{-theta E[L]} for any std > 0, and
    // the gap is STRICT once there is dispersion. Latency variance helps capture.
    double decay_disp = arctic::expected_signal_decay(mean, 0.01, theta);
    EXPECT_GT(decay_disp, std::exp(-theta * mean));

    // The decay is always a valid survival factor in (0, 1].
    EXPECT_GT(decay_disp, 0.0);
    EXPECT_LE(decay_disp, 1.0);

    // More dispersion at fixed mean => larger Laplace transform (more mass near 0).
    EXPECT_GT(arctic::expected_signal_decay(mean, 0.02, theta),
              arctic::expected_signal_decay(mean, 0.005, theta));
}

TEST(MathUtilsTest, BoundaryIndifferenceIdentity) {
    // The boundary is DEFINED by P(win)*(b*-mu)*decay - cost_c = 0. Verify the
    // solved boundary satisfies that identity to numerical tolerance.
    double mean_A = 0.02, std_A = 0.006;
    double mean_B = 0.02, std_B = 0.003;
    double theta = 2.0, mu = 0.0, cost_c = 0.05;

    double b = arctic::compute_equilibrium_boundary(mean_A, std_A, mean_B, std_B, theta, mu, cost_c);
    double p = arctic::compute_p_win(mean_A, std_A, mean_B, std_B);
    double d = arctic::expected_signal_decay(mean_A, std_A, theta);
    EXPECT_NEAR(p * (b - mu) * d - cost_c, 0.0, 1e-9);

    // Solo boundary (P = 1) must also satisfy its own indifference identity.
    double b_solo = arctic::compute_solo_boundary(mean_A, std_A, theta, mu, cost_c);
    EXPECT_NEAR(1.0 * (b_solo - mu) * d - cost_c, 0.0, 1e-9);
}
