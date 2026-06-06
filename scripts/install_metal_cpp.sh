#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "Usage: $0 <metal-cpp-archive-path-or-url> [destination]" >&2
  echo "Example: $0 ~/Downloads/metal-cpp_*.zip" >&2
  echo "Example: $0 https://example.com/metal-cpp.zip third_party/metal-cpp" >&2
  exit 1
fi

source_input="$1"
destination="${2:-third_party/metal-cpp}"

tmp_root="$(mktemp -d)"
cleanup() {
  rm -rf "$tmp_root"
}
trap cleanup EXIT

archive_path="$source_input"
if [[ "$source_input" =~ ^https?:// ]]; then
  archive_path="$tmp_root/metal-cpp-archive"
  curl -fL "$source_input" -o "$archive_path"
fi

if [[ ! -f "$archive_path" ]]; then
  echo "Archive not found: $archive_path" >&2
  exit 1
fi

extract_dir="$tmp_root/extracted"
mkdir -p "$extract_dir"

case "$archive_path" in
  *.zip)
    unzip -q "$archive_path" -d "$extract_dir"
    ;;
  *.tar.gz|*.tgz)
    tar -xzf "$archive_path" -C "$extract_dir"
    ;;
  *.tar)
    tar -xf "$archive_path" -C "$extract_dir"
    ;;
  *)
    echo "Unsupported archive format: $archive_path" >&2
    echo "Supported formats: .zip, .tar.gz, .tgz, .tar" >&2
    exit 1
    ;;
esac

metal_root="$(find "$extract_dir" -type f -path "*/Metal/Metal.hpp" -print -quit | xargs -I{} dirname "{}" | xargs -I{} dirname "{}")"
if [[ -z "$metal_root" ]]; then
  echo "Could not locate Metal/Metal.hpp in extracted archive." >&2
  exit 1
fi

mkdir -p "$(dirname "$destination")"
rm -rf "$destination"
mkdir -p "$destination"

cp -R "$metal_root"/* "$destination"/

if [[ ! -f "$destination/Metal/Metal.hpp" ]]; then
  echo "Install failed: $destination/Metal/Metal.hpp missing after copy." >&2
  exit 1
fi

echo "Installed metal-cpp headers to: $destination"
echo "Configure with: cmake --preset ninja-debug-metal-native -DGRAPHX_METAL_CPP_INCLUDE_DIR=$(cd "$destination" && pwd)"
