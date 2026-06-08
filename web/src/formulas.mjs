// Single source of truth for every LaTeX formula on the site.
// Kept as a plain .mjs module so BOTH the Node prerender script and the
// Vite app can import it. Pre-rendering in Node sidesteps a Rolldown/Vite 8
// bug that miscompiles KaTeX's lexer in the browser bundle.
export const FORMULAS = {
  // block
  ou_kernel: String.raw`V_{t+\Delta t} \sim \mathcal{N}\!\left(\mu + (V_t - \mu)e^{-\theta \Delta t},\; \tfrac{\sigma_V^2}{2\theta}\bigl(1 - e^{-2\theta \Delta t}\bigr)\right)`,
  p_win: String.raw`P(\text{win}) = \Phi\!\left(\frac{\mu_{\text{comp}} - \mu_{\text{self}}}{\sqrt{\sigma_{\text{self}}^2 + \sigma_{\text{comp}}^2}}\right)`,
  decay: String.raw`D(\theta) = \mathbb{E}\!\left[e^{-\theta L}\right] \;\ge\; e^{-\theta\,\mathbb{E}[L]}`,
  boundary: String.raw`b^{*} = \mu + \frac{c}{P(\text{win})\cdot D(\theta)}`,
  payoff: String.raw`\Pi_{\text{win}} = V_{\text{exec}} - \left(\mu + \tfrac{s}{2}\right), \qquad \Pi_{\text{lose}} = -\tfrac{s}{2}`,
  network_sigma: String.raw`\sigma^2 = \ln\!\Bigl(1 + (s/m)^2\Bigr),\quad \mu_{\ln} = \ln m - \tfrac{\sigma^2}{2}`,
  lilliefors: String.raw`D_n = \max_i \left|\,\hat F(x_i) - \tfrac{i}{n}\,\right|,\quad D^{*}_{0.05} \approx \frac{0.895}{\sqrt{n}}`,

  // inline
  L: String.raw`L`,
  indiff: String.raw`P\cdot(b^{*}-\mu)\cdot D - c = 0`,
  mu_pm: String.raw`\mu \pm s/2`,
  v_exec: String.raw`V_{\text{exec}}`,
  half_spread: String.raw`s/2`,
  lnL: String.raw`\ln L \sim \mathcal{N}(m, \sigma^2)`,
};
