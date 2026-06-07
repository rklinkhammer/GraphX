#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: view_sar_tiles.sh [options]

Render SAR visualization PGM tiles as ASCII frames in the terminal.

Options:
  -i, --input DIR     Input directory containing .pgm files (default: sar_viz_output)
  -f, --fps N         Frames per second (default: 4)
  -s, --step N        Pixel stride for downsampling (default: 2)
  -l, --loop N        Number of playback loops (default: 1, use 0 for infinite)
  --list              List frames and exit
  -h, --help          Show this help text

Examples:
  ./examples/SAR/tools/view_sar_tiles.sh
  ./examples/SAR/tools/view_sar_tiles.sh --input sar_viz_output --fps 8 --step 1
  ./examples/SAR/tools/view_sar_tiles.sh --loop 0
EOF
}

input_dir="sar_viz_output"
fps=4
step=2
loop_count=1
list_only=false

while [[ $# -gt 0 ]]; do
  case "$1" in
    -i|--input)
      input_dir="$2"
      shift 2
      ;;
    -f|--fps)
      fps="$2"
      shift 2
      ;;
    -s|--step)
      step="$2"
      shift 2
      ;;
    -l|--loop)
      loop_count="$2"
      shift 2
      ;;
    --list)
      list_only=true
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

if [[ "$fps" -le 0 ]]; then
  echo "fps must be > 0" >&2
  exit 1
fi

if [[ "$step" -le 0 ]]; then
  echo "step must be > 0" >&2
  exit 1
fi

if [[ "$loop_count" -lt 0 ]]; then
  echo "loop must be >= 0" >&2
  exit 1
fi

shopt -s nullglob
frames=("$input_dir"/*.pgm)
if [[ ${#frames[@]} -eq 0 ]]; then
  echo "No .pgm frames found in: $input_dir" >&2
  exit 1
fi

IFS=$'\n' read -r -d '' -a frames < <(printf '%s\n' "${frames[@]}" | sort && printf '\0')

if [[ "$list_only" == true ]]; then
  printf '%s\n' "${frames[@]}"
  exit 0
fi

delay=$(awk -v f="$fps" 'BEGIN { printf "%.3f", 1.0 / f }')

render_frame() {
  local frame="$1"
  awk -v step="$step" '
    BEGIN {
      ramp = " .:-=+*#%@";
    }
    $1 ~ /^#/ { next }
    {
      for (i = 1; i <= NF; ++i) {
        token = $i;
        if (magic == "") {
          magic = token;
          next;
        }
        if (width == "") {
          width = token;
          next;
        }
        if (height == "") {
          height = token;
          next;
        }
        if (maxv == "") {
          maxv = token;
          next;
        }
        pixels[count++] = token;
      }
    }
    END {
      if (magic != "P2" || width == "" || height == "" || maxv == "" || count == 0) {
        print "Invalid or unsupported PGM frame" > "/dev/stderr";
        exit 2;
      }

      for (y = 0; y < height; y += step) {
        line = "";
        for (x = 0; x < width; x += step) {
          idx = (y * width) + x;
          v = pixels[idx] + 0;
          norm = (maxv > 0) ? (v / maxv) : 0;
          ci = int(norm * 9);
          if (ci < 0) ci = 0;
          if (ci > 9) ci = 9;
          line = line substr(ramp, ci + 1, 1);
        }
        print line;
      }
    }
  ' "$frame"
}

loop_idx=0
while [[ "$loop_count" -eq 0 || "$loop_idx" -lt "$loop_count" ]]; do
  for frame in "${frames[@]}"; do
    clear
    echo "SAR Tile Viewer"
    echo "Frame: $(basename "$frame")"
    echo "Input: $input_dir"
    echo
    render_frame "$frame"
    sleep "$delay"
  done
  loop_idx=$((loop_idx + 1))
done
