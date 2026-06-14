# SAR Naming Cleanup Implementor And Verifier Agents

Source roadmap: `plan/SAR_NAMING_CLEANUP_PLANNER_REPORT.md`

Use one implementor prompt and one verifier prompt per PR. Each agent must read:

- `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`
- `plan/SAR_SIMPLIFIER_REPORT.md`
- `plan/SAR_NAMING_CLEANUP_PLANNER_REPORT.md`

Global constraints for every agent:

- Do not redesign GraphX runtime contracts.
- Backward compatibility is not required.
- Complexity is a defect.
- Prefer deletion over compatibility.
- Final names must describe product behavior or capability, not PR/RRP history.
- Preserve final GOTCHA, CRSD, graphx-crsd-lite, SarPy validation, local-only GOTCHA, and image comparison behavior.
- Preserve `graphx-crsd-lite` as a permanent NON-STANDARD format.
- Keep local-only dataset workflows optional and out of CI.
- Do not combine PR scopes.
- Do not start future PR work.

---

## PR1: Quarantine Historical Planning Artifacts

### Implementor Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR1 from plan/SAR_NAMING_CLEANUP_PLANNER_REPORT.md: Quarantine Historical Planning Artifacts.

Scope:
- Move SAR implementation/verifier history files out of active plan/reviews and into plan/history/reviews.
- Move only the historical implementation/verifier artifacts identified by the planner.
- Preserve active architectural reports and policy artifacts in plan/reviews.
- Update any narrow plan index/reference only if it directly points to the moved files.

Do not rename active code, tests, tools, docs, configs, CMake definitions, or user-visible strings.
Do not add linting.
Do not delete historical reports.
Do not alter runtime behavior.

Output the standard IMPLEMENTER summary.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR1 from plan/SAR_NAMING_CLEANUP_PLANNER_REPORT.md.

Required checks:
- SAR_IMPL_PR* and SAR_VERIFY_PR* reports that were in plan/reviews are now under plan/history/reviews.
- plan/reviews retains active architecture/policy reports only.
- No active code, test, tool, config, CMake, or runtime behavior was changed.
- No historical report content was rewritten as product documentation.
- No naming lint was added yet.

Stop after verifier report.
```

---

## PR2: Delete Tracked Cache And Intermediate-Only Config Tests

### Implementor Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR2 from plan/SAR_NAMING_CLEANUP_PLANNER_REPORT.md: Delete Tracked Cache And Intermediate-Only Config Tests.

Scope:
- Delete tracked Python cache artifacts under examples/SAR/tools/__pycache__ that carry rrp naming.
- Delete intermediate-only SAR PR config tests identified by the planner only after confirming equivalent behavior coverage remains.
- Update examples/SAR/test/CMakeLists.txt to remove deleted test sources.
- Run the focused/full SAR unit test command from the planner.

Do not rename retained tests yet.
Do not rename tools or configs yet.
Do not rewrite docs.
Do not add naming lint.
Do not remove capability coverage for accel-token, resolver, definitive config, or examples/SAR/main.cpp.

Output the standard IMPLEMENTER summary.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR2 from plan/SAR_NAMING_CLEANUP_PLANNER_REPORT.md.

Required checks:
- The tracked __pycache__ files identified by PR2 are gone.
- The intermediate-only tests identified by PR2 are gone.
- examples/SAR/test/CMakeLists.txt no longer references deleted files.
- Equivalent final behavior coverage remains in behavior-oriented tests.
- Full SAR unit binary passes, or any failure is clearly unrelated and documented.
- No broad renames, docs cleanup, config rename, or lint work was added.

Stop after verifier report.
```

---

## PR3: Rename Active RRP Tooling To Capability Names

### Implementor Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR3 from plan/SAR_NAMING_CLEANUP_PLANNER_REPORT.md: Rename Active RRP Tooling To Capability Names.

Scope:
- Rename active examples/SAR/tools rrp* scripts, markdown docs, and schema files to capability names exactly as planned.
- Update imports, command examples, tests, CMake compile definitions, and docs that reference the renamed tool paths.
- Update local_gotcha_validation.md title/body to remove PR18 wording while preserving local-only gating.
- Ensure no rrp/RRP tokens remain in active examples/SAR/tools filenames or active tool/doc contents.

Do not rename C++ test files yet.
Do not rename SAR JSON configs yet.
Do not delete additional tests.
Do not add naming lint.
Do not change local-only workflow requirements.

Output the standard IMPLEMENTER summary.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR3 from plan/SAR_NAMING_CLEANUP_PLANNER_REPORT.md.

Required checks:
- The planned examples/SAR/tools files were renamed to capability names.
- Old rrp* tool/doc/schema filenames are gone.
- examples/SAR/test/CMakeLists.txt compile definitions use capability names for tool paths.
- Imports and command examples reference the new tool names.
- No rrp/RRP tokens remain in active examples/SAR/tools filenames or active tool/doc contents.
- Existing tool-related tests pass.
- No C++ test rename, config rename, broad docs cleanup, or lint work was added.

