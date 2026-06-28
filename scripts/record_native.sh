#!/usr/bin/env bash
# Record MYNT EYE camera streams and IMU directly with the SDK sample.

set -e

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
timestamp="${MYNTEYE_RECORD_STAMP:-$(date +%Y%m%d_%H%M%S)}"
out_dir_arg="${1:-${MYNTEYE_RECORD_DIR:-recordings/native/}}"
duration="${MYNTEYE_RECORD_DURATION:-0}"
dev_index="${MYNTEYE_DEV_INDEX:-0}"
stream_mode="${MYNTEYE_STREAM_MODE:-3}"
framerate="${MYNTEYE_FRAMERATE:-30}"
save_rate="${MYNTEYE_SAVE_RATE:-0}"
sync_save="${MYNTEYE_SYNC_SAVE:-false}"

stamp_path() {
  local path="$1"

  if [ "${MYNTEYE_RECORD_EXACT_PATH:-false}" = "true" ]; then
    printf '%s\n' "$path"
    return
  fi

  case "$path" in
    */) printf '%s%s\n' "$path" "$timestamp" ;;
    *_????????_??????) printf '%s\n' "$path" ;;
    *) printf '%s_%s\n' "$path" "$timestamp" ;;
  esac
}

out_dir_arg="$(stamp_path "$out_dir_arg")"
case "$out_dir_arg" in
  /*) out_dir="$out_dir_arg" ;;
  *) out_dir="$repo_dir/$out_dir_arg" ;;
esac

echo "Recording native dataset to: $out_dir"
cd "$repo_dir"

if [ ! -f _install/lib/cmake/mynteyed/mynteyed-config.cmake ]; then
  echo "_install is missing; building and installing SDK first."
  cmake -S . -B _build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$repo_dir/_install"
  cmake --build _build -j"${MYNTEYE_BUILD_JOBS:-2}"
  cmake --install _build --prefix "$repo_dir/_install"
fi

if [ ! -x samples/_output/bin/dataset/record ]; then
  echo "samples/_output/bin/dataset/record is missing; building samples first."
  cmake -S samples -B samples/_build -DCMAKE_BUILD_TYPE=Release
  cmake --build samples/_build --target record -j"${MYNTEYE_BUILD_JOBS:-2}"
fi

mkdir -p "$out_dir"

args=(
  --out "$out_dir"
  --duration "$duration"
  --dev-index "$dev_index"
  --stream-mode "$stream_mode"
  --framerate "$framerate"
  --save-rate "$save_rate"
  --left
  --right
  --depth
  --imu
  --no-display
)

if [ "$sync_save" = "true" ] || [ "$sync_save" = "1" ]; then
  args+=(--sync-save)
fi

exec samples/_output/bin/dataset/record "${args[@]}"
