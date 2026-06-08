/**
 * ARENA logo mark: two racing lanes (teal = you, terracotta = rival) converging
 * to a single fill node at the apex — an "A" formed by the race itself.
 */
export function ArenaMark({ size = 28, className = '' }: { size?: number; className?: string }) {
  return (
    <svg width={size} height={size} viewBox="0 0 48 48" fill="none" className={className} aria-hidden="true">
      {/* left lane (you) */}
      <path d="M9 40 L24 8" stroke="var(--color-teal)" strokeWidth="4.5" strokeLinecap="round" />
      {/* right lane (rival) */}
      <path d="M39 40 L24 8" stroke="var(--color-terracotta)" strokeWidth="4.5" strokeLinecap="round" />
      {/* crossbar / the trade */}
      <path d="M15.5 27 L32.5 27" stroke="var(--color-indigo)" strokeWidth="3.5" strokeLinecap="round" />
      {/* fill node at apex */}
      <circle cx="24" cy="8" r="4.6" fill="var(--color-indigo)" />
      <circle cx="24" cy="8" r="2.1" fill="var(--color-surface-raised)" />
      {/* start pucks */}
      <circle cx="9" cy="40" r="3" fill="var(--color-teal)" />
      <circle cx="39" cy="40" r="3" fill="var(--color-terracotta)" />
    </svg>
  );
}

/** Full lockup: mark + wordmark. */
export function ArenaLogo({ size = 28, showWord = true, className = '' }: { size?: number; showWord?: boolean; className?: string }) {
  return (
    <span className={`inline-flex items-center gap-2.5 ${className}`}>
      <ArenaMark size={size} />
      {showWord && (
        <span className="font-mono font-bold tracking-[0.18em]" style={{ fontSize: size * 0.6 }}>
          ARENA
        </span>
      )}
    </span>
  );
}
