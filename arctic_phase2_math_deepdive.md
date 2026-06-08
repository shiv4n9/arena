# ARCTIC Phase 2: Game Theory & Mathematics Deep Dive

This document provides a comprehensive, academic-grade explanation of **Phase 2: Game Theory & Mathematics** within the ARCTIC (Agentic Racing under Competitive Timing in Continuous-time) framework. We will walk through the mathematical derivations, the rationale behind the modeling choices, and the exact C++ implementation details found in `math_utils.hpp` and `math_utils.cpp`.

---

## 1. The Core Problem: Latency Races in Continuous Time

In modern high-frequency trading (HFT), market participants observe a continuous stream of information. When an asset's "fair value" (the underlying signal) diverges from its quoted price on the Limit Order Book (LOB), an arbitrage opportunity arises.

However, acting on this signal involves risk:
1. **Signal Decay:** The network latency $\delta$ between the trading server and the exchange means that by the time the order arrives, the market may have already incorporated the information (the signal "mean-reverts").
2. **Competitive Risk:** Other participants are observing the same signal. If multiple agents act, the one with the lowest physical latency wins the trade. The loser may suffer adverse selection or "slippage."

The objective of Phase 2 is to solve the **optimal stopping problem**: At what exact price threshold (boundary $b^*$) should an agent submit an order, balancing the expected profit against the risks of signal decay and competitive loss?

---

## 2. Mathematical Foundations

### 2.1 The Ornstein-Uhlenbeck (OU) Signal Process

The "fair value" $V_t$ of the asset is modeled as an Ornstein-Uhlenbeck process, which is a continuous-time stochastic process that exhibits mean-reversion. It is defined by the Stochastic Differential Equation (SDE):

$$ dV_t = \theta (\mu - V_t) dt + \sigma_V dW_t $$

Where:
*   $\theta$: The speed of mean reversion (how fast the market corrects mispricing).
*   $\mu$: The long-run equilibrium mean (the baseline fair value).
*   $\sigma_V$: The volatility of the signal.
*   $dW_t$: A Wiener process (Standard Brownian motion).

**Why OU?** Unlike a standard random walk (Brownian motion) where prices drift infinitely, financial mispricings are inherently bounded. If a price deviates too far from reality, arbitrageurs push it back. The OU process mathematically guarantees this snap-back behavior.

### 2.2 Latency Modeling (The Log-Normal Distribution)

Network latency cannot be negative, and empirical measurements of packet round-trip times (RTTs) show a right-skewed "heavy tail" (due to occasional OS context switches, routing delays, or queue buildups). 

Therefore, ARCTIC models an agent's latency $L$ as a Log-Normal distribution, parameterised by the quantities we can actually **measure** — the real-world latency **mean** $m_i$ and **standard deviation** $s_i$:

$$ \ln(L_i) \sim \mathcal{N}(\mu_i,\ \sigma_i^2), \qquad \sigma_i^2 = \ln\!\left(1 + \frac{s_i^2}{m_i^2}\right), \qquad \mu_i = \ln m_i - \tfrac{1}{2}\sigma_i^2. $$

The pair $(m_i, s_i)$ is supplied **per agent**; nothing is shared implicitly. Because we parameterise directly by the mean, the expected (mean) latency is simply:

$$ \mathbb{E}[L_i] = m_i $$

and the median is $e^{\mu_i} = m_i^2/\sqrt{m_i^2 + s_i^2}$. This $(m,s)$ convention is exactly what `std::lognormal_distribution(\mu_i, \sigma_i)` draws, so the analytic math and the Monte-Carlo sampler agree by construction.

**Code Implementation:**
```cpp
// (mu, sigma) recovered from (mean, std) when sampling / computing P(win):
//   sigma^2 = log1p((s/m)^2);  mu = log(m) - sigma^2/2;  E[L] = m
// Signal decay uses the FULL distribution of L, not just its mean (see §4.1):
//   decay = E[e^{-theta L}]  (Laplace transform, computed by quadrature)
double decay = expected_signal_decay(mean_self, std_self, theta);
```

---

