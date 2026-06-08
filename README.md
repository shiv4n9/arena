# ARCTIC: Agentic Racing under Competitive Timing in Continuous-time

ARCTIC is a C++ framework that simulates and resolves high-frequency algorithmic racing games using continuous-time stochastic models. The project merges Information-Based Trading (Laughlin et al., 2014) with non-cooperative game theory to identify competitive execution boundaries in the presence of microstructural network jitter.

## Core Deliverables
- **Dominant Strategy Equilibrium Solver**: Derives competitive action boundaries from the indifference condition under signal decay and latency race probability, with formal proof of the dominant strategy property.
- **Limit Order Book (LOB)**: Price-time priority matching engine with flat-array price levels, intrusive doubly-linked order lists, and a pre-allocated pool allocator. Zero heap allocation during add/cancel/match. Integrated into the Monte Carlo simulation — agents now execute via market orders against resting LOB liquidity.
- **Live OS Jitter Telemetry**: Lock-free (`std::atomic`) ring buffers (`SPSCBuffer`) that measure local RTT via UDP echo, using `epoll` (Linux) / `select` (Windows) for non-blocking timeout-based measurement. Timed via `RDTSCP` (not `clock_gettime`).
- **Custom Memory Allocators**: Bump-pointer arena (page-aligned, pre-faulted) for per-path simulation data. Intrusive free-list pool allocator for LOB order storage. Zero `malloc`/`free` in the hot path.
- **Continuous-Time Stochastic Process**: Exact-kernel Ornstein-Uhlenbeck mean-reverting signals (zero discretization bias).
- **Browser-Native Visualization**: C++ → WebAssembly with SharedArrayBuffer, WebGL rendering, and live KaTeX mathematics.
- **Statistical Instrumentation**: Per-run Sharpe ratios, stopping time distributions, lag-1 autocorrelation validation, and Monte Carlo timing benchmarks.

## Benchmark Results

