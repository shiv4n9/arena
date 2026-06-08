import { useState, useEffect, useRef, useCallback } from 'react';
import { Tex, TexBlock } from './Tex';
import { WebGLCanvas } from './WebGLCanvas';
import { RaceGame } from './RaceGame';
import { ArenaLogo, ArenaMark } from './ArenaLogo';
import CursorSparkles from './CursorSparkles';
// @ts-ignore
import initWasm from './arctic_wasm.js';
// @ts-ignore
import wasmUrl from './arctic_wasm.wasm?url';

/* =====================================================================
   ARENA — Agentic Racing Engine with Network Analysis
   ===================================================================== */

// --- Reveal-on-scroll ---
function useReveal() {
  const ref = useRef<HTMLDivElement>(null);
  useEffect(() => {
    const el = ref.current;
    if (!el) return;
    const obs = new IntersectionObserver(
      ([e]) => { if (e.isIntersecting) el.classList.add('visible'); },
      { threshold: 0.12 }
    );
    obs.observe(el);
    return () => obs.disconnect();
  }, []);
  return ref;
}

function Reveal({ className = '', delay = 0, children }: { className?: string; delay?: number; children: React.ReactNode }) {
  const ref = useReveal();
  const d = delay > 0 ? ` reveal-delay-${delay}` : '';
  return <div ref={ref} className={`reveal${d} ${className}`}>{children}</div>;
}

function SectionLabel({ n, title }: { n: string; title: string }) {
  return (
    <div className="mb-12">
      <div className="flex items-center gap-3 mb-3">
        <span className="font-mono text-xs tracking-widest" style={{ color: 'var(--color-lime)' }}>{n}</span>
        <span className="h-px w-10" style={{ background: 'var(--color-border)' }} />
        <span className="font-mono text-[10px] uppercase tracking-[0.25em] text-text-tertiary">ARENA</span>
      </div>
      <h2 className="text-3xl md:text-5xl font-semibold tracking-tight text-text-primary">{title}</h2>
    </div>
  );
}

function Metric({ label, value, color }: { label: string; value: string; color?: string }) {
  return (
    <div className="metric-card">
      <div className="text-[10px] uppercase tracking-wider text-text-tertiary font-mono mb-1">{label}</div>
      <div className="font-mono text-sm tabular-nums" style={{ color: color || 'var(--color-text-primary)' }}>{value}</div>
    </div>
  );
}

// --- Animated hero backdrop: packets racing across latency lanes ---
function RaceField() {
  const lanes = [0.14, 0.26, 0.38, 0.5, 0.62, 0.74, 0.86];
  const colors = ['var(--color-lime)', 'var(--color-vermilion)', 'var(--color-periwinkle)'];
  return (
    <div className="absolute inset-0 overflow-hidden pointer-events-none" style={{ opacity: 0.55 }}>
      {/* faint horizontal lanes */}
      {lanes.map((y, i) => (
        <div key={`l${i}`} className="absolute left-0 right-0" style={{ top: `${y * 100}%`, height: 1, background: 'var(--color-border-subtle)' }} />
      ))}
      {/* racing packets */}
      {Array.from({ length: 22 }).map((_, i) => {
        const y = lanes[i % lanes.length];
        const c = colors[i % colors.length];
        const dur = 4 + (i % 5) * 1.6;
        const delay = -(i * 0.9);
        return (
          <div
            key={`r${i}`}
            className="racer"
            style={{
              top: `calc(${y * 100}% - 1px)`,
              background: `linear-gradient(90deg, transparent, ${c})`,
              boxShadow: `0 0 10px ${c}`,
              animationDuration: `${dur}s`,
              animationDelay: `${delay}s`,
            }}
          />
        );
      })}
    </div>
  );
}

