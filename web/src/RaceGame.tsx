import { useEffect, useRef, useState, useCallback } from 'react';

/* =====================================================================
   ARENA RACE — gamified front-end over the real WASM engine.

   The player tunes their own network jitter (latency coefficient of
   variation). That value is written into the latency buffer the WASM
   engine reads each frame, so the win probability P(win) shown here is
   computed by the *actual* equilibrium solver, not a fake.

   Each "snipe opportunity" is resolved as a Bernoulli(P(win)) draw —
   exactly the latency race. Winner snipes the stale quote for
   PnL = V_exec - fill; the loser pays the half-spread (loser-pays).
   ===================================================================== */

interface RaceGameProps {
  wasmEngine: any;
  latencyBufferRef: React.MutableRefObject<Float32Array | null>;
  onActiveChange?: (active: boolean) => void;
}

type Phase = 'idle' | 'countdown' | 'running' | 'done';

const TARGET = 12;          // snipes to win the race
const TICK_MS = 480;        // time between snipe opportunities
const HALF_SPREAD = 0.05;   // matches cost_c in the engine

interface FloatPop { id: number; text: string; positive: boolean; lane: 'you' | 'rival'; }

export function RaceGame({ wasmEngine, latencyBufferRef, onActiveChange }: RaceGameProps) {
  const [phase, setPhase] = useState<Phase>('idle');
  const [count, setCount] = useState(3);
  const [userCV, setUserCV] = useState(0.12);
  const [youScore, setYouScore] = useState(0);
  const [rivalScore, setRivalScore] = useState(0);
  const [pnl, setPnl] = useState(0);
  const [pWin, setPWin] = useState(0.5);
  const [streak, setStreak] = useState(0);
  const [bestStreak, setBestStreak] = useState(0);
  const [flash, setFlash] = useState<'win' | 'lose' | null>(null);
  const [pops, setPops] = useState<FloatPop[]>([]);
  const [result, setResult] = useState<'win' | 'lose' | 'draw' | null>(null);

  const youRef = useRef(0);
  const rivalRef = useRef(0);
  const streakRef = useRef(0);
  const popId = useRef(0);

  // Push the player's chosen CV into the engine's latency buffer.
  useEffect(() => {
    if (latencyBufferRef.current) latencyBufferRef.current[0] = userCV;
  }, [userCV, latencyBufferRef]);

  // Tell the parent when the race owns the shared latency buffer.
  useEffect(() => {
    onActiveChange?.(phase === 'running' || phase === 'countdown');
  }, [phase, onActiveChange]);

  // Live P(win) read straight from the engine.
  useEffect(() => {
    if (!wasmEngine) return;
    const id = setInterval(() => {
      try { setPWin(wasmEngine.get_p_win()); } catch { /* engine not ready */ }
    }, 150);
    return () => clearInterval(id);
  }, [wasmEngine]);

  const addPop = useCallback((text: string, positive: boolean, lane: 'you' | 'rival') => {
    const id = ++popId.current;
    setPops(p => [...p, { id, text, positive, lane }]);
    setTimeout(() => setPops(p => p.filter(x => x.id !== id)), 1000);
  }, []);

  const reset = useCallback(() => {
    youRef.current = 0; rivalRef.current = 0; streakRef.current = 0;
    setYouScore(0); setRivalScore(0); setPnl(0); setStreak(0); setBestStreak(0);
    setResult(null); setPops([]);
  }, []);

  const start = useCallback(() => {
    if (!wasmEngine) return;
    reset();
    setPhase('countdown');
    setCount(3);
  }, [wasmEngine, reset]);

  // countdown 3..2..1..GO
  useEffect(() => {
    if (phase !== 'countdown') return;
    if (count <= 0) { setPhase('running'); return; }
    const id = setTimeout(() => setCount(c => c - 1), 700);
    return () => clearTimeout(id);
  }, [phase, count]);

  // main race loop
  useEffect(() => {
    if (phase !== 'running') return;
    const id = setInterval(() => {
      let p = 0.5;
      let v = 0;
      try {
        p = wasmEngine.get_p_win();
        v = wasmEngine.get_current_v();
      } catch { /* ignore */ }

      const youWin = Math.random() < p;
      if (youWin) {
        youRef.current += 1;
        const edge = Math.max(0, v - HALF_SPREAD); // snipe the stale quote
        setPnl(x => x + edge);
        setYouScore(youRef.current);
        streakRef.current += 1;
        setStreak(streakRef.current);
        setBestStreak(b => Math.max(b, streakRef.current));
        setFlash('win');
        addPop(`+${edge.toFixed(3)}`, true, 'you');
      } else {
        rivalRef.current += 1;
        setPnl(x => x - HALF_SPREAD);          // loser pays the half-spread
        setRivalScore(rivalRef.current);
        streakRef.current = 0;
        setStreak(0);
        setFlash('lose');
        addPop(`-${HALF_SPREAD.toFixed(2)}`, false, 'rival');
      }
      setTimeout(() => setFlash(null), 450);

      if (youRef.current >= TARGET || rivalRef.current >= TARGET) {
        setPhase('done');
        if (youRef.current > rivalRef.current) setResult('win');
        else if (rivalRef.current > youRef.current) setResult('lose');
        else setResult('draw');
      }
    }, TICK_MS);
    return () => clearInterval(id);
  }, [phase, wasmEngine, addPop]);

  const youPct = Math.min(100, (youScore / TARGET) * 100);
  const rivalPct = Math.min(100, (rivalScore / TARGET) * 100);
  const cvLabel = userCV <= 0.1 ? 'Co-located' : userCV <= 0.2 ? 'Tight' : userCV <= 0.35 ? 'Retail' : 'Congested';

  return (
    <div className="w-full max-w-3xl mx-auto">
      {/* HUD */}
      <div className="grid grid-cols-4 gap-3 mb-6">
        <HudStat label="P(win)" value={`${(pWin * 100).toFixed(1)}%`} accent={pWin >= 0.5 ? 'lime' : 'vermilion'} />
        <HudStat label="Net PnL" value={pnl >= 0 ? `+${pnl.toFixed(2)}` : pnl.toFixed(2)} accent={pnl >= 0 ? 'lime' : 'vermilion'} />
        <HudStat label="Streak" value={`${streak}`} accent="periwinkle" />
        <HudStat label="Best" value={`${bestStreak}`} accent="periwinkle" />
      </div>

      {/* Lanes */}
      <div className="space-y-4 mb-6">
        <RaceLane
          name="YOU"
          score={youScore}
          pct={youPct}
          color="var(--color-lime)"
          flash={flash === 'win'}
          pops={pops.filter(p => p.lane === 'you')}
        />
        <RaceLane
          name="RIVAL"
          score={rivalScore}
          pct={rivalPct}
          color="var(--color-vermilion)"
          flash={flash === 'lose'}
          pops={pops.filter(p => p.lane === 'rival')}
        />
      </div>

      {/* Network tuning */}
      <div className="card p-5 mb-6">
        <div className="flex items-center justify-between mb-3">
          <div className="text-[11px] font-mono uppercase tracking-widest text-text-tertiary">
            Your Network Jitter (CV)
          </div>
          <div className="font-mono text-sm">
            <span className="text-text-primary tabular-nums">{userCV.toFixed(2)}</span>
            <span className="ml-2 text-lime" style={{ color: 'var(--color-lime)' }}>{cvLabel}</span>
          </div>
        </div>
        <input
          type="range"
          className="arena-range"
          min={0.05}
          max={0.6}
          step={0.01}
          value={userCV}
          onChange={e => setUserCV(parseFloat(e.target.value))}
          disabled={phase === 'running' || phase === 'countdown'}
        />
        <div className="flex justify-between mt-2 text-[10px] font-mono text-text-tertiary">
          <span>tighter network — higher win odds</span>
          <span>rival fixed at CV 0.20</span>
        </div>
      </div>

      {/* Controls / status */}
      <div className="flex items-center justify-center min-h-[64px]">
        {phase === 'idle' && (
          <button className="btn-race" onClick={start} disabled={!wasmEngine}>
            {wasmEngine ? '▶ Start Race' : 'Loading engine…'}
          </button>
        )}
        {phase === 'countdown' && (
          <div className="font-mono text-5xl font-bold grad-text tabular-nums">
            {count === 0 ? 'GO' : count}
          </div>
        )}
        {phase === 'running' && (
          <div className="font-mono text-xs uppercase tracking-widest text-text-tertiary animate-pulse">
            Racing… first to {TARGET} snipes
          </div>
        )}
        {phase === 'done' && (
          <div className="flex flex-col items-center gap-4">
            <div
              className="font-mono text-4xl font-bold"
              style={{ color: result === 'win' ? 'var(--color-lime)' : result === 'lose' ? 'var(--color-vermilion)' : 'var(--color-periwinkle)' }}
            >
              {result === 'win' ? 'YOU WIN' : result === 'lose' ? 'RIVAL WINS' : 'DEAD HEAT'}
            </div>
            <div className="text-xs font-mono text-text-secondary">
              {youScore}–{rivalScore} · net PnL {pnl >= 0 ? '+' : ''}{pnl.toFixed(2)} · best streak {bestStreak}
            </div>
            <button className="btn-race" onClick={start}>↻ Race Again</button>
          </div>
        )}
      </div>
    </div>
  );
}