Measured via [Google Benchmark](https://github.com/google/benchmark) v1.8.3 with `RDTSCP` hardware timestamps.  
CPU: 12th Gen Intel (12 cores @ 2.61 GHz) | L1d: 48 KiB × 6 | L2: 1280 KiB × 6 | L3: 12288 KiB

### SPSC Ring Buffer Latency Distribution

| Metric | Single-Thread Push/Pop | Cross-Thread |
|--------|----------------------|---------------|
| **p50** | **46 ns** | 72 μs |
| **p90** | **61 ns** | 73 μs |
| **p99** | **67 ns** | 1.52 ms |
| **p99.9** | **84 ns** | 1.53 ms |
| min | 35 ns | — |
| max | 133 ns (OS jitter) | — |

Cross-thread p50 is dominated by cache-line ownership transfer (MESI protocol `Invalid → Shared` transitions) between producer and consumer cores. The 72 μs includes L3 round-trip + store buffer drain.

### Limit Order Book (LOB) Latency

| Operation | p50 | p90 | p99 | p99.9 |
|-----------|-----|-----|-----|-------|
| **Add Order** | **94 ns** | **140 ns** | **162 ns** | **227 ns** |
| **Match Market** | **32 ns** | **62 ns** | **158 ns** | **922 ns** |

O(1) add via intrusive list append. O(k) match where k = number of fills. All operations use the pre-allocated `PoolAllocator` — zero heap allocation.

### Arena Allocator vs `std::vector` (Cache-Miss Analysis)

| Allocator | Time per 1000-double alloc | Speedup |
|-----------|--------------------------|----------|
| `ArenaAllocator` | **1.65 ns** | **71×** |
| `std::vector<double>` | 117 ns | 1× (baseline) |

**Why 71× faster — the cache-miss story:**

1. **Arena** (`ArenaAllocator::allocate`): A single pointer bump (`offset_ += size`). The backing store is page-aligned (`std::align_val_t{4096}`) and pre-faulted (`memset` at construction). Every allocation hits L1d because the bump pointer is monotonically advancing through contiguous, pre-warmed cache lines. **Zero TLB misses, zero page faults, zero syscalls.**

2. **`std::vector`**: Each construction calls `operator new` → `malloc` → kernel `brk`/`mmap` (amortized). The allocator metadata (free-list headers, size classes) lives on different cache lines than the data, causing **L1d capacity evictions**. Deallocation dirties additional cache lines for coalescing. On repeated alloc/free cycles, the DRAM access pattern becomes non-sequential, degrading hardware prefetcher effectiveness.

3. **Measurement**: On this CPU (48 KiB L1d per core), the arena's 8 KB working set (1000 doubles) fits entirely in L1d with room for the bump pointer metadata on the same cache line. `std::vector`'s allocator metadata + free-list pointers push the working set past L1d capacity, forcing L2 round-trips (~4 ns each on this microarchitecture).

### RDTSCP Overhead

| Operation | Latency |
|-----------|----------|
| `RDTSCP` read | **23.5 ns** |
| Calibrated frequency | 2.6112 GHz |

Calibration: 5 rounds × 50 ms, median-filtered to reject context switch outliers.

## Architecture

### 1. The Stochastic Signal (Exact OU Kernel)
Asset mispricing $V_t$ is modeled as a mean-reverting Ornstein-Uhlenbeck process:
$$ dV_t = \theta (\mu - V_t) dt + \sigma_V dW_t $$

We use the **exact analytical transition density** (not Euler-Maruyama):
$$ V_{t+\Delta t} \sim \mathcal{N}\left(\mu + (V_t - \mu)e^{-\theta \Delta t},\; \frac{\sigma_V^2}{2\theta}(1 - e^{-2\theta \Delta t})\right) $$

This eliminates discretization bias entirely. The stationary distribution is $V_\infty \sim \mathcal{N}(\mu, \sigma_V^2 / 2\theta)$. The simulator validates the exact kernel by checking that the empirical lag-1 autocorrelation matches the theoretical value $\rho_1 = e^{-\theta \Delta t}$.

### 2. The Execution Game
The market maker posts a static quote anchored at the unconditional mean $\mu$ (the rational quote for a mean-reverting fundamental), so the offer sits at $\mu + c$ where $c$ is the half-spread. When the signal $V_t$ deviates far enough, an agent **snipes** that stale quote. Multiple agents may trigger at once; the winner is the one whose order arrives first. We model latency $L \sim \text{Log-Normal}$ and parameterise it by the quantities we can actually **measure**: the real-world latency **mean** $m_i$ and **standard deviation** $s_i$ per agent. The underlying-normal parameters are recovered analytically:
$$ \sigma_i^2 = \ln\!\left(1 + \frac{s_i^2}{m_i^2}\right), \qquad \mu_i = \ln m_i - \tfrac{1}{2}\sigma_i^2, \qquad \ln L_i \sim \mathcal{N}(\mu_i,\ \sigma_i^2). $$
This is the single convention used everywhere in the code — and it is exactly what `std::lognormal_distribution(μ_i, σ_i)` draws, so the analytic math and the Monte-Carlo sampler agree by construction. The pair $(m_i, s_i)$ is per agent; nothing is assumed shared.

**Information structure (send-side latency only).** Each agent observes $V_t$ in **real time**; the drawn latency $L$ delays only the *arrival* of its order. This is the Budish–Cramton–Shim assumption and avoids double-counting the delay (observe-late *and* execute-late). The winner lifts the stale $\mu$-quote and books $V_{t+L} - (\mu + c)$; the loser arrives after the maker has repriced and books exactly $-c$ (**loser-pays**: speed is a cost even when you lose).

### 3. Signal Decay Under Latency
When an agent acts at $V_t = b$, the OU signal mean-reverts during the **random** delay $L$. Averaging over $L$, the surviving edge is governed by the **Laplace transform** of the latency, not by $e^{-\theta\,\mathbb{E}[L]}$:
$$ \mathbb{E}\big[V_{t+L} \mid V_t = b\big] = \mu + (b - \mu) \cdot D(\theta), \qquad D(\theta) \equiv \mathbb{E}\big[e^{-\theta L}\big]. $$
Since $e^{-\theta x}$ is convex, **Jensen's inequality** gives $D(\theta) \ge e^{-\theta m_i}$, with strict inequality whenever $s_i > 0$ — so latency **variance** (not just the mean) raises the surviving signal. The log-normal Laplace transform has no closed form, so `expected_signal_decay()` evaluates it by Gaussian quadrature (no modelling approximation, only ~1e-12 numerical error).

### 4. Latency Race Win Probability
The probability that agent A wins the latency race follows directly from log-normality, with **no** equal-means assumption. Let $D = \ln L_A - \ln L_B \sim \mathcal{N}(\mu_A - \mu_B,\ \sigma_A^2 + \sigma_B^2)$, and agent A wins iff $D < 0$:
$$ P(L_A < L_B) = \Phi\!\left(\frac{\mu_B - \mu_A}{\sqrt{\sigma_A^2 + \sigma_B^2}}\right), \quad \text{where } \mu_i = \ln m_i - \tfrac12\ln\!\Big(1+\tfrac{s_i^2}{m_i^2}\Big). $$
The race is decided by $g_i \equiv e^{\mu_i} = m_i^2/\sqrt{m_i^2 + s_i^2}$ (the log-normal **median**). A key — and initially counter-intuitive — consequence: at **equal mean** $m_A = m_B$, the agent with the *larger* jitter $s_i$ has the *smaller* $g_i$ and therefore wins **more** often. Rare latency spikes inflate the mean, so the typical (median) packet of the high-variance agent is actually faster. A durable win-rate edge requires a genuine **mean** advantage $m_A < m_B$, not merely tighter jitter.

### 5. Equilibrium Boundary Derivation
The optimal action boundary $b_A^*$ satisfies the **indifference condition**: the expected sniping payoff (winner captures the decayed edge, loser pays $-c$) equals zero at the boundary.
$$ P(\text{win}) \cdot (b_A^* - \mu) \cdot D(\theta) - c = 0 $$

Solving:
$$ b_A^* = \mu + \frac{c}{P(\text{win}) \cdot D(\theta)} $$

The numerator is the cost $c$ **alone** (the earlier $c-\mu$ form silently assumed $\mu = 0$). The cost $c$ is **unified** with the LOB half-spread, so the analytic boundary is the exact zero-EV threshold of the simulated payoff. Latency **std** enters $b_A^*$ **twice** — through $P(\text{win})$ *and* through the Laplace-transform decay $D(\theta)$.

### 6. Dominant Strategy Equilibrium (Proof)

**Theorem.** The strategy profile $(b_A^*, b_B^*)$ constitutes a **dominant strategy equilibrium**.

**Proof.** Observe that $b_A^*$ is a function of $(m_A, s_A, m_B, s_B, \theta, \mu, c)$ — all exogenous parameters of the game. Crucially, $b_A^*$ does **not** depend on $b_B$ (the opponent's chosen boundary). Therefore:

1. $b_A^*$ is agent A's best response **regardless** of what boundary agent B selects. By definition, this makes $b_A^*$ a **dominant strategy** for agent A.
2. By the symmetric argument, $b_B^*$ is a dominant strategy for agent B.
3. A strategy profile where every player plays a dominant strategy is a **dominant strategy equilibrium**, which is the strongest solution concept in non-cooperative game theory — it implies Nash equilibrium, and is also robust to trembling-hand perturbations.

**Numerical verification.** The function `verify_equilibrium_convergence()` checks the **indifference identity** that defines each boundary, $\;P(\text{win})\cdot(b^* - \mu)\cdot D(\theta) - c = 0\;$ (to $\sim10^{-9}$). This is a substantive algebraic check on the solved boundary — it would fail if the boundary formula, the decay $D(\theta)$, or $P(\text{win})$ were mutually inconsistent — rather than a tautological best-response loop that converges trivially because $b^*$ ignores the opponent. The dominant-strategy property is then reported as a corollary: $b^*$ is independent of $b_B$ by construction.

**Model limitation.** This dominance arises because agents compete purely on execution speed — the probability of competition is not conditioned on the opponent's boundary. In a richer model where agents can infer the competitor's strategy (e.g., through market impact or information leakage), the best-response *would* depend on $b_B$, requiring Banach contraction or Kakutani fixed-point arguments to establish equilibrium existence.

The `LiveLatency` module actively fits the empirical ping mean $m_A$ and jitter $s_A$ using Welford's Online Algorithm in an independent thread, pushing updates to the execution thread lock-free via cache-line-aligned atomics with `release`/`acquire` memory ordering.

## Future Scope: A Second, More Complete Model (Budish–Cramton–Shim)

The current engine models one half of the high-frequency latency arms race: the **liquidity taker / sniper**. It asks "given a mispricing signal and a latency distribution, when should I fire, and how often do I win the race?" This is deliberately the side with a clean, provable answer — the boundary $b_A^*$ is a *dominant strategy* with a one-line closed form.

The natural next model is the **other half of the same phenomenon**: the **market maker's adverse-selection problem**, formalised by Budish, Cramton & Shim (2015, *"The High-Frequency Trading Arms Race: Frequent Batch Auctions as a Market Design Response,"* Quarterly Journal of Economics). This is not a refinement of the current model — it is a **strictly more competent, second model** that should live alongside it, sharing only the OU signal generator and the latency primitives.

### What the second model adds

A **Glosten–Milgrom-style Bayesian market maker** who posts a bid–ask spread and knows that whenever the value $V_t$ jumps, snipers will try to pick off his now-stale quote *before* he can cancel it. The MM cannot win every cancellation race, so he prices the expected sniping loss into his spread. The headline objects become:

1. **Stale-quote sniping loss.** When $V_t$ moves by $\Delta$, the MM's resting quote is mispriced. A sniper who wins the race against the MM's cancel captures (a fraction of) $\Delta$. The MM's expected loss per repricing event is
   $$ \mathcal{L}_{\text{snipe}} \;=\; P(\text{sniper beats cancel}) \cdot \mathbb{E}[\,\text{adverse move} \mid \text{jump}\,]. $$
   Here $P(\text{sniper beats cancel})$ is exactly the latency-race probability already implemented in `compute_p_win`, but now run **MM-vs-sniper** rather than sniper-vs-sniper.

2. **Equilibrium spread as a fixed point.** The MM widens the half-spread $\lambda$ until spread revenue from uninformed ("noise") flow covers the sniping tax:
   $$ \underbrace{\lambda \cdot \phi_{\text{noise}}}_{\text{revenue from noise traders}} \;=\; \underbrace{\mathcal{L}_{\text{snipe}}(\lambda) \cdot \phi_{\text{snipe}}}_{\text{losses to snipers}}. $$
   Because $\mathcal{L}_{\text{snipe}}$ itself depends on how aggressively snipers act (which depends on $\lambda$), this is a genuine **fixed-point problem** — solved by Banach contraction iteration, *not* a closed form.

3. **The market-design result.** The model's punchline is that this sniping tax is **mechanical and irreducible** in continuous-time trading: no spread fully eliminates it, because the race is a coin-flip on speed, not on information. Budish et al. use this to argue for **frequent batch auctions** (discretising time into short uniform-price auctions), which convert the speed race into a price race and collapse the tax. The second model would reproduce this by comparing the equilibrium spread under continuous trading vs. under batched auctions.

### Why it is a separate model, not a patch

- **It breaks the dominant strategy on purpose.** Once $\lambda$ is endogenous, the sniper's payoff is $P(\text{win})\cdot(\text{decayed signal} - \lambda)$ with $\lambda$ a function of sniper behaviour. The best response now depends on the opponent's strategy, so the equilibrium must be established by a fixed-point argument (Banach / Kakutani) and verified by an iterative solver — the current closed-form boundary and its one-shot indifference check (`verify_equilibrium_convergence`) would no longer apply, **by design**.
- **It requires a different information structure.** Glosten–Milgrom's mechanism needs the MM to be *uninformed* and to learn from order flow via Bayesian updating. The current model exposes $V_t$ to everyone. The second model must therefore **hide $V_t$ from the MM** and have him infer it from informed-order arrivals — a structurally different setup.
- **The OU process must stay.** It is tempting to make $V_t$ a martingale to fit the classic GM derivation, but that would kill the $D(\theta) = \mathbb{E}[e^{-\theta L}]$ signal-decay term that is the entire economic tension of the current boundary. The two models share the *same* OU primitive precisely so that they remain two lenses on one phenomenon.

### Suggested staging

| Stage | Deliverable | Solver | Risk |
|-------|-------------|--------|------|
| 0 (read-only) | **Break-even spread diagnostic**: report the minimum half-spread $\lambda_{\min}$ that covers the current sniping loss given live $(m, s, \theta)$. Zero changes to the existing core. | none (one formula) | minimal |
| 1 | Standalone `BudishMarketMaker` with hidden value + Bayesian quote updates. | Banach fixed-point | medium |
| 2 | Continuous-vs-batch-auction comparison reproducing the irreducible-tax result. | fixed-point + auction clearing | higher |

Presented this way, ARCTIC tells a stronger, more honest story: a **taker's dominant strategy** (closed form, this repo) and a **maker's adverse-selection spread** (fixed point, future model) — two rigorous views of the same continuous-time latency race.

## Disclaimers

**Loopback Latency Proxy.** The UDP loopback measurement (`127.0.0.1`) captures OS scheduler jitter — context switches, timer interrupts, cache pressure — producing a coefficient of variation $s/m \approx 0.3\text{--}1.0$. Real co-location execution has a tighter $s/m \approx 0.05\text{--}0.2$. The live adaptation demo holds the latency **mean** at a fixed model scale and lets the measured loopback **dispersion** (CV) drive the equilibrium boundary, as a **pedagogical proxy** for how boundaries respond to variance shifts. It does not claim to measure production-grade network latency.

**Web Frontend.** The WebAssembly dashboard uses a synthetic latency random walk for visualization purposes. It does not receive live socket measurements from the host OS.

**Model Scope.** This is a continuous-time stochastic game simulation, not a production trading system. It does not implement FIX/ITCH protocol parsing, kernel-bypass networking, or exchange connectivity.

## Build and Run
This project uses CMake. The build system defaults to **Release mode** — timing Debug builds produces meaningless results because the optimizer eliminates dead stores, inlines functions, and vectorizes hot loops.

```bash
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

### Components
- `arctic`: Core executable. Runs the continuous-time agent race with live latency adaptation and LOB execution. Outputs equilibrium verification, Sharpe ratios, stopping time distributions, OU autocorrelation validation, LOB fill metrics, and per-run timing benchmarks.
- `arctic_sweep`: Runs parameter sweeps across variance gap configurations. Outputs timing, Sharpe ratios, and mean stopping times per sweep step.
- `test_ks`: Validates live jitter against a log-normal distribution via the Lilliefors test (KS statistic with the parameter-estimation–corrected critical value). Cross-platform (Windows/Linux).
- `live_adaptation_logger`: Monitors OS jitter and dynamically shifts $b_A^*$ over a 60-second window, logging to CSV.
- `bench_spsc`: Google Benchmark microbenchmarks for SPSC buffer, LOB, arena allocator, and RDTSCP overhead.

### Benchmark Build
```bash
cmake -DARCTIC_BUILD_BENCHMARKS=ON ..
cmake --build . --config Release --target bench_spsc
./Release/bench_spsc --benchmark_format=console
```

### Web Visualization
```bash
cd web
npm install
npm run dev
```
Opens a browser-native dashboard with:
- Real-time WebGL rendering of OU signal and equilibrium boundaries from WASM heap pointers
- Live telemetry: $V(t)$, $b_A^*$, $b_B^*$, $P(\text{win})$, signal decay
- KaTeX-rendered mathematical derivations
- Interactive latency variance controls via SharedArrayBuffer

## Mathematical Validity (Lilliefors Test)
The goodness-of-fit test validates the empirical log-normal fit of local UDP RTTs using the two-sided supremum statistic
$$ D_n = \sup_x |F_n(x) - F(x)| = \max_i \max\!\left(\left|\tfrac{i}{n} - F(x_{(i)})\right|,\ \left|\tfrac{i-1}{n} - F(x_{(i)})\right|\right). $$
Because the log-normal parameters $(\mu, \sigma)$ are estimated **from the same sample** (MLE), the classical Kolmogorov–Smirnov critical value $1.36/\sqrt{n}$ is **invalid** — it assumes a fully specified null and is far too conservative. The correct test for a fitted (log-)normal is **Lilliefors'**, whose asymptotic $5\%$ critical value is $\approx 0.895/\sqrt{n}$. If $D_n > 0.895/\sqrt{n}$ the null of log-normality is rejected. **Loopback testing frequently rejects this** due to OS scheduler anomalies producing heavy tails that the symmetric log-normal cannot capture. This is expected — a mixture model or heavy-tailed distribution (e.g., log-$t$) would be more appropriate for production use.

## Performance Profiling

Measuring performance separates discussion from engineering. ARCTIC provides scripts to build the engine with debug symbols and optimizations enabled (`RelWithDebInfo`), which is required for meaningful profiling.

To profile on Windows:
```powershell
.\scripts\run_profiler.ps1
```

### Analyzing the Results
1. **Visual Studio Profiler**: Open the generated `arctic.sln` in `build_profile`, go to **Debug > Performance Profiler (Alt+F2)**, and select **CPU Usage**. This will generate a Flame Graph showing exactly where cycles are spent.
2. **Intel VTune**: If installed, run `vtune -collect hotspots` against the executable.
3. **What to look for**:
   - Cache misses in the `OrderBook` flat arrays versus the `PoolAllocator` linked list traversal.
   - SPSC Buffer cross-core latency (MESI cache-line transfers should be ~50-100ns when properly pinned).

## References
Laughlin, A., Aguirre, A., & Grundfest, J. (2014). *Information Transmission between Financial Markets in Chicago and New York.* Financial Review, 49(2), 283-312.
