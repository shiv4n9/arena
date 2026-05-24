# ARCTIC: Agentic Racing under Competitive Timing in Continuous-time

ARCTIC is a C++ framework that simulates and resolves high-frequency algorithmic racing games using continuous-time stochastic models. The project merges Information-Based Trading (Laughlin et al., 2014) with non-cooperative game theory to identify competitive execution boundaries in the presence of microstructural network jitter.

## Core Deliverables
- **Live OS Jitter Telemetry**: Lock-free (`std::atomic`) ring buffers (`SPSCBuffer`) that measure local RTT via UDP echo.
- **Continuous-Time Stochastic Process**: Evaluates Ornstein-Uhlenbeck mean-reverting signals.
- **Dynamic Nash Boundaries**: Compresses or expands the optimal trading frontier (action boundary) mathematically based on real-time empirical latency variance.
- **Data Visualizations**: Parameter sweeps and live adaptation plots exposing the core finding that superior execution infrastructure naturally suppresses competitor margins.

## Architecture

### 1. The Stochastic Signal (OU Process)
Asset mispricing $V_t$ is modeled as a mean-reverting Ornstein-Uhlenbeck process:
$$ dV_t = \theta (\mu - V_t) dt + \sigma_V dW_t $$
where:
- $\theta$: Mean reversion speed
- $\mu$: Long-term mean mispricing
- $\sigma_V$: Volatility of the signal
- $dW_t$: Wiener process increment

### 2. The Execution Game
If two agents observe $V_t > c$ (where $c$ is execution cost), both may trigger an action. The winner is determined by their latency distribution. We model latency $\delta \sim \text{Log-Normal}(\mu_\delta, \sigma_\delta^2)$.

### 3. Adaptive Nash Boundary
A slower or more volatile agent cannot afford to wait as long. The optimal action boundary $b_A^*$ shifts due to the "Competitive Cost" (the lost PnL from being beaten to the trade).

Let $b_{solo}$ be the boundary when acting alone. Under competitive tension, Agent A compresses its boundary:
$$ b_A^* = b_{solo} - k \cdot \sigma_V \cdot (\sigma_A - \sigma_B) \cdot \mathbb{I}(\sigma_A > \sigma_B) $$

The `LiveLatency` module actively fits empirical ping jitter to $\sigma_A$ using Welford's Online Algorithm in an independent thread, pushing updates to the execution thread lock-free.

## Build and Run
This project uses CMake.

```bash
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

### Components
- `arctic`: Core executable simulating the continuous-time agent race.
- `arctic_sweep`: Runs parameter sweeps to analyze the cost of variance.
- `test_ks`: Validates live jitter against a log-normal distribution via the Kolmogorov-Smirnov test.
- `live_adaptation_logger`: Monitors OS jitter and dynamically shifts $b_A^*$.

## Mathematical Validity (KS-Test)
The KS test validates the empirical log-normal fit of local UDP RTTs.
$$ D_n = \sup_x |F_n(x) - F(x)| $$
If $D_n > 1.36 / \sqrt{n}$ (for $\alpha = 0.05$), the null hypothesis of pure log-normal distribution is rejected. Loopback testing often rejects this due to OS scheduler anomalies, requiring heavy-tailed considerations (brutal honesty).

## References
Laughlin, A., Aguirre, A., & Grundfest, J. (2014). *Information Transmission between Financial Markets in Chicago and New York.* Financial Review, 49(2), 283-312.
