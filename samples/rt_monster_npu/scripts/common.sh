#!/usr/bin/env bash

set -euo pipefail

demo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
sdk_root="$(cd "${demo_root}/../.." && pwd)"
model_path="${RT_MONSTER_BLOB:-${demo_root}/models/rt_monster_384x512_npu3720.blob}"

find_openvino_python() {
  if [[ -n "${OPENVINO_PYTHON:-}" ]]; then
    printf '%s\n' "${OPENVINO_PYTHON}"
    return
  fi
  if python3 -c 'import openvino, cv2, numpy' >/dev/null 2>&1; then
    command -v python3
    return
  fi
  printf '%s\n' \
    "Set OPENVINO_PYTHON to a Python executable with openvino, opencv-python and numpy." >&2
  return 1
}
require_model() {
  if [[ ! -f "${model_path}" ]]; then
    printf 'Missing NPU blob: %s\n' "${model_path}" >&2
    printf '%s\n' \
      'Set RT_MONSTER_BLOB or place rt_monster_384x512_npu3720.blob in models/.' >&2
    return 1
  fi
}
