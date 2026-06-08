// Pre-render all KaTeX formulas to HTML at build time using Node, where
// KaTeX works correctly. Writes src/prerendered-formulas.json which the app
// imports and injects via dangerouslySetInnerHTML. This avoids the Vite 8 /
// Rolldown bug that corrupts KaTeX's lexer when it is bundled for the browser.
import katex from 'katex';
import { writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import { FORMULAS } from '../src/formulas.mjs';

const __dirname = dirname(fileURLToPath(import.meta.url));

// inline keys render in inline mode; everything else is display (block) mode.
const INLINE = new Set(['L', 'indiff', 'mu_pm', 'v_exec', 'half_spread', 'lnL']);

const out = {};
for (const [key, tex] of Object.entries(FORMULAS)) {
  out[key] = katex.renderToString(tex, {
    displayMode: !INLINE.has(key),
    throwOnError: true,
    output: 'htmlAndMathml',
  });
}

const target = join(__dirname, '..', 'src', 'prerendered-formulas.json');
writeFileSync(target, JSON.stringify(out));
console.log(`Prerendered ${Object.keys(out).length} formulas -> ${target}`);
