# DSP CPU vs Metal Performance PR Agents

Use these prompts with:

- `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`
- `plan/roadmap/DSP_CPU_VS_METAL_PERFORMANCE_PR_ROADMAP.md`

Global constraints for every PR:

- Implement or verify exactly the named PR.
- Do not redesign GraphX runtime contracts beyond the explicit PR scope.
- Use `GraphExecutor::Execute()` consolidated timing as the canonical timing source after PR1.
- Do not invent an unrelated benchmark-local lifecycle timing harness.
- Preserve the CPU DSP lane as the correctness reference.
- Preserve the GPU DSP lane as Metal direct DFT, not FFT.
- Do not rename `MetalSpectrumDftNode` to FFT.
- Do not claim Metal is faster unless measured timing reports show it.
- Do not add external benchmark dependencies.
- Do not require real data, audio devices, SAR, GOTCHA, CRSD, MATLAB, SarPy, or external datasets.
- Do not add spectrogram image output.
- Stop after the requested implementer or verifier report.

---

## PR1: Consolidated GraphExecutor Execute Timing

### Implementer Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR1 from plan/roadmap/DSP_CPU_VS_METAL_PERFORMANCE_PR_ROADMAP.md: Consolidated GraphExecutor Execute Timing.

Use the implementer prompt from: plan/agents/DSP_CPU_VS_METAL_PERFORMANCE_PR_AGENTS.md

Scope:
- Extend `ExecutionResult` with consolidated `Execute()` timing fields:
  - `init_elapsed_time_ms`
  - `start_elapsed_time_ms`
  - `run_elapsed_time_ms`
  - `stop_elapsed_time_ms`
  - `join_elapsed_time_ms`
  - keep `elapsed_time_ms` as total `Execute()` wall-clock duration.
- Update `GraphExecutor::ExecuteExpected()` so it measures total wall-clock duration and copies lifecycle phase timings from `Init`, `Start`, `Run`, `Stop`, and `Join`.
- Add focused timing contract tests.
- Preserve existing `Init`, `Start`, `Run`, `Stop`, and `Join` behavior.
- Do not add benchmark executable, DSP docs, or performance claims.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/DSP_CPU_VS_METAL_PERFORMANCE_IMPL_PR1.md.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR1 from plan/roadmap/DSP_CPU_VS_METAL_PERFORMANCE_PR_ROADMAP.md: Consolidated GraphExecutor Execute Timing.

Use the verifier prompt from: plan/agents/DSP_CPU_VS_METAL_PERFORMANCE_PR_AGENTS.md

Required checks:
- `ExecutionResult` exposes consolidated `Execute()` timing fields.
- `GraphExecutor::ExecuteExpected()` sets total `elapsed_time_ms`.
- `ExecuteExpected()` copies phase timings for init/start/run/stop/join.
- Existing manual lifecycle methods still expose their previous timing behavior.
- Focused timing contract tests exist and pass.
- No benchmark executable, DSP docs, performance claims, or unrelated runtime redesign was added.