Stop after verifier report.
```

---

## PR4: Rename Active PR/RRP Test Files And Suites

### Implementor Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR4 from plan/SAR_NAMING_CLEANUP_PLANNER_REPORT.md: Rename Active PR/RRP Test Files And Suites.

Scope:
- Rename retained PR/RRP-named test files and sar_pr7_parity_fixture.hpp to the capability names in the planner.
- Rename test suite names, namespace aliases, skip messages, and active string literals in those tests to capability names.
- Update examples/SAR/test/CMakeLists.txt to point to the new test filenames.
- Preserve all retained behavior coverage.
- Run the full SAR unit binary.

Do not rename SAR JSON configs yet.
Do not rewrite product docs except references required for renamed test files.
Do not delete additional tests.
Do not add naming lint.
Do not change local-only test gating.

Output the standard IMPLEMENTER summary.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR4 from plan/SAR_NAMING_CLEANUP_PLANNER_REPORT.md.

Required checks:
- All planned retained PR/RRP test files were renamed to capability filenames.
- Old retained PR/RRP test filenames are gone.
- examples/SAR/test/CMakeLists.txt references the new filenames only.
- Test suite names, namespace aliases, skip messages, and active string literals in renamed tests no longer use PR/RRP naming.
- Full SAR unit binary passes, or any failure is clearly unrelated and documented.
- No SAR config rename, product-doc cleanup, or lint work was added.

Stop after verifier report.
```

---

## PR5: Rename SAR Configs And Compile Definitions

### Implementor Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR5 from plan/SAR_NAMING_CLEANUP_PLANNER_REPORT.md: Rename SAR Configs And Compile Definitions.

Scope:
- Rename active SAR JSON config files from prXX names to capability names exactly as planned.
- Delete obsolete definitive metal/nonmetal variants after preserving required definitive behavior coverage.
- Update examples/SAR/test/CMakeLists.txt compile definitions from SAR_PR*/SAR_RRP* names to capability names.
- Update tests, README snippets, scripts, and docs that reference old config filenames or compile definition names.
- Ensure examples/SAR/main.cpp still runs with sar_stripmap_definitive.json.
- Run the full SAR unit binary.

Do not rename tools or tests beyond direct references required by this PR.
Do not add naming lint.
Do not redesign config semantics.
Do not remove graphx-crsd-lite or CRSD behavior.

Output the standard IMPLEMENTER summary.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR5 from plan/SAR_NAMING_CLEANUP_PLANNER_REPORT.md.

Required checks:
- Active SAR config filenames no longer contain pr/PR tokens.
- Obsolete definitive metal/nonmetal variants are removed or explicitly justified if kept under capability names.
- examples/SAR/test/CMakeLists.txt no longer defines SAR_PR* or SAR_RRP* compile definitions.
- Tests/docs/scripts reference new config names.
- examples/SAR/main.cpp still runs with sar_stripmap_definitive.json.
- Full SAR unit binary passes, or any failure is clearly unrelated and documented.
- No product-doc sweep or naming lint was added beyond config references.

Stop after verifier report.
```

---

## PR6: Clean Product Docs, Benchmark Output, Comments, And Scenario Text

### Implementor Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR6 from plan/SAR_NAMING_CLEANUP_PLANNER_REPORT.md: Clean Product Docs, Benchmark Output, Comments, And Scenario Text.

Scope:
- Remove planning-era PR/RRP wording from product-facing SAR docs, benchmark output labels, source comments, SarPy tool docs, and scenario text identified by the planner.
- Rewrite wording to describe final capabilities and architecture invariants directly.
- Keep historical context only by moving it to or leaving it under allowed historical plan paths.
- Preserve benchmark trace/schema meaning while renaming PR-labeled output fields/strings to capability labels.
- Preserve all runtime behavior.

Do not rename active files unless required by this PR's direct docs cleanup.
Do not add naming lint.
Do not change CRSD, graphx-crsd-lite, GOTCHA, SarPy, or local-only workflow behavior.

Output the standard IMPLEMENTER summary.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR6 from plan/SAR_NAMING_CLEANUP_PLANNER_REPORT.md.

Required checks:
- Product docs describe final capabilities, not PR/RRP milestones.
- Benchmark stdout and trace-facing strings no longer use PR/RRP labels.
- Source comments describe architecture invariants directly without PR/RRP references.
- Scenario docs remove RRP0 language and preserve CI-safe fixture boundaries.
- tools/sarpy/README.md no longer presents SarPy tooling as PR13/PR14/PR17 work.
- Existing benchmark trace/schema and SAR unit tests pass, or any failure is clearly unrelated and documented.
- No naming lint was added.

Stop after verifier report.
```

---

## PR7: Add Naming Hygiene Lint And Final Verification Gate

### Implementor Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR7 from plan/SAR_NAMING_CLEANUP_PLANNER_REPORT.md: Add Naming Hygiene Lint And Final Verification Gate.

Scope:
- Add a CI-safe naming hygiene lint that scans active filenames and file contents for forbidden planning-era tokens.
- Exclude generated build directories, .git, binary artifacts, external datasets, and allowed historical plan paths.
- Register the lint in the normal build/test path via CMake/CTest or the existing project test mechanism.
- Use the forbidden-token and allowed-path policy from the planner.
- Run the lint and full SAR unit binary.

Do not perform broad cleanup beyond the minimal fixes needed for the lint to pass.
Do not rewrite historical files in allowed history paths.
Do not weaken the lint to pass active PR/RRP names.

Output the standard IMPLEMENTER summary.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR7 from plan/SAR_NAMING_CLEANUP_PLANNER_REPORT.md.

Required checks:
- Naming hygiene lint exists and scans both filenames and file contents.
- Lint rejects active prXX/PRXX/rrpXX/RRPXX/SAR_IMPL_PR/SAR_VERIFY_PR/SAR_PRXX/SAR_RRPXX tokens.
- Lint excludes only the allowed historical/generated paths documented by the planner.
- Lint is part of the normal build/test path.
- Lint passes on the current tree after PR1-PR6.
- Full SAR unit binary passes, or any failure is clearly unrelated and documented.
- No historical plan files were rewritten merely to satisfy lint.

Stop after verifier report.
```
