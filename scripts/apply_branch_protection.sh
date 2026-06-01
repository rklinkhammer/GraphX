#!/usr/bin/env bash
set -euo pipefail

# Apply GraphX branch protection defaults using GitHub CLI.
#
# Usage:
#   scripts/apply_branch_protection.sh [branch]
#
# Example:
#   scripts/apply_branch_protection.sh main

branch="${1:-main}"

if ! command -v gh >/dev/null 2>&1; then
  echo "error: GitHub CLI 'gh' is required but not installed." >&2
  exit 1
fi

if ! gh auth status >/dev/null 2>&1; then
  echo "error: gh is not authenticated. Run: gh auth login" >&2
  exit 1
fi

repo="$(gh repo view --json nameWithOwner --jq .nameWithOwner)"

echo "Applying branch protection to ${repo}:${branch}"

gh api \
  --method PUT \
  -H "Accept: application/vnd.github+json" \
  "repos/${repo}/branches/${branch}/protection" \
  --input - <<'JSON'
{
  "required_status_checks": {
    "strict": true,
    "contexts": [
      "Package Smoke / package-smoke (macos-latest)",
      "Package Smoke / package-smoke (ubuntu-latest)",
      "Libgraph Unit / libgraph-unit"
    ]
  },
  "enforce_admins": true,
  "required_pull_request_reviews": {
    "dismiss_stale_reviews": true,
    "require_code_owner_reviews": false,
    "required_approving_review_count": 1,
    "require_last_push_approval": false
  },
  "required_conversation_resolution": true,
  "restrictions": null,
  "allow_force_pushes": false,
  "allow_deletions": false,
  "block_creations": false,
  "required_linear_history": false,
  "lock_branch": false,
  "allow_fork_syncing": true
}
JSON

echo "Branch protection successfully applied to ${repo}:${branch}"