## 3. Deriving the Probability of Winning: $P(\text{win})$

When two agents (Agent A and Agent B) decide to trade at the same time, they enter a latency race. The winner is the agent with the smaller latency: $L_A < L_B$.

Because both $L_A$ and $L_B$ are Log-Normally distributed, the difference of their logs is exactly Normal. With $\ln(L_i) \sim \mathcal{N}(\mu_i, \sigma_i^2)$:

$$ D = \ln(L_A) - \ln(L_B) \sim \mathcal{N}\!\left(\mu_A - \mu_B,\ \sigma_A^2 + \sigma_B^2\right) $$

Agent A wins if $L_A < L_B$, i.e. $D < 0$. Using the standard normal CDF $\Phi$:

$$ P(L_A < L_B) = P(D < 0) = \Phi\!\left(\frac{\mu_B - \mu_A}{\sqrt{\sigma_A^2 + \sigma_B^2}}\right) $$

The race is decided by the median $g_i \equiv e^{\mu_i} = m_i^2/\sqrt{m_i^2 + s_i^2}$. A key — and initially counter-intuitive — consequence: at **equal mean** $m_A = m_B$, the agent with the **larger** jitter $s_i$ has the **smaller** $g_i$ and therefore wins **more** often. Rare latency spikes inflate the mean, so the typical (median) packet of the high-variance agent is actually faster. A durable win-rate edge requires a genuine **mean** advantage $m_A < m_B$, not merely tighter jitter.

**Code Implementation (`compute_p_win`):**
```cpp
double compute_p_win(double mean_self, double std_self,
                     double mean_competitor, double std_competitor) {
    double var_self = latency_log_variance(mean_self, std_self);  // ln(1+(s/m)^2)
    double var_comp = latency_log_variance(mean_competitor, std_competitor);
    double combined_std = std::sqrt(var_self + var_comp);
    if (combined_std < 1e-10) {
        if (mean_self < mean_competitor) return 1.0;
        if (mean_self > mean_competitor) return 0.0;
        return 0.5;
    }
    double mu_self = latency_log_mu(mean_self, std_self);  // ln(m) - var/2
    double mu_comp = latency_log_mu(mean_competitor, std_competitor);
    return normal_cdf((mu_comp - mu_self) / combined_std);
}
```
**Why this matters:** A genuine speed edge lives in the **mean** latency $m$ (co-location, microwave links lower $m$). Jitter $s$ alone, at equal mean, actually *helps* the noisier agent's race odds via the median effect above; it is not a clean, universal source of edge.

---

## 4. The Indifference Condition and $b^*$

To find the optimal trading boundary $b^*$, we use the **Indifference Condition**. An agent is indifferent to trading when the expected profit of the trade is exactly zero. At any signal value higher than $b^*$, the expected profit is positive, and the agent should act.

### Step 4.1: Signal Decay
If an agent acts at time $t$ when the signal is $V_t = b$, the order takes a **random** delay $\delta$ (the send-side latency) to arrive. We need the expected surviving edge, averaged over the randomness of $\delta$. For a fixed delay the OU process mean-reverts as $\mu + (b-\mu)e^{-\theta\delta}$; taking the expectation over $\delta$ and pulling out the deterministic $(b-\mu)$ term:

$$ \mathbb{E}_\delta\big[\mathbb{E}[V_{t+\delta} \mid V_t = b]\big] = \mu + (b - \mu)\,\underbrace{\mathbb{E}\big[e^{-\theta \delta}\big]}_{\displaystyle D(\theta)} $$

The factor $D(\theta) \equiv \mathbb{E}[e^{-\theta\delta}]$ is the **Laplace transform** of the latency distribution evaluated at $\theta$ — *not* $e^{-\theta\,\mathbb{E}[\delta]}$. Since $x\mapsto e^{-\theta x}$ is convex, **Jensen's inequality** gives

$$ D(\theta) = \mathbb{E}[e^{-\theta\delta}] \;\ge\; e^{-\theta\,\mathbb{E}[\delta]}, $$