// --- Nav ---
function Nav() {
  const links = [
    ['race', 'The Race'],
    ['engine', 'Engine'],
    ['math', 'Mathematics'],
    ['live', 'Live'],
    ['simulator', 'Simulator'],
    ['network', 'Network'],
  ];
  return (
    <nav className="fixed top-0 left-0 right-0 z-50" style={{ background: 'rgba(244,238,225,0.72)', backdropFilter: 'blur(12px)', borderBottom: '1px solid var(--color-border-subtle)' }}>
      <div className="max-w-6xl mx-auto px-6 h-14 flex items-center justify-between">
        <a href="#top" className="text-text-primary hover:opacity-80 transition-opacity">
          <ArenaLogo size={26} />
        </a>
        <div className="hidden md:flex items-center gap-6">
          {links.map(([id, label]) => (
            <a key={id} href={`#${id}`} className="font-mono text-[11px] uppercase tracking-wider text-text-tertiary hover:text-text-primary transition-colors">
              {label}
            </a>
          ))}
        </div>
        <a href="#simulator" className="font-mono text-[11px] uppercase tracking-wider px-3 py-1.5 rounded-lg transition-colors" style={{ color: 'var(--color-lime)', border: '1px solid var(--color-lime-dim)' }}>
          ▶ Play
        </a>
      </div>
    </nav>
  );
}

