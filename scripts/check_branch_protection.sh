#!/usr/bin/env bash
set -euo pipefail

# Check GraphX branch protection policy drift using GitHub CLI.
#
# Usage:
#   scripts/check_branch_protection.sh [branch]
#
# Example:
#   scripts/check_branch_protection.sh main

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

expected_contexts=(
  "Package Smoke / package-smoke (macos-latest)"
  "Package Smoke / package-smoke (ubuntu-latest)"
  "Libgraph Unit / libgraph-unit"
)

mapfile -t actual_contexts < <(gh api \
  -H "Accept: application/vnd.github+json" \
  "repos/${repo}/branches/${branch}/protection" \
  --jq '.required_status_checks.contexts[]')

sort_lines() {
  printf '%s\n' "$@" | LC_ALL=C sort
}

expected_sorted="$(sort_lines "${expected_contexts[@]}")"
actual_sorted="$(sort_lines "${actual_contexts[@]}")"

strict="$(gh api -H "Accept: application/vnd.github+json" "repos/${repo}/branches/${branch}/protection" --jq '.required_status_checks.strict')"
enforce_admins="$(gh api -H "Accept: application/vnd.github+json" "repos/${repo}/branches/${branch}/protection" --jq '.enforce_admins.enabled')"
required_approvals="$(gh api -H "Accept: application/vnd.github+json" "repos/${repo}/branches/${branch}/protection" --jq '.required_pull_request_reviews.required_approving_review_count')"
dismiss_stale="$(gh api -H "Accept: application/vnd.github+json" "repos/${repo}/branches/${branch}/protection" --jq '.required_pull_request_reviews.dismiss_stale_reviews')"
conversation_resolution="$(gh api -H "Accept: application/vnd.github+json" "repos/${repo}/branches/${branch}/protection" --jq '.required_conversation_resolution.enabled')"
allow_force_pushes="$(gh api -H "Accept: application/vnd.github+json" "repos/${repo}/branches/${branch}/protection" --jq '.allow_force_pushes.enabled')"
allow_deletions="$(gh api -H "Accept: application/vnd.github+json" "repos/${repo}/branches/${branch}/protection" --jq '.allow_deletions.enabled')"

failures=0

if [[ "${expected_sorted}" != "${actual_sorted}" ]]; then
  echo "drift: required status-check contexts differ" >&2
  echo "expected:" >&2
  echo "${expected_sorted}" >&2
  echo "actual:" >&2
  echo "${actual_sorted}" >&2
  failures=$((failures + 1))
fi

if [[ "${strict}" != "true" ]]; then
  echo "drift: required_status_checks.strict should be true (actual: ${strict})" >&2
  failures=$((failures + 1))
fi

if [[ "${enforce_admins}" != "true" ]]; then
  echo "drift: enforce_admins.enabled should be true (actual: ${enforce_admins})" >&2
  failures=$((failures + 1))
fi

if [[ "${required_approvals}" != "1" ]]; then
  echo "drift: required_approving_review_count should be 1 (actual: ${required_approvals})" >&2
  failures=$((failures + 1))
fi

if [[ "${dismiss_stale}" != "true" ]]; then
  echo "drift: dismiss_stale_reviews should be true (actual: ${dismiss_stale})" >&2
  failures=$((failures + 1))
fi

if [[ "${conversation_resolution}" != "true" ]]; then
  echo "drift: required_conversation_resolution.enabled should be true (actual: ${conversation_resolution})" >&2
  failures=$((failures + 1))
fi

if [[ "${allow_force_pushes}" != "false" ]]; then
  echo "drift: allow_force_pushes.enabled should be false (actual: ${allow_force_pushes})" >&2
  failures=$((failures + 1))
fi

if [[ "${allow_deletions}" != "false" ]]; then
  echo "drift: allow_deletions.enabled should be false (actual: ${allow_deletions})" >&2
  failures=$((failures + 1))
fi

if [[ "${failures}" -gt 0 ]]; then
  echo "Branch protection drift detected for ${repo}:${branch} (${failures} issue(s))." >&2
  exit 1
fi

echo "Branch protection matches GraphX policy for ${repo}:${branch}"