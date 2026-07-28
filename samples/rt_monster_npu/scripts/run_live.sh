#!/usr/bin/env bash

set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

require_model
python_bin="$(find_openvino_python)"
work_dir="${RT_MONSTER_BUILD_DIR:-${demo_root}/build}"
build_dir="${work_dir}/demo"

openvino_cmake="$(${python_bin} -c \
  'from pathlib import Path; import openvino; print(Path(openvino.__file__).parent / "cmake")')"

sdk_cmake="${MYNTEYED_CMAKE_DIR:-}"
if [[ -z "${sdk_cmake}" ]]; then
  sdk_build="${work_dir}/sdk-build"
  sdk_install="${work_dir}/sdk-install"
  sdk_cmake="${sdk_install}/lib/cmake/mynteyed"
  if [[ ! -f "${sdk_cmake}/mynteyed-config.cmake" ||
        ! -e "${sdk_install}/lib/libmynteye_depth.so" ||
        ! -e "${sdk_install}/lib/3rdparty/libeSPDI.so" ]]; then
    cmake -S "${sdk_root}" -B "${sdk_build}" \
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX="${sdk_install}"
    cmake --build "${sdk_build}" -j"$(nproc)"
    cmake --install "${sdk_build}"
  fi
fi

cmake -S "${demo_root}" -B "${build_dir}" \
  -DCMAKE_BUILD_TYPE=Release \
  -Dmynteyed_DIR="${sdk_cmake}" \
  -DOpenVINO_DIR="${openvino_cmake}"
cmake --build "${build_dir}" --target mynteye_rt_monster -j"$(nproc)"

openvino_libs="$(${python_bin} -c \
  'from pathlib import Path; import openvino; print(Path(openvino.__file__).parent / "libs")')"
sdk_prefix="$(cd "${sdk_cmake}/../../.." && pwd)"
export LD_LIBRARY_PATH="${sdk_prefix}/lib:${sdk_prefix}/lib/3rdparty:${openvino_libs}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}"

exec "${build_dir}/mynteye_rt_monster" --blob "${model_path}" "$@"
