# Branch Protection Recommendations

This repository uses CI workflows that should be required before merging to `main`.

## Recommended Required Status Checks

Enable branch protection for `main` and require these checks:

1. `Package Smoke / package-smoke (macos-latest)`
2. `Package Smoke / package-smoke (ubuntu-latest)`
3. `Libgraph Unit / libgraph-unit`

## Automated Setup

Apply these settings with GitHub CLI:

```bash
scripts/apply_branch_protection.sh main
```

Requirements:

1. `gh` installed
2. Authenticated session (`gh auth login`)
3. Admin permissions on the repository

## Recommended Protection Settings

1. Require a pull request before merging
2. Require approvals: at least 1
3. Dismiss stale approvals when new commits are pushed
4. Require status checks to pass before merging
5. Require branches to be up to date before merging
6. Restrict direct pushes to `main`

## Why These Checks

- Package Smoke validates install/export and downstream `find_package(GraphX)` consumption.
- Libgraph Unit validates core runtime behavior for the main test target.
- Together, they reduce regression risk across packaging and execution paths.

## Maintenance Notes

- If workflow or job names change, update required check names in GitHub branch protection settings.
- Keep this file aligned with `.github/workflows/package-smoke.yml` and `.github/workflows/libgraph-unit.yml`.
