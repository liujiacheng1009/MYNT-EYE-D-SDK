#!/usr/bin/env bash

set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

require_model
expected_blob_sha="2af07781b421dc7fb691d095d9bc1ee97a33947f47c2b3912964be990e9225c8"
actual_blob_sha="$(sha256sum "${model_path}" | cut -d' ' -f1)"
[[ "${actual_blob_sha}" == "${expected_blob_sha}" ]] || {
  printf 'Blob checksum mismatch: %s\n' "${actual_blob_sha}" >&2
  exit 1
}

python_bin="$(find_openvino_python)"
test_output="$(mktemp -d "${TMPDIR:-/tmp}/mynteye-rt-monster-test.XXXXXX")"
trap 'rm -rf "${test_output}"' EXIT

OPENVINO_PYTHON="${python_bin}" RT_MONSTER_OUTPUT="${test_output}" \
  "${demo_root}/scripts/run_offline.sh"

"${python_bin}" - "${test_output}" <<'PY'
from pathlib import Path
import sys

import cv2
import numpy as np

root = Path(sys.argv[1])
disparity = np.load(root / "000000.npy")
assert disparity.shape == (480, 640), disparity.shape
assert disparity.dtype == np.float32, disparity.dtype
assert np.isfinite(disparity).all()
assert (root / "000000_disp16.png").is_file()
assert cv2.imread(str(root / "000000_color.png")).shape[:2] == (480, 640)
print("Offline NPU output validation: PASS")
PY

OPENVINO_PYTHON="${python_bin}" "${demo_root}/scripts/run_live.sh" --help >/dev/null
printf '%s\n' 'C++ live demo build/help test: PASS'
