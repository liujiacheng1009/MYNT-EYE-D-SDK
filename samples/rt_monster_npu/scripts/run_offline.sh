#!/usr/bin/env bash

set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

require_model
python_bin="$(find_openvino_python)"

left="${RT_MONSTER_LEFT:-${demo_root}/test_data/left/*.png}"
right="${RT_MONSTER_RIGHT:-${demo_root}/test_data/right/*.png}"
output="${RT_MONSTER_OUTPUT:-${demo_root}/output}"

exec "${python_bin}" "${demo_root}/infer_openvino_npu.py" \
  --blob "${model_path}" --left "${left}" --right "${right}" \
  --output "${output}" "$@"
