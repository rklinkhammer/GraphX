# Utils

## Low-Credit Prompt Template

```text
Goal:
[One-sentence outcome]

Scope:
- Files allowed: [file1, file2, ...]
- Do NOT explore outside these files unless blocked.
- Keep changes minimal and local.

Actions:
1) Implement the change.
2) Run only these checks:
   - Build target: [target]
   - Tests: [exact test names/files]
3) If checks pass, commit with message: "[message]"
4) Push branch and post one PR comment with summary + evidence.

Constraints:
- No broad search or full-suite tests.
- No refactors unrelated to goal.
- Keep response short: changed files + commands run + pass/fail.
- If blocked, stop and ask exactly one question.

Context:
- Branch: [branch]
- PR: [number/url]
- Assume previous setup/build is valid unless this task requires otherwise.
```

## Bugfix Variant

```text
Goal:
[Fix one specific bug]

Scope:
- Files allowed: [exact file list]
- Do not search beyond the bug's immediate code path unless blocked.
- Preserve current behavior everywhere else.

Actions:
1) Reproduce or inspect the failure locally.
2) Make the smallest fix at the root cause.
3) Run only the narrowest test or build target that proves the fix.
4) If green, commit and push the change.

Constraints:
- No unrelated cleanup.
- No broad refactors.
- If the fix touches more than [N] files, stop and explain why.
```

## Docs-Only Variant

```text
Goal:
[Update documentation for one feature]

Scope:
- Files allowed: [docs/README/guide paths only]
- Do not modify code or tests unless a doc mismatch is blocking accuracy.
- Keep wording concise and user-facing.

Actions:
1) Update the relevant docs.
2) Verify links, commands, and examples are accurate.
3) Skip builds/tests unless you changed executable instructions.
4) Commit and push the doc update.

Constraints:
- No code changes.
- No broad search.
- Keep the response to changed docs and any verification performed.
```

## PR Review Variant

```text
Goal:
[Review a PR for correctness and risk]

Scope:
- Files/PR: [PR number or branch]
- Focus on bugs, regressions, missing tests, and risky assumptions.
- Do not suggest large rewrites unless they are necessary to fix a defect.

Actions:
1) Inspect the diff and any touched tests.
2) Identify the highest-risk issues first.
3) Report findings with severity, file references, and rationale.
4) If there are no issues, explicitly say so and note residual risks.

Constraints:
- No implementation changes unless explicitly requested.
- Keep the review concise and evidence-based.
```

## SAR-Focused Low-Credit Prompt

```text
Goal:
Convert all SAR nodes and related files to use the GPU node model.   Refer to the nodes in libgpu and the model described in /Users/rklinkhammer/workspace/GraphX/doc/architecture/CUDA_GRAPH_NODE_IMPLEMENTATION_PLAN.md

Scope:
- Files allowed: examples/SAR/include/sar/[...], examples/SAR/src/[...], examples/SAR/test/[...]
- Do NOT explore outside SAR unless blocked.
- Prefer the existing gpu node/plugin/test patterns.  no new framework layer.

Actions:
1) Existing SAR implementation must be completely replaced.
2) Run only the touched SAR build/test target(s).
3) If green, commit and push to the current PR branch.
4) Post a concise PR update with the exact tests run.

Constraints:
- No full-suite runs.
- No unrelated refactors.
- Keep the response short and factual.
```

## PR Update Snippet

```text
Implemented [feature/fix] in [commit].

Validation:
- [build command]
- [test command]
- exit code: 0

Notes:
- [one-line risk/behavior note]
```

## Log4CXX Quick Toggle Snippet

```properties
log4j.logger.graph.GraphExecutor=TRACE
# log4j.logger.graphlib.scheduler=DEBUG
# log4j.logger.graphlib.plugin=INFO
# log4j.logger.graphlib.edge=DEBUG
# log4j.logger.graphlib.node=DEBUG
# log4j.logger.graph.graph=TRACE
# log4j.logger.graphlib.visualization=INFO
# log4j.logger.app.policies=TRACE
```
