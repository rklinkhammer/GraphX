#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: view_sar_tiles_native.sh [options]

Open SAR visualization tiles as native grayscale images on macOS.
Optionally converts PGM frames to PNG files first.

Options:
  -i, --input DIR       Input directory containing .pgm files (default: sar_viz_output)
  --list                List discovered .pgm files and exit
  --open-dir            Open input directory in Finder and exit
  --open-first          Open first frame in Preview and exit
  --convert-png         Convert all .pgm frames to .png using sips
  -o, --output DIR      PNG output directory when using --convert-png (default: <input>/png)
  --open-png-dir        Open PNG output directory after conversion
  -h, --help            Show this help text

Examples:
  ./examples/SAR/tools/view_sar_tiles_native.sh --open-dir
  ./examples/SAR/tools/view_sar_tiles_native.sh --open-first
  ./examples/SAR/tools/view_sar_tiles_native.sh --convert-png --open-png-dir
EOF
}

input_dir="sar_viz_output"
list_only=false
open_dir=false
open_first=false
convert_png=false
output_dir=""
open_png_dir=false

while [[ $# -gt 0 ]]; do
  case "$1" in
    -i|--input)
      input_dir="$2"
      shift 2
      ;;
    --list)
      list_only=true
      shift
      ;;
    --open-dir)
      open_dir=true
      shift
      ;;
    --open-first)
      open_first=true
      shift
      ;;
    --convert-png)
      convert_png=true
      shift
      ;;
    -o|--output)
      output_dir="$2"
      shift 2
      ;;
    --open-png-dir)
      open_png_dir=true
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage
      exit 1
      ;;
  esac
done

if [[ ! -d "$input_dir" ]]; then
  echo "Input directory not found: $input_dir" >&2
  exit 1
fi

shopt -s nullglob
frames=("$input_dir"/*.pgm)
if [[ ${#frames[@]} -eq 0 ]]; then
  echo "No .pgm frames found in: $input_dir" >&2
  exit 1
fi

IFS=$'\n' read -r -d '' -a frames < <(printf '%s\n' "${frames[@]}" | sort -V && printf '\0')

if [[ "$list_only" == true ]]; then
  printf '%s\n' "${frames[@]}"
  exit 0
fi

if [[ "$open_dir" == true ]]; then
  open "$input_dir"
  exit 0
fi

if [[ "$open_first" == true ]]; then
  open "${frames[0]}"
  exit 0
fi

if [[ "$convert_png" == true ]]; then
  if [[ -z "$output_dir" ]]; then
    output_dir="$input_dir/png"
  fi
  mkdir -p "$output_dir"

  for frame in "${frames[@]}"; do
    base_name=$(basename "$frame" .pgm)
    sips -s format png "$frame" --out "$output_dir/$base_name.png" >/dev/null
  done

  echo "Converted ${#frames[@]} frame(s) to PNG in: $output_dir"

  if [[ "$open_png_dir" == true ]]; then
    open "$output_dir"
  fi

  exit 0
fi

echo "No action selected. Use --open-dir, --open-first, --convert-png, or --list."
usage
exit 1
