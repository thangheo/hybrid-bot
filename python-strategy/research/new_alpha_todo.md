I'll write a short tracked todo plan for implementing and validating the slow-gate hybrid policy (design, backtests, runtime integration, telemetry). Expected outcome: a clear, ordered checklist we can follow and mark progress against.

Starting (1/6) Design gating policy spec

Nice, that’s a solid, well-thought plan for a gated hybrid — I understand it and it's reasonable for your stack. Below I’ll do exactly what you asked: no code yet, just an actionable plan and analysis, callouts for risks/complexities, and choices you can defer. I’ll keep it focused and practical so you can pick next steps.

Quick recap of your idea (one-liner)
Use short-term HMM + Bayesian OB signals to decide minute-scale entries/exits, but when the short-term signal is neutral allow a time-limited extension only when the slow 4‑hour context (trend + regime persistence + microstructure) supports continuing the trade.

Inputs, outputs, and contract
Inputs
pi_t(r): 1-min HMM posterior vector (size 4).
P_stay(r): HMM self-transition probabilities (per-regime).
p_OB_up^{(r)}(t): 1-min Bayesian OB up-prob under regime r (or vector).
p_trend_up(t): slow 4h trend score (probability or monotone score).
position state: current side/quantity/entry time/last update/time-held.
market microstructure signals: spread, OB imbalance, liquidity depth.
volatility sigma_1m (estimator).
config thresholds: theta_enter, theta_exit, theta_neutral, tau_stay, tau_trend, k, check_interval_sec, max_extensions, risk_scaling params.
Outputs (actions)
ENTER / ADD / HOLD / EXIT / PARTIAL_EXIT and parameters (size, sl, tl).
Telemetry labels: exit_reason ∈ {short_term_exit, neutral_no_slow_gate, extension_time_cap, slow_gate_fails, …}.
Error modes
Missing features (empty p_trend or regime posteriors): treat as conservative (prefer exit).
Data lag / stale market data: abort extension until data is fresh.
Execution failures: track and retry with backoff, but avoid aggressive re-submits.
Success criteria

Backtest: extensions should increase net-after-fee return or reduce drawdown without excessive turnover.
Runtime: extension rate within target (e.g., 15–25% of neutrals), no additional tail risk.
Step-by-step plan (high level)
Design spec (safe, measurable). (Short, we’ll implement offline first.)
Data collection & diagnostics: estimate P_stay, HL_r, distributions, alpha, and slow-score calibration.
Offline Python implementation + backtests for threshold tuning.
Decide runtime placement (Python vs C++).
Implement runtime module (if chosen) and unit tests.
Telemetry/A–B rollout.
I put these into a tracked todo list (6 items). Below I expand each step with subtasks and caveats.

1) Design / spec (I can finish this now)
Formalize alpha_1m(t) = sum_r pi_t(r) * (2*p_OB_up^{(r)} - 1).
Define neutral band: |alpha| ≤ theta_neutral. Hysteresis: theta_enter > theta_exit.
Slow gate conditions:
stay_prob = sum_r pi_t(r) * P_stay(r) ≥ tau_stay
p_trend_up ≥ tau_trend
microstructure_ok() = (spread < spread_cap) && (OB_up_for_top_regime >= min_micro_ob)
Mixture half-life HL(t) = sum_r pi_t(r) * HL_r where HL_r = ln(2) / -ln(P_stay(r)).
Extension window = k * HL(t). Re-evaluate gate every check_interval seconds (30–60s).
Risk scaling when extended:
sigma_eff = sigma_1m * sqrt(t_hold / 1min)
stop = entry_price - c_sl * sigma_eff (for long)
trailing and partial decay rules (sketch below)
Telemetry fields to log per exit/extend decision: time, alpha, stay_prob, p_trend_up, spread, top_regime, HL(t), hold_time, action, exit_reason.
Caveats / choices:

Transition matrix: you said transitions not implemented — you can still estimate P_stay(r) empirically from HMM smoothing or fit a simple transition matrix from historical state sequences. If not available, treat P_stay(r) as an empirical persistence score from dwell times (OK to skip explicit transitions for a first pass).
p_trend_up: if you don’t have calibrated probabilities, use a scaled logistic on 4h features; calibrate in Python backtests.
2) Data collection & baseline stats (important)
Why: To pick sensible thresholds (tau_stay, tau_trend, theta_enter/exit, k) and to check extension frequency and realized outcomes.
Subtasks:

Extract historical records aligned to your trades or signals at 1-min resolution:
pi_t(r) (posterior), p_OB_up^{(r)}(t), p_trend_up(t), spread, sigma_1m, realized rtn next 1m and next N mins.
Compute:
Empirical P_stay(r) or dwell-time-based HL_r.
Distribution of alpha_1m — compute percentiles, neutral frequency.
For neutral cases, compute conditional realized drift given p_trend bins.
Estimate trade statistics when slow gate conditions pass vs fail (to see expected uplift).
Output: CSV/Parquet dataset for offline testing.
Complexities:

You may need to re-run the HMM posterior calculation offline if not logged historically — but typically you log pi_t during live runs. If not, recompute in Python from saved features.
If this is hard now, we can approximate P_stay by counting how often state at t equals t+1 using Viterbi or smoothing sequences (acceptable).

3) Offline calibration & backtest (Python) — recommended first
Why: Fast iteration, no risk to production, easy to grid-search.
Subtasks:

Implement the policy as pure Python using your saved dataset (pseudo code you've provided).
Grid-search / Bayesian optimization over thresholds:
theta_enter ∈ [0.01..0.5], theta_exit ∈ [theta_enter-0.05..theta_enter-0.2], tau_stay ∈ [0.7..0.9], tau_trend ∈ [0.55..0.8], k ∈ [0.5..3].
Metrics tracked: net PnL after fees, Sharpe, max drawdown, turnover, extension rate, exit_reason attribution.
Plot extension vs performance, turnover vs performance.
Decide defaults and safety bounds.
Practical choices:

Use vectorized backtest; if you lack a backtester, a simple event-loop wallet+order model with slippage and fee assumptions is enough for first pass.
Start with small, conservative k (1) and strict tau_stay (0.8) to avoid overstaying.
If this shows no benefit, stop — don’t implement.

4) Decide runtime architecture (Python vs C++)
Considerations:

Latency: The gating runs on neutral states every 30–60s. That's low-frequency; Python strategy layer is perfectly fine.
Dependencies: If your production strategy process already runs Python (your repo has python-strategy), adding the policy there is simpler. If you want everything inside the C++ risk engine, integrate into PostTradeMonitor or a new AutoExtensionManager.
Safety & Execution: Execution should go through existing ExecutionInterface (C++). The Python strategy layer likely talks to a gateway; check latency and auth.
Recommendation:

Run calibration & decision logic in Python first (fast). If results are good and you require tighter integration, port to C++ into PostTradeMonitor or add a small AutoExtensionManager that subscribes to PositionTracker updates.
5) Runtime implementation sketch (if asked to implement later)
Option A — Python strategy layer:
Implement HybridGate class that subscribes to your pipeline, uses HMM posteriors and slow features to emit hold/exit signals, and asks the existing order-execution layer to close/scale.
Option B — C++ AutoExtensionManager:
Add a manager owned by RiskManager or PostTradeMonitor.
Subscribe to PositionTracker updates or poll getAllPositions.
Maintain internal timers per-position (last entry time, last re-eval time).
Use ExecutionInterface to submit close orders. Respect concurrency and rate limits.
Implement unit tests for the decision function (given synthetic pi, p_OB_up, p_trend, returns expected action).
Edge cases and safety:

Stale data: require recency window; if a component lagging, prefer exit.
Position partial fills: track remaining size and avoid double-submits.
Exchange disconnect or insufficient liquidity: skip auto-close, mark telemetry.
Multi-strategy conflicts: ensure decisions are scoped to strategyId and do not fight other strategy managers.
6) Telemetry & A/B testing (essential)
Add explicit log/metric tag: exit_source ∈ {short_term, slow_gate, timed_cap, manual, risk_limit}.
Collect per-exit metrics: P&L from entry to exit, hold time, fees, pre/post volatility.
Run A/B tests: enable slow-gate for X% of opportunities and compare net P&L, turnover, drawdown.
Risks and where to be conservative
Overfitting thresholds: use robust CV (time-forward) and penalties for turnover/costs.
Regime classification noise: avoid extension without a strong stay probability (tau_stay high).
Complexity vs gain: if slow-gate increases turnover or worsens net P&L after fees, revert to neutral-exit.
Interaction with other managers (stop-loss, rebalancing): ensure ordering of controllers — e.g., stop-loss should always be able to close even if slow-gate suggests extend.
Quick answers to a couple of your aside points
The regime transition probabilities: useful but not strictly required. You can estimate P_stay(r) from dwell times (empirical) rather than full transition matrix. That’s acceptable for gating, so safe to postpone full transition modeling.
Drift calculation in Python: yes, do that offline first — easier and faster to tune. If successful, you can port the scaler/predictor to C++ later.
Deliverables I can produce next (pick any)
(A) A compact Python backtest harness implementing your pseudocode with grid search over thresholds (recommended next step).
(B) A detailed file-by-file integration plan to implement the runtime module in C++ (which components to change, constructors to modify, exact files).
(C) Add diagnostic logs to PositionTracker to confirm instance creation sources (I already added idempotency protections earlier; can add pointer logs).
(D) Help implement the AutoExtensionManager in C++ (larger change) with unit tests and telemetry.
Tell me which deliverable you want next. If you want (A), I’ll outline the minimal data schema and the backtest algorithm and then implement a Python script for offline experiments. If you want (B) or (D), I’ll produce the step-by-step code changes and then implement them.