// =============================================================================
export default function App() {
  const [wasmEngine, setWasmEngine] = useState<any>(null);
  const [wasmMemory, setWasmMemory] = useState<any>(null);
  const [liveSigma, setLiveSigma] = useState(0.12);
  const [isStressed, setIsStressed] = useState(false);
  const [metrics, setMetrics] = useState({
    currentV: 0, boundaryA: 1, boundaryB: 1, pWin: 0.5,
    signalDecay: 1, signalMean: 0, signalVar: 0, theoreticalVar: 0.25,
  });
  const latencyBufferRef = useRef<Float32Array | null>(null);
  const gameActiveRef = useRef(false);

  // Boot WASM
  useEffect(() => {
    (async () => {
      try {
        const mod = await initWasm({ locateFile: (p: string) => (p.endsWith('.wasm') ? wasmUrl : p) });
        const engine = new mod.ArcticWasmEngine(1000, 0.01);
        const ptr = mod._malloc(4);
        engine.bind_latency_buffer(ptr);
        latencyBufferRef.current = new Float32Array(mod.wasmMemory.buffer, ptr, 1);
        latencyBufferRef.current[0] = 0.12;
        setWasmMemory(mod.wasmMemory);
        setWasmEngine(engine);
      } catch (err) {
        console.error('WASM init failed:', err);
      }
    })();
  }, []);

  // Stress loop for the live telemetry section. Yields to the race when active.
  useEffect(() => {
    const id = setInterval(() => {
      const buf = latencyBufferRef.current;
      if (!buf || gameActiveRef.current) return;
      const target = isStressed ? 1.1 : 0.12;
      const next = buf[0] + (target - buf[0]) * 0.18;
      buf[0] = Math.abs(next - target) < 0.004 ? target : next;
      setLiveSigma(buf[0]);
    }, 80);
    return () => clearInterval(id);
  }, [isStressed]);

  // Metrics readback
  useEffect(() => {
    if (!wasmEngine) return;
    const id = setInterval(() => {
      try {
        setMetrics({
          currentV: wasmEngine.get_current_v(),
          boundaryA: wasmEngine.get_boundary_a_val(),
          boundaryB: wasmEngine.get_boundary_b_val(),
          pWin: wasmEngine.get_p_win(),
          signalDecay: wasmEngine.get_signal_decay(),
          signalMean: wasmEngine.get_signal_mean(),
          signalVar: wasmEngine.get_signal_variance(),
          theoreticalVar: wasmEngine.get_theoretical_variance(),
        });
      } catch { /* not ready */ }
    }, 180);
    return () => clearInterval(id);
  }, [wasmEngine]);

  const stressDown = useCallback(() => setIsStressed(true), []);
  const stressUp = useCallback(() => setIsStressed(false), []);
  const onGameActive = useCallback((a: boolean) => { gameActiveRef.current = a; }, []);

  return (
    <>
      <CursorSparkles />
      <Nav />
      <div className="snap-container" id="top">

        {/* ============================== HERO ============================== */}
        <section className="snap-section flex-col px-6 scanline" style={{ overflow: 'hidden' }}>
          <RaceField />
          <div className="relative z-10 max-w-4xl text-center">
            <Reveal>
              <div className="tag mb-8 mx-auto">
                <span className="tag-dot" />
                Agentic Racing · Network Analysis
              </div>
            </Reveal>
            <Reveal delay={1}>
              <div className="flex justify-center mb-4"><ArenaMark size={64} className="float-slow" /></div>
              <h1 className="text-6xl md:text-[8.5rem] font-bold tracking-tighter leading-[0.9] mb-6">
                <span className="grad-text">ARENA</span>
              </h1>
            </Reveal>
            <Reveal delay={2}>
              <p className="text-lg md:text-2xl text-text-primary font-light leading-relaxed max-w-2xl mx-auto mb-5">
                Two agents watch the same price signal. When it drifts far enough to snipe a stale quote, they <span style={{ color: 'var(--color-lime)' }}>race</span>. The faster, steadier network wins the fill — the other <span style={{ color: 'var(--color-vermilion)' }}>pays the spread</span>.
              </p>
            </Reveal>
            <Reveal delay={3}>
              <p className="text-sm text-text-tertiary font-mono max-w-xl mx-auto mb-10">
                A C++17 latency-race engine — exact Ornstein–Uhlenbeck signal, game-theoretic sniping boundaries, log-normal network analysis — compiled to WebAssembly, rendered live at 60fps.
              </p>
            </Reveal>
            <Reveal delay={4}>
              <div className="flex items-center justify-center gap-4 flex-wrap">
                <a href="#simulator" className="btn-race">▶ Enter the Arena</a>
                <a href="#math" className="btn-ghost">Read the model</a>
              </div>
            </Reveal>
          </div>
          <Reveal delay={5} className="absolute bottom-8 text-text-tertiary text-[10px] font-mono tracking-[0.3em]">
            SCROLL
          </Reveal>
        </section>

        {/* ============================== THE RACE ============================== */}
        <section id="race" className="snap-section flex-col px-6 py-24">
          <div className="max-w-5xl w-full">
            <Reveal><SectionLabel n="01" title="What is an agentic race?" /></Reveal>
            <div className="grid grid-cols-1 md:grid-cols-3 gap-5">
              {[
                ['Signal drifts', 'var(--color-periwinkle)',
                  'A mid-price signal mean-reverts around fair value as an exact Ornstein–Uhlenbeck process. Agents observe it continuously, in real time.'],
                ['Snipe the stale quote', 'var(--color-lime)',
                  'When the signal crosses an agent\'s trigger boundary b*, the resting quote is mispriced. Both agents fire an order to snipe it for V − fill.'],
                ['Latency decides', 'var(--color-vermilion)',
                  'Orders arrive after a random network delay. Whoever\'s packet lands first wins the fill; the loser pays the half-spread. The race is won in the tail of the latency distribution.'],
              ].map(([title, color, body], i) => (
                <Reveal key={i} delay={i + 1}>
                  <div className="card card-accent p-6 h-full">
                    <div className="font-mono text-3xl font-bold mb-4 ghost-num">0{i + 1}</div>
                    <div className="font-mono text-xs uppercase tracking-wider mb-3" style={{ color: color as string }}>{title}</div>
                    <p className="text-sm text-text-secondary leading-relaxed">{body}</p>
                  </div>
                </Reveal>
              ))}
            </div>
            <Reveal delay={4}>
              <div className="card p-6 mt-5 flex flex-col md:flex-row items-center gap-6">
                <div className="font-mono text-xs uppercase tracking-widest text-text-tertiary md:w-40 flex-shrink-0">Information structure</div>
                <p className="text-sm text-text-secondary leading-relaxed flex-1">
                  Send-side latency only (Budish–Cramton–Shim): the agent sees the signal instantly, but the network delays only the <em>arrival</em> of the order. The signal mean-reverts during transit, so latency erodes the very edge it is racing for.
                </p>
              </div>
            </Reveal>
          </div>
        </section>

        {/* ============================== THE ENGINE ============================== */}
        <section id="engine" className="snap-section flex-col px-6 py-24">
          <div className="max-w-5xl w-full">
            <Reveal><SectionLabel n="02" title="What's actually built" /></Reveal>
            <div className="grid grid-cols-1 md:grid-cols-3 gap-5">
              <Reveal delay={1}>
                <div className="card card-accent p-6 h-full">
                  <div className="font-mono text-xs uppercase tracking-wider mb-3" style={{ color: 'var(--color-lime)' }}>Matching Engine</div>
                  <p className="text-sm text-text-secondary leading-relaxed mb-4">
                    Flat-array limit order book indexed by integer tick. Orders live in a pool with intrusive doubly-linked lists per price level — no <code>std::map</code>, no tree walk. O(1) add and cancel.
                  </p>
                  <div className="text-xs font-mono text-text-tertiary">65,536 order pool · 10,000 levels</div>
                </div>
              </Reveal>
              <Reveal delay={2}>
                <div className="card card-accent p-6 h-full">
                  <div className="font-mono text-xs uppercase tracking-wider mb-3" style={{ color: 'var(--color-periwinkle)' }}>OU Signal Engine</div>
                  <p className="text-sm text-text-secondary leading-relaxed mb-4">
                    Continuous-time Ornstein–Uhlenbeck signal sampled from the <em>exact</em> transition kernel — not Euler–Maruyama. Precomputed decay and conditional variance; Welford online statistics.
                  </p>
                  <div className="text-xs font-mono text-text-tertiary">Zero discretization bias</div>
                </div>
              </Reveal>
              <Reveal delay={3}>
                <div className="card card-accent p-6 h-full">
                  <div className="font-mono text-xs uppercase tracking-wider mb-3" style={{ color: 'var(--color-vermilion)' }}>Equilibrium Solver</div>
                  <p className="text-sm text-text-secondary leading-relaxed mb-4">
                    Computes each agent's sniping boundary b* under log-normal latency. A dominant strategy: it depends on the agent's own latency law and win probability, verified by an indifference identity.
                  </p>
                  <div className="text-xs font-mono text-text-tertiary">|P·(b*−μ)·D − c| &lt; 1e−9</div>
                </div>
              </Reveal>
              <Reveal delay={1}>
                <div className="card card-accent p-6 h-full">
                  <div className="font-mono text-xs uppercase tracking-wider mb-3" style={{ color: 'var(--color-signal-amber)' }}>Network Analysis</div>
                  <p className="text-sm text-text-secondary leading-relaxed">
                    Live UDP latency telemetry over a lock-free SPSC ring buffer, RDTSCP hardware timestamps, and a Lilliefors test for log-normality of the round-trip jitter. Dispersion feeds straight into the race odds.
                  </p>
                </div>
              </Reveal>
              <Reveal delay={2}>
                <div className="card card-accent p-6 h-full">
                  <div className="font-mono text-xs uppercase tracking-wider mb-3" style={{ color: 'var(--color-lime)' }}>WASM Bridge</div>
                  <p className="text-sm text-text-secondary leading-relaxed">
                    The engine is compiled to WebAssembly via Emscripten. JavaScript reads the OU signal and boundary arrays directly from WASM linear memory through raw pointers — no copy, no serialization — and renders with WebGL.
                  </p>
                </div>
              </Reveal>
            </div>
          </div>
        </section>

        {/* ============================== MATHEMATICS ============================== */}
        <section id="math" className="snap-section flex-col px-6 py-24">
          <div className="max-w-4xl w-full">
            <Reveal>
              <SectionLabel n="03" title="The mathematics" />
              <p className="text-sm text-text-secondary -mt-8 mb-12 max-w-xl">Every formula below maps to a function in the C++ engine.</p>
            </Reveal>
            <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
              <Reveal delay={1}>
                <div className="card p-6">
                  <div className="text-xs font-mono uppercase tracking-wider mb-4" style={{ color: 'var(--color-periwinkle)' }}>Exact OU transition kernel</div>
                  <div className="rounded-lg p-4 overflow-x-auto" style={{ background: 'var(--color-bg-deep)' }}>
                    <TexBlock name="ou_kernel" />
                  </div>
                  <p className="text-xs text-text-tertiary mt-3">Draws from the analytically exact conditional Gaussian — no SDE discretization.</p>
                </div>
              </Reveal>
              <Reveal delay={2}>
                <div className="card p-6">
                  <div className="text-xs font-mono uppercase tracking-wider mb-4" style={{ color: 'var(--color-lime)' }}>Race win probability</div>
                  <div className="rounded-lg p-4 overflow-x-auto" style={{ background: 'var(--color-bg-deep)' }}>
                    <TexBlock name="p_win" />
                  </div>
                  <p className="text-xs text-text-tertiary mt-3">
                    <code>compute_p_win()</code>. Latencies are log-normal; the log-difference is Gaussian, so the race reduces to a normal CDF on the log-space means.
                  </p>
                </div>
              </Reveal>
              <Reveal delay={3}>
                <div className="card p-6">
                  <div className="text-xs font-mono uppercase tracking-wider mb-4" style={{ color: 'var(--color-signal-amber)' }}>Signal decay — Laplace transform</div>
                  <div className="rounded-lg p-4 overflow-x-auto" style={{ background: 'var(--color-bg-deep)' }}>
                    <TexBlock name="decay" />
                  </div>
                  <p className="text-xs text-text-tertiary mt-3">
                    The fraction of edge surviving a random delay <Tex name="L" /> is the Laplace transform of the log-normal latency (no closed form — Gaussian quadrature). By Jensen, <em>dispersion erodes capture</em>.
                  </p>
                </div>
              </Reveal>
              <Reveal delay={4}>
                <div className="card p-6">
                  <div className="text-xs font-mono uppercase tracking-wider mb-4" style={{ color: 'var(--color-vermilion)' }}>Sniping boundary (Nash indifference)</div>
                  <div className="rounded-lg p-4 overflow-x-auto" style={{ background: 'var(--color-bg-deep)' }}>
                    <TexBlock name="boundary" />
                  </div>
                  <p className="text-xs text-text-tertiary mt-3">
                    <code>compute_equilibrium_boundary()</code>. Set expected sniping profit to zero: <Tex name="indiff" />. A dominant strategy — independent of the rival's threshold.
                  </p>
                </div>
              </Reveal>
            </div>
            <Reveal delay={5}>
              <div className="card p-6 mt-6">
                <div className="text-xs font-mono uppercase tracking-wider mb-4 text-text-tertiary">Payoff — loser-pays sniping</div>
                <div className="rounded-lg p-4 overflow-x-auto" style={{ background: 'var(--color-bg-deep)' }}>
                  <TexBlock name="payoff" />
                </div>
                <p className="text-xs text-text-tertiary mt-3">
                  The winner snipes the static maker quote anchored at <Tex name="mu_pm" />; <Tex name="v_exec" /> is the OU path linearly interpolated at the fractional arrival index. The loser crosses the spread and pays <Tex name="half_spread" />.
                </p>
              </div>
            </Reveal>
          </div>
        </section>

        {/* ============================== LIVE ARENA ============================== */}
        <section id="live" className="snap-section flex-col px-6 py-24">
          <div className="max-w-6xl w-full">
            <Reveal>
              <SectionLabel n="04" title="The arena, live" />
              <p className="text-sm md:text-base text-text-secondary -mt-8 mb-4 max-w-2xl leading-relaxed">
                This is the real C++ engine running in your browser at 60&nbsp;fps. The chart below is the
                shared <strong style={{ color: 'var(--color-indigo)' }}>price signal</strong> both agents watch.
                When it crosses an agent's <strong>snipe boundary</strong>, that agent fires at the stale quote —
                and the faster network wins the fill.
              </p>
            </Reveal>

            {/* legend + how-to-read strip */}
            <Reveal delay={1}>
              <div className="flex flex-wrap items-center gap-x-6 gap-y-2 mb-6 text-[11px] font-mono">
                <span className="flex items-center gap-2"><span className="w-4 h-[3px] rounded inline-block" style={{ background: 'var(--color-indigo)' }} /> V(t) — live price signal (OU process)</span>
                <span className="flex items-center gap-2"><span className="w-4 h-[3px] rounded inline-block" style={{ background: 'var(--color-teal)' }} /> your snipe boundary b*</span>
                <span className="flex items-center gap-2"><span className="w-4 h-[3px] rounded inline-block" style={{ background: 'var(--color-terracotta)' }} /> rival snipe boundary b*</span>
              </div>
            </Reveal>

            <Reveal delay={2}>
              <div className="card overflow-hidden grid lg:grid-cols-[300px_1fr]">
                {/* telemetry panel */}
                <div className="flex flex-col p-6 gap-5" style={{ background: 'var(--color-surface-raised)', borderRight: '1px solid var(--color-border-subtle)' }}>
                  <div className="flex items-center gap-2">
                    <span className="tag-dot" />
                    <span className="text-xs font-mono uppercase tracking-widest text-text-tertiary">Live Telemetry</span>
                  </div>

                  <div>
                    <div className="text-[10px] uppercase tracking-wider text-text-tertiary font-mono mb-1">Your network jitter (CV)</div>
                    <div className="text-3xl font-mono tabular-nums" style={{ color: liveSigma > 0.4 ? 'var(--color-terracotta)' : 'var(--color-teal)' }}>
                      {liveSigma.toFixed(4)}
                    </div>
                    <div className="text-[10px] text-text-tertiary mt-1 leading-snug">
                      Lower = steadier latency = you win more races. Rival is fixed at <span className="font-mono">0.2000</span>.
                    </div>
                  </div>

                  <div className="divider" />

                  <div className="grid grid-cols-2 gap-3">
                    <Metric label="b*_you" value={metrics.boundaryA.toFixed(4)} color="var(--color-teal)" />
                    <Metric label="b*_rival" value={metrics.boundaryB.toFixed(4)} color="var(--color-terracotta)" />
                    <Metric label="P(win)" value={`${(metrics.pWin * 100).toFixed(1)}%`} color={metrics.pWin < 0.5 ? 'var(--color-terracotta)' : 'var(--color-teal)'} />
                    <Metric label="Decay D(θ)" value={metrics.signalDecay.toFixed(4)} color="var(--color-gold)" />
                    <Metric label="V(t)" value={metrics.currentV.toFixed(4)} color="var(--color-indigo)" />
                    <Metric label="Var emp/th" value={`${metrics.signalVar.toFixed(2)}/${metrics.theoreticalVar.toFixed(2)}`} />
                  </div>
                  <div className="text-[10px] text-text-tertiary leading-snug -mt-1">
                    <strong>b*</strong> = how far the signal must drift before sniping is worth it ·
                    <strong> P(win)</strong> = your live edge in the latency race ·
                    <strong> Var emp/th</strong> = the engine's signal matching its closed-form variance.
                  </div>

                  <button
                    onMouseDown={stressDown} onMouseUp={stressUp} onMouseLeave={stressUp}
                    onTouchStart={stressDown} onTouchEnd={stressUp}
                    className="mt-2 py-3 rounded-xl font-mono text-xs uppercase tracking-wider transition-all"
                    style={isStressed
                      ? { background: 'rgba(217,81,42,0.14)', border: '1px solid var(--color-terracotta)', color: 'var(--color-terracotta)' }
                      : { background: 'transparent', border: '1px solid var(--color-border)', color: 'var(--color-text-secondary)' }}
                  >
                    {isStressed ? 'Inducing Latency…' : 'Hold to Stress Network'}
                  </button>
                  <div className="text-[10px] text-text-tertiary leading-snug -mt-2">
                    Hold to spike <em>your</em> jitter toward congestion (CV→1.1). Watch your boundary and
                    P(win) move against you in real time, then recover when you release.
                  </div>
                </div>

                {/* canvas */}
                <div className="relative" style={{ minHeight: '52vh' }}>
                  {wasmEngine ? (
                    <WebGLCanvas wasmEngine={wasmEngine} wasmMemory={wasmMemory} numPoints={1000} />
                  ) : (
                    <div className="w-full h-full flex items-center justify-center" style={{ minHeight: '52vh' }}>
                      <div className="text-text-tertiary font-mono text-sm animate-pulse">Loading WASM engine…</div>
                    </div>
                  )}
                  <div className="absolute top-4 left-5 font-mono text-[10px] uppercase tracking-[0.25em] text-text-tertiary pointer-events-none">
                    OU signal · equilibrium boundaries · 60 fps
                  </div>
                </div>
              </div>
            </Reveal>
          </div>
        </section>

        {/* ============================== SIMULATOR (GAME) ============================== */}
        <section id="simulator" className="snap-section flex-col px-6 py-24">
          <div className="max-w-5xl w-full">
            <Reveal>
              <SectionLabel n="05" title="Race the engine" />
              <p className="text-sm text-text-secondary -mt-8 mb-12 max-w-xl">
                Tune your network, then race your rival to twelve snipes. Win probability is computed live by the real equilibrium solver — every snipe is a Bernoulli draw on the latency race.
              </p>
            </Reveal>
            <Reveal delay={1}>
              <div className="card p-6 md:p-10">
                <RaceGame wasmEngine={wasmEngine} latencyBufferRef={latencyBufferRef} onActiveChange={onGameActive} />
              </div>
            </Reveal>
          </div>
        </section>

        {/* ============================== NETWORK ANALYSIS ============================== */}
        <section id="network" className="snap-section flex-col px-6 py-24">
          <div className="max-w-5xl w-full">
            <Reveal><SectionLabel n="06" title="Network analysis" /></Reveal>
            <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
              <Reveal delay={1}>
                <div className="card p-6 h-full">
                  <div className="font-mono text-xs uppercase tracking-wider mb-4" style={{ color: 'var(--color-periwinkle)' }}>Latency as a log-normal</div>
                  <p className="text-sm text-text-secondary leading-relaxed mb-4">
                    Round-trip delays are modelled as <Tex name="lnL" />, parameterised by the real-world latency mean and standard deviation. The coefficient of variation — not the mean — drives the race, because both agents share the same median path.
                  </p>
                  <div className="rounded-lg p-4 overflow-x-auto" style={{ background: 'var(--color-bg-deep)' }}>
                    <TexBlock name="network_sigma" />
                  </div>
                </div>
              </Reveal>
              <Reveal delay={2}>
                <div className="card p-6 h-full">
                  <div className="font-mono text-xs uppercase tracking-wider mb-4" style={{ color: 'var(--color-signal-amber)' }}>Lilliefors goodness-of-fit</div>
                  <p className="text-sm text-text-secondary leading-relaxed mb-4">
                    A two-sided Kolmogorov–Smirnov statistic with estimated parameters tests whether observed jitter is log-normal. Because mean and variance are fit from the sample, the standard 1.36/√n band is invalid — ARENA uses the Lilliefors critical value.
                  </p>
                  <div className="rounded-lg p-4 overflow-x-auto" style={{ background: 'var(--color-bg-deep)' }}>
                    <TexBlock name="lilliefors" />
                  </div>
                </div>
              </Reveal>
            </div>
            <Reveal delay={3}>
              <div className="grid grid-cols-2 md:grid-cols-4 gap-3 mt-6">
                <Metric label="P(win) live" value={`${(metrics.pWin * 100).toFixed(1)}%`} color={metrics.pWin < 0.5 ? 'var(--color-vermilion)' : 'var(--color-lime)'} />
                <Metric label="Decay D(θ)" value={metrics.signalDecay.toFixed(4)} color="var(--color-signal-amber)" />
                <Metric label="Your CV" value={liveSigma.toFixed(3)} color="var(--color-lime)" />
                <Metric label="Rival CV" value="0.200" color="var(--color-vermilion)" />
              </div>
            </Reveal>
          </div>
        </section>

        {/* ============================== ARCHITECTURE ============================== */}
        <section className="snap-section flex-col px-6 py-24">
          <div className="max-w-5xl w-full">
            <Reveal><SectionLabel n="07" title="Under the hood" /></Reveal>
            <div className="space-y-4">
              {[
                ['order_book.hpp', '315 lines', 'var(--color-lime)',
                  '32-byte aligned orders in a contiguous pool of 65,536 slots; each price level keeps head/tail indices forming an intrusive doubly-linked list. Top of book is an array lookup, not a tree search. Matching walks the resting queue and emits a fill array capped at 1,024 per call.'],
                ['spsc_buffer.hpp', '71 lines', 'var(--color-periwinkle)',
                  'Single-producer single-consumer ring buffer, power-of-two capacity so modulo becomes a bitwise AND. Head and tail are std::atomic with acquire/release — no mutex, no lock, no syscall.'],
                ['tsc_clock.hpp', '120 lines', 'var(--color-signal-amber)',
                  'Reads the CPU cycle counter via RDTSCP, serializing before the read. Calibration runs five rounds of 50ms sleeps, takes the median ticks-per-nanosecond to reject context-switch outliers, and also exposes an LFENCE-fenced variant.'],
                ['wasm_core.cpp', '260 lines', 'var(--color-vermilion)',
                  'The simulation engine compiled to WebAssembly. Exposes ArcticWasmEngine via embind; JS receives raw pointers to the internal float arrays and builds Float32Array views over WASM memory. The WebGL loop reads them each frame with zero intermediate allocation.'],
              ].map(([file, lines, color, body], i) => (
                <Reveal key={i} delay={(i % 4) + 1}>
                  <div className="card card-accent p-6 flex flex-col md:flex-row gap-6">
                    <div className="md:w-44 flex-shrink-0">
                      <div className="font-mono text-xs uppercase tracking-wider" style={{ color: color as string }}>{file}</div>
                      <div className="text-xs text-text-tertiary mt-1">{lines}</div>
                    </div>
                    <p className="text-sm text-text-secondary leading-relaxed flex-1">{body}</p>
                  </div>
                </Reveal>
              ))}
            </div>
          </div>
        </section>

        {/* ============================== FOOTER ============================== */}
        <footer className="py-14 text-center" style={{ borderTop: '1px solid var(--color-border-subtle)' }}>
          <div className="flex justify-center mb-3"><ArenaMark size={34} /></div>
          <div className="font-mono text-sm font-semibold tracking-widest grad-text mb-2">ARENA</div>
          <p className="text-xs font-mono text-text-tertiary">
            Agentic Racing Engine with Network Analysis · C++17 / Emscripten / WebGL
          </p>
        </footer>

      </div>
    </>
  );
}
