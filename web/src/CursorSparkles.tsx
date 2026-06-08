import { useEffect, useRef } from 'react';

/**
 * CursorSparkles — a lightweight canvas particle trail that follows the cursor.
 * Emits small star/diamond sparkles in the ARENA palette as the pointer moves.
 * Pointer-events disabled; respects prefers-reduced-motion; pauses when hidden.
 */
type Spark = {
  x: number;
  y: number;
  vx: number;
  vy: number;
  life: number;
  maxLife: number;
  size: number;
  rot: number;
  vr: number;
  color: string;
};

const COLORS = ['#0f8c7e', '#d9512a', '#5a47d6', '#c8901a'];

export default function CursorSparkles() {
  const canvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    const reduce = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
    if (reduce) return;
    // Skip on touch-primary devices (no hover cursor to trail)
    if (window.matchMedia('(hover: none)').matches) return;

    const canvas = canvasRef.current!;
    const ctx = canvas.getContext('2d')!;
    let raf = 0;
    let w = 0;
    let h = 0;
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const sparks: Spark[] = [];

    let lastX = 0;
    let lastY = 0;
    let lastEmit = 0;

    const resize = () => {
      w = window.innerWidth;
      h = window.innerHeight;
      canvas.width = w * dpr;
      canvas.height = h * dpr;
      canvas.style.width = w + 'px';
      canvas.style.height = h + 'px';
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    };
    resize();
    window.addEventListener('resize', resize);

    const emit = (x: number, y: number, n: number) => {
      for (let i = 0; i < n; i++) {
        const a = Math.random() * Math.PI * 2;
        const sp = Math.random() * 0.5 + 0.1;
        sparks.push({
          x,
          y,
          vx: Math.cos(a) * sp,
          vy: Math.sin(a) * sp - 0.25,
          life: 0,
          maxLife: 480 + Math.random() * 360,
          size: Math.random() * 3 + 1.6,
          rot: Math.random() * Math.PI,
          vr: (Math.random() - 0.5) * 0.08,
          color: COLORS[(Math.random() * COLORS.length) | 0],
        });
      }
      if (sparks.length > 220) sparks.splice(0, sparks.length - 220);
    };

    const onMove = (e: PointerEvent) => {
      const now = performance.now();
      const dx = e.clientX - lastX;
      const dy = e.clientY - lastY;
      const dist = Math.hypot(dx, dy);
      lastX = e.clientX;
      lastY = e.clientY;
      if (now - lastEmit > 16 && dist > 1.5) {
        emit(e.clientX, e.clientY, Math.min(3, 1 + (dist / 24) | 0));
        lastEmit = now;
      }
    };
    window.addEventListener('pointermove', onMove, { passive: true });

    const drawStar = (s: Spark, alpha: number) => {
      ctx.save();
      ctx.translate(s.x, s.y);
      ctx.rotate(s.rot);
      ctx.globalAlpha = alpha;
      ctx.fillStyle = s.color;
      ctx.shadowColor = s.color;
      ctx.shadowBlur = 6;
      // four-point sparkle
      const r = s.size;
      ctx.beginPath();
      ctx.moveTo(0, -r * 2);
      ctx.quadraticCurveTo(0, 0, r * 2, 0);
      ctx.quadraticCurveTo(0, 0, 0, r * 2);
      ctx.quadraticCurveTo(0, 0, -r * 2, 0);
      ctx.quadraticCurveTo(0, 0, 0, -r * 2);
      ctx.fill();
      ctx.restore();
    };

    let prev = performance.now();
    const loop = (t: number) => {
      const dt = Math.min(40, t - prev);
      prev = t;
      ctx.clearRect(0, 0, w, h);
      for (let i = sparks.length - 1; i >= 0; i--) {
        const s = sparks[i];
        s.life += dt;
        if (s.life >= s.maxLife) {
          sparks.splice(i, 1);
          continue;
        }
        s.x += s.vx * dt * 0.06;
        s.y += s.vy * dt * 0.06;
        s.vy += 0.0006 * dt; // gentle gravity
        s.rot += s.vr;
        const p = s.life / s.maxLife;
        const alpha = p < 0.2 ? p / 0.2 : 1 - (p - 0.2) / 0.8;
        drawStar(s, alpha * 0.9);
      }
      raf = requestAnimationFrame(loop);
    };
    raf = requestAnimationFrame(loop);

    return () => {
      cancelAnimationFrame(raf);
      window.removeEventListener('resize', resize);
      window.removeEventListener('pointermove', onMove);
    };
  }, []);

  return (
    <canvas
      ref={canvasRef}
      aria-hidden="true"
      style={{
        position: 'fixed',
        inset: 0,
        zIndex: 60,
        pointerEvents: 'none',
      }}
    />
  );
}
