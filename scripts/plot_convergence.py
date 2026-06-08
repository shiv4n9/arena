"""
Monte Carlo Convergence Analysis for ARCTIC

Demonstrates that PnL standard error decreases as 1/sqrt(N), validating
that the Monte Carlo estimator converges properly and that the chosen
num_paths (50,000) provides sufficient precision.

This script implements the OU + boundary simulation directly in Python
rather than calling the C++ binary, enabling controlled variation of num_paths.
"""

import numpy as np
import matplotlib.pyplot as plt
import matplotlib as mpl
from scipy.stats import norm

def normal_cdf(x):
    return norm.cdf(x)

# Latency convention A: ln(L) ~ N(mu, sig^2), parameterised by the real-world
# latency MEAN m and STD s. Log-space moments:
#   sig^2 = ln(1 + (s/m)^2),  mu = ln(m) - sig^2/2,  and E[L] = m.
def latency_log_variance(mean, stddev):
    if mean <= 0.0:
        return 0.0
    cv = stddev / mean
    return np.log1p(cv * cv)

def latency_log_mu(mean, stddev):
    return np.log(mean) - 0.5 * latency_log_variance(mean, stddev)

def expected_signal_decay(mean, std, theta):
    """E[e^{-theta L}], L ~ LogNormal(mean, std): the Laplace transform of the
    latency. No closed form -> self-normalising Gaussian quadrature. By Jensen
    this exceeds exp(-theta*mean), so latency dispersion helps signal capture."""
    if theta <= 0.0 or mean <= 0.0:
        return 1.0
    sig2 = latency_log_variance(mean, std)
    sig_L = np.sqrt(sig2)
    if sig_L < 1e-12:
        return float(np.exp(-theta * mean))
    mu_L = latency_log_mu(mean, std)
    z = np.linspace(-8.0, 8.0, 1024)
    w = np.exp(-0.5 * z * z)
    lat = np.exp(mu_L + sig_L * z)
    return float(np.sum(w * np.exp(-theta * lat)) / np.sum(w))

def compute_p_win(mean_self, std_self, mean_comp, std_comp):
    mu_self = latency_log_mu(mean_self, std_self)
    mu_comp = latency_log_mu(mean_comp, std_comp)
    var_self = latency_log_variance(mean_self, std_self)
    var_comp = latency_log_variance(mean_comp, std_comp)
    combined = np.sqrt(var_self + var_comp)
    if combined < 1e-10:
        return 0.5 if mean_self == mean_comp else (1.0 if mean_self < mean_comp else 0.0)
    return normal_cdf((mu_comp - mu_self) / combined)

def compute_equilibrium_boundary(mean_self, std_self, mean_comp, std_comp, theta, mu, cost_c):
    decay = expected_signal_decay(mean_self, std_self, theta)  # Laplace transform
    if decay < 1e-10:
        return 1.0
    p_win = compute_p_win(mean_self, std_self, mean_comp, std_comp)
    effective = p_win * decay
    if effective < 1e-10:
        return 1.0
    # Indifference: P*(b*-mu)*decay - cost_c = 0  =>  b* = mu + cost_c/(P*decay).
    return mu + cost_c / effective

def run_simulation(num_paths, mean_A, std_A, mean_B, std_B, seed=42):
    """Run the ARCTIC simulation with exact OU kernel in Python."""
    theta = 2.0
    mu = 0.0
    sigma_V = 1.0
    dt = 0.01
    steps = 1000
    half_spread = 0.05
    cost_c = half_spread  # cost == half-spread (unified with the LOB/payoff)
    
    rng = np.random.RandomState(seed)
    
    # Precompute OU transition kernel
    ou_decay = np.exp(-theta * dt)
    ou_std = sigma_V * np.sqrt((1.0 - np.exp(-2.0 * theta * dt)) / (2.0 * theta))
    
    b_a = compute_equilibrium_boundary(mean_A, std_A, mean_B, std_B, theta, mu, cost_c)
    
    # Log-space params drive the lognormal latency draws (convention A).
    log_mu_A = latency_log_mu(mean_A, std_A)
    log_sig_A = np.sqrt(latency_log_variance(mean_A, std_A))
    
    pnl_per_path = np.zeros(num_paths)
    
    for p in range(num_paths):
        # Generate OU path (exact kernel)
        v = np.zeros(steps)
        v[0] = mu
        z = rng.randn(steps - 1)
        for i in range(1, steps):
            v[i] = mu + (v[i-1] - mu) * ou_decay + ou_std * z[i-1]
        
        # Send-side latency only: agent observes V in real time (no observation
        # lag); the drawn latency delays ONLY order arrival.
        latency_a = rng.lognormal(log_mu_A, log_sig_A)
        
        for i in range(1, steps):
            if v[i] >= b_a:
                # Continuous execution instant; interpolate the fair value there.
                exec_t = i + latency_a / dt
                if exec_t >= steps - 1:
                    v_exec = v[steps - 1]
                else:
                    lo = int(exec_t)
                    f = exec_t - lo
                    v_exec = v[lo] * (1.0 - f) + v[lo + 1] * f
                # Snipe stale mean-anchored quote: PnL = V_exec - (mu + half_spread).
                pnl_per_path[p] = v_exec - (mu + half_spread)
                break
    
    return pnl_per_path