Stop after verifier report.
Save the report to plan/reviews/DSP_CPU_VS_METAL_PERFORMANCE_VERIFY_PR1.md.
```

---

## PR2: DSP CPU vs Metal Execute-Timing Comparison

### Implementer Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR2 from plan/roadmap/DSP_CPU_VS_METAL_PERFORMANCE_PR_ROADMAP.md: DSP CPU vs Metal Execute-Timing Comparison.

Use the implementer prompt from: plan/agents/DSP_CPU_VS_METAL_PERFORMANCE_PR_AGENTS.md

Prerequisite:
- PR1 consolidated `GraphExecutor::Execute()` timing must already exist.

Scope:
- Add CPU-vs-Metal DSP comparison using existing CPU and GPU DSP configs.
- Use `GraphExecutorBuilder`, JSON configs, plugin loading, executor completion, and `GraphExecutor::Execute()` returned timing fields.
- Do not manually time lifecycle phases as the primary timing source.
- Support warm-up iterations and measured iterations.
- Compare deterministic equivalent sine settings.
- Emit informational report JSON when requested.
- Include correctness/parity summary using peak frequency, peak magnitude, and selected magnitude bins.
- Skip/report clearly when Metal is unavailable.
- Default mode must not fail only because Metal is slower.

Do not implement true Metal FFT.
Do not add strict performance gate; that is PR4.
Do not make performance claims in docs.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/DSP_CPU_VS_METAL_PERFORMANCE_IMPL_PR2.md.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR2 from plan/roadmap/DSP_CPU_VS_METAL_PERFORMANCE_PR_ROADMAP.md: DSP CPU vs Metal Execute-Timing Comparison.

Use the verifier prompt from: plan/agents/DSP_CPU_VS_METAL_PERFORMANCE_PR_AGENTS.md

Required checks:
- Comparison uses CPU config and GPU Metal DFT config.
- Timing data comes from `GraphExecutor::Execute()` returned `ExecutionResult`.
- Warm-up iterations are excluded from measured summary.
- Report includes CPU/GPU timing arrays, summary stats, speedup ratio, and correctness/parity summary.
- Metal-unavailable cases report/skip clearly and do not fabricate GPU timing.
- Default mode does not fail only because Metal is slower.
- No true Metal FFT, strict gate, docs performance claim, SAR/GOTCHA/CRSD dependency, or external benchmark dependency was added.

Stop after verifier report.
Save the report to plan/reviews/DSP_CPU_VS_METAL_PERFORMANCE_VERIFY_PR2.md.
```

---

## PR3: Execute-Timing Report Schema And Statistics Guardrails

### Implementer Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR3 from plan/roadmap/DSP_CPU_VS_METAL_PERFORMANCE_PR_ROADMAP.md: Execute-Timing Report Schema And Statistics Guardrails.

Use the implementer prompt from: plan/agents/DSP_CPU_VS_METAL_PERFORMANCE_PR_AGENTS.md

Prerequisites:
- PR1 consolidated `GraphExecutor::Execute()` timing must exist.
- PR2 CPU-vs-Metal comparison report must exist.

Scope:
- Add stable JSON schema for CPU-vs-Metal performance reports.
- Ensure report field names match `ExecutionResult` execute timing fields.
- Add deterministic summary-statistics tests.
- Document how to interpret `elapsed_time_ms` and `run_elapsed_time_ms`.
- Update README only in the existing DSP example/index section.
- Keep default report mode informational.

Do not add strict performance gate; that is PR4.
Do not claim general GPU superiority.
Do not describe Metal DFT as GPU FFT.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/DSP_CPU_VS_METAL_PERFORMANCE_IMPL_PR3.md.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR3 from plan/roadmap/DSP_CPU_VS_METAL_PERFORMANCE_PR_ROADMAP.md: Execute-Timing Report Schema And Statistics Guardrails.

Use the verifier prompt from: plan/agents/DSP_CPU_VS_METAL_PERFORMANCE_PR_AGENTS.md

Required checks:
- Report schema exists and covers required fields from the roadmap.
- Report uses `ExecutionResult` execute timing field names.
- Statistics tests cover one and many measured iterations.
- Default report mode is informational.
- Docs explain total execute timing, run phase timing, warm-up behavior, and direct DFT vs FFT truth-in-labeling.
- README updates are limited to the DSP example/index section.
- No strict gate, unqualified performance claim, true Metal FFT, SAR/GOTCHA/CRSD dependency, or external benchmark dependency was added.