with strict inequality whenever $\delta$ has any dispersion. So latency **variance** (not just the mean) materially changes the surviving signal, and it does so in the agent's favour: a noisy-but-occasionally-fast link captures more edge than its mean alone would suggest. The log-normal has **no closed-form** Laplace transform, so $D(\theta)$ is evaluated by numerical Gaussian quadrature (`expected_signal_decay`).

### Step 4.2: Expected Payoff
The agent **snipes a stale quote** anchored at the unconditional mean $\mu$ (the rational static quote a market maker posts for a mean-reverting fundamental). Crossing the half-spread costs $c$, so the *fill price* is $\mu + c$.

- **If the agent wins the race**, it lifts the stale quote and captures the transient edge. Its PnL is $V_{t+\delta} - (\mu + c)$, whose expectation is $\mu + (b-\mu)D - (\mu + c) = (b-\mu)D - c$.
- **If the agent loses**, the market maker has already repriced to the corrected fair value, so the late order crosses a fresh spread and nets exactly $-c$ (the **loser-pays** mechanism: speed is a cost even when you don't win).

Combining both outcomes, the expected payoff of committing at $V_t = b$ is

$$ \mathbb{E}[\text{Payoff}] = P(\text{win})\big[(b-\mu)D - c\big] + \big(1 - P(\text{win})\big)(-c) = P(\text{win})\,(b-\mu)\,D - c. $$

### Step 4.3: Solving for $b^*$
We set the Expected Payoff to 0 and solve for $b^*$:

$$ P(\text{win})\,(b^* - \mu)\,D - c = 0 $$
$$ (b^* - \mu)\,D = \frac{c}{P(\text{win})} $$
$$ b^* = \mu + \frac{c}{P(\text{win}) \cdot D(\theta)} $$

The numerator is the cost $c$ **alone** — not $c - \mu$. (The earlier $c-\mu$ form silently assumed $\mu = 0$; the corrected derivation above is general for any $\mu$.) If the agent acts alone, $P(\text{win}) = 1$ and the boundary is lower; in competition $P(\text{win}) < 1$ shrinks the denominator and pushes $b^*$ higher, so **competition forces a larger safety margin before trading**. Latency dispersion enters $b^*$ **twice** — through $P(\text{win})$ and through the Laplace-transform decay $D(\theta)$.

**Code Implementation (`compute_equilibrium_boundary`):**
```cpp
double compute_equilibrium_boundary(double mean_self, double std_self,
                                    double mean_competitor, double std_competitor,
                                    double theta, double mu, double cost_c) {
    double decay = expected_signal_decay(mean_self, std_self, theta); // E[e^{-theta L}]
    if (decay < 1e-10) return 1.0; 

    double p_win = compute_p_win(mean_self, std_self, mean_competitor, std_competitor);
    double effective = p_win * decay;
    if (effective < 1e-10) return 1.0; 

    return mu + cost_c / effective;   // b* = mu + c / (P(win) * D)
}
```

---

## 5. Game Theory: The Dominant Strategy Equilibrium

In game theory, a **Nash Equilibrium** is a situation where no player can gain by unilaterally changing their strategy, *given* the strategy of the other player. Finding a Nash Equilibrium usually requires solving a complex system of simultaneous equations or finding fixed points (e.g., using Kakutani's fixed-point theorem).

However, ARCTIC achieves something much stronger: a **Dominant Strategy Equilibrium**.

### Why is it a Dominant Strategy?
Look closely at the formula for Agent A's optimal boundary, $b_A^*$:
$$ b_A^* = \mu + \frac{c - \mu}{P(\text{win}) \cdot e^{-\theta \mathbb{E}[\delta_A]}} $$

The parameters required to calculate this are:
*   $\mu, c, \theta$: Exogenous market constants.
*   $m_A, P(\text{win})$: Dependent purely on the physical network latencies ($m_A, s_A, m_B, s_B$).

Notice what is missing: **Agent B's chosen boundary ($b_B$) does not appear in the equation.**

Because $b_A^*$ is independent of $b_B$, Agent A's best response is exactly the same *regardless* of what Agent B decides to do. Agent A doesn't need to guess Agent B's strategy. Playing $b_A^*$ is universally optimal. When both players play a dominant strategy, it forms a Dominant Strategy Equilibrium.

### 5.1 Numerical Proof in Code
To prove this computationally, the ARCTIC codebase includes the `verify_equilibrium_convergence` function. 

It starts with arbitrary, wildly incorrect guesses for the boundaries ($b_A = 10, b_B = 10$). It then repeatedly updates them by calculating the "best response" to the opponent's current boundary.

If the game required strategic coupling, this loop would run multiple times, slowly converging toward the Nash fixed-point. However, because it is a dominant strategy, **the error drops to zero in exactly one iteration**. 

**Code Implementation (`verify_equilibrium_convergence`):**
```cpp
bool verify_equilibrium_convergence(...) {
    double b_A = 10.0;  // Arbitrary initial guess
    double b_B = 10.0;  // Arbitrary initial guess

    for (int k = 0; k < max_iters; ++k) {
        // Best response for A: does NOT mathematically use b_B
        double b_A_new = compute_equilibrium_boundary(mu_delta_A, sig_A, mu_delta_B, sig_B, ...);
        // Best response for B: does NOT mathematically use b_A
        double b_B_new = compute_equilibrium_boundary(mu_delta_B, sig_B, mu_delta_A, sig_A, ...);

        double err_A = std::abs(b_A_new - b_A);
        double err_B = std::abs(b_B_new - b_B);

        b_A = b_A_new; b_B = b_B_new;
        
        if (err_A < tol && err_B < tol) break; // Exits on iteration 1
    }
    // ...
}
```

---

## 6. Statistical Validation & Infrastructure Mathematics

Phase 2 isn't just about agent logic; it also provides the mathematical tools to evaluate the performance of the simulation rigorously.

### 6.1 Welford's Algorithm for the Sharpe Ratio
The Sharpe Ratio is a measure of risk-adjusted return: $S = \frac{\text{Mean}(PnL)}{\text{StdDev}(PnL)}$. 

Naively calculating variance requires two passes over the data (one to find the mean, one to sum the squared differences). When running millions of Monte Carlo paths, iterating twice destroys L1 cache coherency and tanks performance.

ARCTIC uses **Welford's Online Algorithm**, which computes the exact variance in a single pass over the data. It does this by keeping a running update of the mean and the sum of squares of differences from the current mean (`m2`). It is also significantly more numerically stable against floating-point catastrophic cancellation.

**Code Implementation (`compute_sharpe_ratio`):**
```cpp
double compute_sharpe_ratio(const double* pnl_per_path, size_t n) {
    double mean = 0.0;
    double m2 = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double delta = pnl_per_path[i] - mean;
        mean += delta / static_cast<double>(i + 1);
        double delta2 = pnl_per_path[i] - mean;
        m2 += delta * delta2;
    }
    double variance = m2 / static_cast<double>(n - 1);
    return mean / std::sqrt(variance);
}
```

### 6.2 Validating the OU Kernel: Lag-1 Autocorrelation
How do we mathematically prove that Phase 1 (the OU Sampler) isn't suffering from floating-point drift? By measuring the empirical Lag-1 Autocorrelation ($\rho_1$) of the generated price paths and comparing it to the theoretical value.

For an Exact OU process with timestep $\Delta t$, the theoretical correlation between $V_t$ and $V_{t+1}$ is:
$$ \rho_{1(\text{theoretical})} = e^{-\theta \Delta t} $$

The codebase calculates the empirical sample covariance divided by the sample variance in a single optimized pass. If the simulator is mathematically sound, `empirical_rho1` will perfectly match `theoretical_rho1`.

---

## Summary of Phase 2

Phase 2 represents the rigid mathematical backbone of the ARCTIC engine. By deriving exact continuous-time integrals for signal decay, formulating log-normal latency race probabilities, and utilizing Welford's single-pass algorithms, it ensures that the simulation runs without discretization bias, without L1 cache-busting multi-pass statistical loops, and perfectly adheres to non-cooperative game theory. This mathematical purity is what allows the C++ engine to resolve millions of paths per second accurately.