def main():
    plt.style.use("dark_background")
    mpl.rcParams["axes.grid"] = True
    mpl.rcParams["grid.color"] = "#333333"
    mpl.rcParams["axes.edgecolor"] = "#555555"
    
    # Equal latency MEAN; agent A carries the looser jitter (higher CV).
    mean_A = 0.02
    std_A = 0.01    # CV_A = 0.5
    mean_B = 0.02
    std_B = 0.002   # CV_B = 0.1
    
    path_counts = [500, 1000, 2500, 5000, 10000, 25000, 50000]
    means = []
    std_errors = []
    
    print("Running convergence analysis...")
    for n in path_counts:
        print(f"  num_paths = {n}...", end="", flush=True)
        pnl = run_simulation(n, mean_A, std_A, mean_B, std_B)
        m = np.mean(pnl)
        se = np.std(pnl, ddof=1) / np.sqrt(n)
        means.append(m)
        std_errors.append(se)
        print(f" mean={m:.6f}, SE={se:.6f}")
    
    path_counts = np.array(path_counts)
    std_errors = np.array(std_errors)
    means = np.array(means)
    
    # Reference 1/sqrt(N) line scaled to match the first data point
    ref_scale = std_errors[0] * np.sqrt(path_counts[0])
    ref_line = ref_scale / np.sqrt(path_counts)
    
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 6))
    
    # Left: Standard Error vs N (log-log)
    ax1.loglog(path_counts, std_errors, 'o-', color="#ff00ff", linewidth=2.5, markersize=8, label="Empirical SE")
    ax1.loglog(path_counts, ref_line, '--', color="#555555", linewidth=1.5, label="$1/\\sqrt{N}$ reference")
    ax1.set_xlabel("Number of Monte Carlo Paths ($N$)", fontsize=14)
    ax1.set_ylabel("PnL Standard Error", fontsize=14)
    ax1.set_title("Monte Carlo Convergence", fontsize=16, fontweight='bold', color='white')
    ax1.legend(fontsize=12)
    
    # Right: Mean PnL vs N (shows stabilization)
    ax2.semilogx(path_counts, means, 's-', color="#00ffff", linewidth=2.5, markersize=8)
    ax2.fill_between(path_counts, means - 2*std_errors, means + 2*std_errors, color="#00ffff", alpha=0.15, label="$\\pm 2$ SE")
    ax2.set_xlabel("Number of Monte Carlo Paths ($N$)", fontsize=14)
    ax2.set_ylabel("Mean PnL (Agent A)", fontsize=14)
    ax2.set_title("PnL Estimate Stabilization", fontsize=16, fontweight='bold', color='white')
    ax2.legend(fontsize=12)
    
    fig.suptitle(f"Convergence Analysis ($s_A={std_A}$, $s_B={std_B}$, equal mean $m={mean_A}$)", 
                 fontsize=18, fontweight='bold', color='white', y=1.02)
    plt.tight_layout()
    plt.savefig("data/convergence_analysis.png", dpi=300, bbox_inches='tight')
    plt.close()
    
    print(f"\nConvergence plot saved to data/convergence_analysis.png")
    print(f"At N=50000: SE={std_errors[-1]:.6f} (mean={means[-1]:.6f})")
    print(f"SE ratio (N=500 vs N=50000): {std_errors[0]/std_errors[-1]:.1f}x")
    print(f"Expected ratio (sqrt(50000/500)): {np.sqrt(50000/500):.1f}x")

if __name__ == "__main__":
    main()