Stop after verifier report.
Save the report to plan/reviews/DSP_CPU_VS_METAL_PERFORMANCE_VERIFY_PR3.md.
```

---

## PR4: Optional Local Strict Performance Gate

### Implementer Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR4 from plan/roadmap/DSP_CPU_VS_METAL_PERFORMANCE_PR_ROADMAP.md: Optional Local Strict Performance Gate.

Use the implementer prompt from: plan/agents/DSP_CPU_VS_METAL_PERFORMANCE_PR_AGENTS.md

Prerequisites:
- PR1 consolidated `GraphExecutor::Execute()` timing must exist.
- PR2 CPU-vs-Metal comparison runner/report must exist.
- PR3 report schema must exist.

Scope:
- Add explicitly enabled local-only strict speedup gate.
- Gate must require `GRAPHX_DSP_REQUIRE_METAL_SPEEDUP=1`.
- Optional threshold may use `GRAPHX_DSP_MIN_METAL_SPEEDUP_RATIO`.
- Strict mode must use consolidated `GraphExecutor::Execute()` timing fields.
- Strict mode fails clearly when native Metal is unavailable, correctness/parity fails, or measured speedup is below threshold.
- Default mode must remain informational and CI-safe.
- Add tests proving the strict gate is disabled by default and local-only.

Do not make strict gate part of default CI.
Do not claim Metal is generally faster.
Do not implement true Metal FFT.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/DSP_CPU_VS_METAL_PERFORMANCE_IMPL_PR4.md.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR4 from plan/roadmap/DSP_CPU_VS_METAL_PERFORMANCE_PR_ROADMAP.md: Optional Local Strict Performance Gate.

Use the verifier prompt from: plan/agents/DSP_CPU_VS_METAL_PERFORMANCE_PR_AGENTS.md

Required checks:
- Strict gate requires explicit environment opt-in.
- Default mode never fails only because Metal is slower.
- Strict gate uses `GraphExecutor::Execute()` consolidated timing.
- Strict gate fails clearly for Metal unavailable, parity failure, or insufficient speedup.
- Strict report marks `mode: gate_enforced`.
- Any CTest lane is disabled or local-only and not default CI.
- No unqualified performance claim, true Metal FFT, SAR/GOTCHA/CRSD dependency, or external benchmark dependency was added.

Stop after verifier report.
Save the report to plan/reviews/DSP_CPU_VS_METAL_PERFORMANCE_VERIFY_PR4.md.
```

---

## PR5: Truth-In-Labeling Performance Documentation Audit

### Implementer Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR5 from plan/roadmap/DSP_CPU_VS_METAL_PERFORMANCE_PR_ROADMAP.md: Truth-In-Labeling Performance Documentation Audit.

Use the implementer prompt from: plan/agents/DSP_CPU_VS_METAL_PERFORMANCE_PR_AGENTS.md

Prerequisites:
- PR1-PR4 performance comparison work must already exist.

Scope:
- Add final guardrails proving active docs/tests avoid unqualified performance claims.
- Ensure docs say “measured on this host/config” or equivalent when discussing speedup.
- Ensure Metal DFT is never documented as GPU FFT.
- Ensure default CI does not require native Metal speedup.
- Ensure performance docs name `GraphExecutor::Execute()` consolidated timing as the comparison source.

Do not change algorithm behavior.
Do not add new benchmark features.
Do not implement true Metal FFT.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/DSP_CPU_VS_METAL_PERFORMANCE_IMPL_PR5.md.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR5 from plan/roadmap/DSP_CPU_VS_METAL_PERFORMANCE_PR_ROADMAP.md: Truth-In-Labeling Performance Documentation Audit.

Use the verifier prompt from: plan/agents/DSP_CPU_VS_METAL_PERFORMANCE_PR_AGENTS.md

Required checks:
- Guardrail tests cover unqualified performance claims.
- Docs qualify speedup as measured on host/config.
- Docs do not imply general GPU superiority.
- Metal DFT is not documented as GPU FFT.
- Default CI does not require native Metal speedup.
- Performance docs name `GraphExecutor::Execute()` consolidated timing as the comparison source.
- No algorithm behavior change, new benchmark feature, true Metal FFT, SAR/GOTCHA/CRSD dependency, or external benchmark dependency was added.

Stop after verifier report.
Save the report to plan/reviews/DSP_CPU_VS_METAL_PERFORMANCE_VERIFY_PR5.md.
```
