#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

forbidden_filename_regex='(pr|PR|rrp|RRP)[0-9]+|SAR_IMPL_PR|SAR_VERIFY_PR|SAR_PR[0-9]+|SAR_RRP[0-9]+'
forbidden_content_regex='\b(pr[0-9]+|PR[0-9]+|rrp[0-9]+|RRP[0-9]+)\b|SAR_IMPL_PR|SAR_VERIFY_PR|SAR_PR[0-9]+|SAR_RRP[0-9]+'

is_allowed_historical_path() {
    local path="$1"
    case "$path" in
        plan/history/*) return 0 ;;
        plan/old/*) return 0 ;;
        plan/agents/*) return 0 ;;
        plan/prompt\ examples/*) return 0 ;;
        plan/SAR_SIMPLIFIER_REPORT.md) return 0 ;;
        plan/SAR_NAMING_CLEANUP_PLANNER_REPORT.md) return 0 ;;
        plan/reviews/SAR_PLANNER_REPORT.md) return 0 ;;
        plan/reviews/SAR_GOTCHA_TO_CRSD_CURRENT_STATE.md) return 0 ;;
        doc/architecture/CUDA_GRAPH_NODE_IMPLEMENTATION_PLAN.md) return 0 ;;
        *) return 1 ;;
    esac
}

is_generated_or_binary_path() {
    local path="$1"
    case "$path" in
        .git/*) return 0 ;;
        build/*) return 0 ;;
        build-*/*) return 0 ;;
        Testing/*) return 0 ;;
        examples/SAR/tools/__pycache__/*) return 0 ;;
        examples/SAR/datasets/*) return 0 ;;
        external/*) return 0 ;;
        third_party/*) return 0 ;;
        *.pyc|*.o|*.a|*.so|*.dylib|*.dll|*.exe|*.npy|*.bin|*.png|*.jpg|*.jpeg|*.gif|*.webp|*.pdf|*.zip|*.tar|*.gz|*.bz2|*.xz|*.7z)
            return 0
            ;;
        *) return 1 ;;
    esac
}

scan_paths_file="$(mktemp)"
filename_hits_file="$(mktemp)"
content_hits_file="$(mktemp)"
trap 'rm -f "$scan_paths_file" "$filename_hits_file" "$content_hits_file"' EXIT

while IFS= read -r -d '' path; do
    if is_allowed_historical_path "$path" || is_generated_or_binary_path "$path"; then
        continue
    fi

    if [[ ! -e "$path" ]]; then
        continue
    fi

    printf '%s\0' "$path" >> "$scan_paths_file"

    if [[ "$path" =~ $forbidden_filename_regex ]]; then
        echo "$path" >> "$filename_hits_file"
    fi
done < <(git ls-files -z)

if [[ -s "$scan_paths_file" ]]; then
    xargs -0 rg -n -H --no-heading --color never -I -e "$forbidden_content_regex" \
        < "$scan_paths_file" > "$content_hits_file" || true
fi

if [[ -s "$filename_hits_file" || -s "$content_hits_file" ]]; then
    echo "SAR naming hygiene lint failed. Forbidden planning-era tokens detected outside allowed historical paths." >&2
    if [[ -s "$filename_hits_file" ]]; then
        echo >&2
        echo "Filename hits:" >&2
        sort -u "$filename_hits_file" >&2
    fi
    if [[ -s "$content_hits_file" ]]; then
        echo >&2
        echo "Content hits:" >&2
        cat "$content_hits_file" >&2
    fi
    exit 1
fi

echo "SAR naming hygiene lint passed."
