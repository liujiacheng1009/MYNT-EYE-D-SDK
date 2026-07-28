# RT-MonSter++ Intel NPU demos

This directory contains two migrated demos for the MYNT EYE D camera and the
Intel NPU 3720:

- `infer_openvino_npu.py`: offline inference from matching left/right images;
- `mynteye_rt_monster`: live rectified stereo capture and NPU inference, with
  optional native-depth comparison and point-cloud display.

The model takes FP32 NCHW stereo inputs at `512x384`. The demos resize MYNT EYE
`640x480` frames before inference and restore disparity to the source size.

## Requirements

- Intel NPU visible to OpenVINO (`ov.Core().available_devices` contains `NPU`)
- OpenVINO 2026.1 Python package
- Python OpenCV and NumPy
- MYNT EYE D SDK and OpenCV development packages for the live C++ demo

Set the Python executable if OpenVINO is installed in a virtual environment:

```bash
export OPENVINO_PYTHON=/path/to/venv/bin/python
```

The precompiled blob is 119,922,688 bytes, which exceeds GitHub's normal
single-file limit and is therefore intentionally ignored. Place it at:

```text
models/rt_monster_384x512_npu3720.blob
```

Expected SHA256:

```text
2af07781b421dc7fb691d095d9bc1ee97a33947f47c2b3912964be990e9225c8
```

Alternatively, set `RT_MONSTER_BLOB` to its location.

## Offline demo

The repository includes one stereo test pair:

```bash
OPENVINO_PYTHON=/path/to/venv/bin/python \
  ./samples/rt_monster_npu/scripts/run_offline.sh
```

Set `RT_MONSTER_LEFT`, `RT_MONSTER_RIGHT`, and `RT_MONSTER_OUTPUT` to process
another data set. Image arguments may be shell-style glob patterns.

## Live camera demo

```bash
OPENVINO_PYTHON=/path/to/venv/bin/python \
  ./samples/rt_monster_npu/scripts/run_live.sh
```

Useful modes:

```bash
# Compare network depth with the MYNT EYE native depth stream.
./samples/rt_monster_npu/scripts/run_live.sh --compare-native

# Display the inferred RGB point cloud.
./samples/rt_monster_npu/scripts/run_live.sh --pointcloud
```

Press `q`/Escape to quit and `s` to save the current result.

## Regression test

The test imports the real blob on the NPU, validates the generated disparity,
builds the C++ demo, and checks its command-line entry point:

```bash
OPENVINO_PYTHON=/path/to/venv/bin/python \
  ./samples/rt_monster_npu/scripts/test.sh
```