function HudStat({ label, value, accent }: { label: string; value: string; accent: 'lime' | 'vermilion' | 'periwinkle' }) {
  const color = accent === 'lime' ? 'var(--color-lime)' : accent === 'vermilion' ? 'var(--color-vermilion)' : 'var(--color-periwinkle)';
  return (
    <div className="metric-card text-center">
      <div className="text-[9px] uppercase tracking-wider text-text-tertiary font-mono mb-1">{label}</div>
      <div className="font-mono text-lg tabular-nums" style={{ color }}>{value}</div>
    </div>
  );
}

function RaceLane({ name, score, pct, color, flash, pops }: {
  name: string; score: number; pct: number; color: string; flash: boolean; pops: FloatPop[];
}) {
  return (
    <div>
      <div className="flex items-center justify-between mb-1.5">
        <span className="font-mono text-[11px] uppercase tracking-widest" style={{ color }}>{name}</span>
        <span className="font-mono text-xs text-text-secondary tabular-nums">{score}/{TARGET}</span>
      </div>
      <div className={`lane ${flash ? (name === 'YOU' ? 'flash-win' : 'flash-lose') : ''}`}>
        <div
          className="lane-fill"
          style={{ width: `${pct}%`, background: `linear-gradient(90deg, transparent, ${color}33 60%, ${color}66)` }}
        />
        <div
          className="lane-puck"
          style={{ left: `calc(${pct}% )`, background: color, boxShadow: `0 0 18px ${color}` }}
        />
        {/* finish line */}
        <div className="absolute top-0 bottom-0 right-1 w-[2px]" style={{ background: 'repeating-linear-gradient(180deg, var(--color-text-tertiary), var(--color-text-tertiary) 4px, transparent 4px, transparent 8px)' }} />
        {/* floating points */}
        <div className="absolute inset-0 pointer-events-none overflow-hidden">
          {pops.map(p => (
            <span
              key={p.id}
              className="pop-up absolute font-mono text-xs font-semibold"
              style={{ left: `${Math.min(92, pct)}%`, top: '8px', color: p.positive ? 'var(--color-lime)' : 'var(--color-vermilion)' }}
            >
              {p.text}
            </span>
          ))}
        </div>
      </div>
    </div>
  );
}
