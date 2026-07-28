# MYNT® EYE D SDK

[![](https://img.shields.io/badge/MYNT%20EYE%20D%20SDK-v1.9.0-brightgreen.svg?style=flat)](https://github.com/slightech/MYNT-EYE-D-SDK)


MYNT® EYE D SDK is a library for MYNT® EYE Depth cameras.

## Support platforms

* Linux x64 & aarch64
  * Tested on PC and TX2, with Ubuntu 16.04 (GCC 5).
* Windows x64
  * Tested on PC with Windows 10 and Visual Studio 2017.

## Documentations

API reference and the guide documentations.

* en: [![](https://img.shields.io/badge/Download-PDF-blue.svg?style=flat)](https://readthedocs.org/projects/mynt-eye-d-sdk/downloads/pdf/latest/) [![](https://img.shields.io/badge/Download-HTML-blue.svg?style=flat)](https://readthedocs.org/projects/mynt-eye-d-sdk/downloads/htmlzip/latest/) [![](https://img.shields.io/badge/Online-HTML-blue.svg?style=flat)](http://mynt-eye-d-sdk.rtfd.io/)
* zh-Hans: [![](https://img.shields.io/badge/Download-PDF-blue.svg?style=flat)](https://readthedocs.org/projects/mynt-eye-d-sdk-docs-zh-cn/downloads/pdf/latest/) [![](https://img.shields.io/badge/Download-HTML-blue.svg?style=flat)](https://readthedocs.org/projects/mynt-eye-d-sdk-docs-zh-cn/downloads/htmlzip/latest/) [![](https://img.shields.io/badge/Online-HTML-blue.svg?style=flat)](https://mynt-eye-d-sdk.rtfd.io/zh_CN/latest/)

### Quick Start Guide

* en:
  * [Ubuntu Source Installation](https://mynt-eye-d-sdk.rtfd.io/en/latest/sdk/install_ubuntu_src.html)
  * [Windows Source Installation](https://mynt-eye-d-sdk.rtfd.io/en/latest/sdk/install_win_src.html)
    * [Windows EXE Installation](https://mynt-eye-d-sdk.rtfd.io/en/latest/sdk/install_win_exe.html)
* zh-Hans:
  * [Ubuntu 源码安装](https://mynt-eye-d-sdk.rtfd.io/zh_CN/latest/sdk/install_ubuntu_src.html)
  * [Windows 源码安装](https://mynt-eye-d-sdk.rtfd.io/zh_CN/latest/sdk/install_win_src.html)
    * [Windows EXE 安装](https://mynt-eye-d-sdk.rtfd.io/zh_CN/latest/sdk/install_win_exe.html)

### Local Verification

* [SDK and ROS 2 verification commands](VERIFICATION.md)

## 本机编译、安装和录制

把 SDK 编译并安装到仓库内的 `_install` 目录：

```bash
cmake -S . -B _build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$PWD/_install
cmake --build _build -j2
cmake --install _build --prefix $PWD/_install
```

编译本机 sample：

```bash
cmake -S samples -B samples/_build -DCMAKE_BUILD_TYPE=Release
cmake --build samples/_build -j2
```

不依赖 ROS，直接把双目图像、深度和 IMU 录成文件：

```bash
./scripts/record_native.sh recordings/native/test
```

本机录制会把 PNG 图像和时间戳文本写到 `left/`、`right/`、`depth/`，
IMU 数据写到 `motion.txt`，双目标定参数写到 `calibration.yaml`。默认使用
`MYNTEYE_STREAM_MODE=3`，因此会启用右目。设置 `MYNTEYE_RECORD_DURATION=10` 可在
10 秒后自动停止。脚本会给输出名自动追加时间戳，例如
`recordings/native/test_20260628_153012`，并在启动时打印完整路径。

常用本机录制方式：

```bash
# 录制 10 秒双目文件，默认 stream_mode=3。
MYNTEYE_RECORD_DURATION=10 ./scripts/record_native.sh recordings/native/stereo

# 相机实际可能输出更高帧率；只把图像文件按 10Hz 落盘，IMU 仍全量保存。
MYNTEYE_RECORD_DURATION=10 MYNTEYE_SAVE_RATE=10 \
  ./scripts/record_native.sh recordings/native/stereo_10hz

# 只保存 left/right/depth 三路都有相同 frame_id 的同步帧组。
MYNTEYE_RECORD_DURATION=10 MYNTEYE_SAVE_RATE=10 MYNTEYE_SYNC_SAVE=true \
  ./scripts/record_native.sh recordings/native/synced_10hz

# 录制左目 + 深度 + IMU。
MYNTEYE_STREAM_MODE=2 MYNTEYE_RECORD_DURATION=10 \
  ./scripts/record_native.sh recordings/native/left_depth
```

开启 `MYNTEYE_SYNC_SAVE=true` 后，图像不会按各路独立落盘，而是只保存所有已启用图像流
共有的 `frame_id`。默认脚本启用 left/right/depth，所以输出的 `left/`、`right/`、
`depth/` 文件序号会一一对应；IMU 仍写入 `motion.txt`，后处理时按 timestamp 对齐。
`calibration.yaml` 保存当前 `stream_mode` 对应的 left/right 内参、畸变、P/R 矩阵和
`left_to_right` 外参；如果启用了 IMU，也会写入 `left_to_imu` 外参。

把 ROS 2 双目图像、深度和 IMU topic 录成 rosbag2 目录。
`MYNTEYE_RECORD_DURATION` 控制录制秒数：

```bash
MYNTEYE_RECORD_DURATION=10 ./scripts/record_ros2_bag.sh recordings/ros2/test
```

脚本会给 bag 目录自动追加时间戳，例如
`recordings/ros2/test_20260628_153012`，并在启动时打印完整路径。需要固定输出路径时，
加 `MYNTEYE_BAG_EXACT_PATH=true`。

ROS 2 录制脚本默认也使用 `MYNTEYE_STREAM_MODE=3`，并覆盖
`publish_right:=true` 以启用右目。如果你的固件在该模式下不能稳定发布深度，
使用 `MYNTEYE_PUBLISH_DEPTH=false`。双目 2560x720 bag 数据量很大；如果
rosbag 提示丢消息，设置 `MYNTEYE_FRAMERATE` 或录到更快的磁盘；该变量会传给
ROS 2 node 的 `framerate` 参数，并进入 SDK 的 `OpenParams.framerate`。脚本默认使用
`MYNTEYE_COLOR_ENCODING=bgr8`、`MYNTEYE_QOS_RELIABLE=true`、
`MYNTEYE_QOS_DEPTH=30`、`MYNTEYE_BAG_STORAGE_PRESET=none` 和 1GiB
rosbag 缓存，实测双目图像可接近 30Hz，且 `rqt_image_view`/`cv_bridge`
可以直接显示。需要进一步降低 CPU 和写盘压力时，可改用
`MYNTEYE_COLOR_ENCODING=yuyv`，但部分图像工具不能直接转换该格式。需要极限写入速度时，
可临时加 `MYNTEYE_BAG_STORAGE_PRESET=fastwrite`，但生成的 MCAP 没有 message index，
读取时可能出现 `no message index` warning。

常用 ROS 2 bag 录制方式：

```bash
# 双目 + IMU；如果双目 + 深度数据量太大，关闭深度。
MYNTEYE_RECORD_DURATION=10 MYNTEYE_PUBLISH_DEPTH=false \
  ./scripts/record_ros2_bag.sh recordings/ros2/stereo

# 左目 + 深度 + IMU。
MYNTEYE_STREAM_MODE=2 MYNTEYE_FRAMERATE=10 MYNTEYE_RECORD_DURATION=10 \
  ./scripts/record_ros2_bag.sh recordings/ros2/left_depth

# 查看 bag 信息。
source /opt/ros/jazzy/setup.bash
latest=$(ls -td recordings/ros2/stereo_* | head -1)
ros2 bag info "$latest"
```

如果本机有 ROS 1 环境，可以把 ROS 1 topic 录成 rosbag 文件：

```bash
ROS_SETUP=/opt/ros/noetic/setup.bash MYNTEYE_RECORD_DURATION=10 \
  ./scripts/record_ros1_bag.sh recordings/ros1/test.bag
```

ROS 1 bag 文件同样会自动追加时间戳，例如 `recordings/ros1/test_20260628_153012.bag`。

也可以在启动 ROS wrapper 后手动录制：

```bash
ros2 bag record -o recordings/ros2/manual \
  /mynteye/left/image_color /mynteye/left/image_color/camera_info \
  /mynteye/right/image_color /mynteye/right/image_color/camera_info \
  /mynteye/depth/image_raw /mynteye/depth/image_raw/camera_info \
  /mynteye/depth/image_color /mynteye/imu/data_raw

rosbag record -O recordings/ros1/manual.bag \
  /mynteye/left/image_color /mynteye/left/image_color/camera_info \
  /mynteye/right/image_color /mynteye/right/image_color/camera_info \
  /mynteye/depth/image_raw /mynteye/depth/image_raw/camera_info \
  /mynteye/imu/data_raw
```

停止脚本录制请用 `Ctrl+C`，或者等待 `MYNTEYE_RECORD_DURATION` 自动结束。不要用
`Ctrl+Z`，否则可能留下 rosbag 或相机节点进程。

如果旧 bag 是用 `MYNTEYE_COLOR_ENCODING=yuyv` 录的，部分图像工具可能报
`Unsupported image format: yuyv`。可以离线转成 `bgr8`：

```bash
source /opt/ros/jazzy/setup.bash
./scripts/convert_ros2_bag_yuyv_to_bgr8.py \
  recordings/ros2/stereo_20260628_113719 \
  recordings/ros2/stereo_20260628_113719_bgr8
```

如果只是看到 MCAP `no message index` warning，也可以用同一个脚本重写一份带索引的
bag；已是 `bgr8` 的图像会原样保留。

## RT-MonSter++ Intel NPU demos

`samples/rt_monster_npu` 提供两个面向 Intel NPU 3720 的双目 demo：Python
离线图片推理，以及 MYNT EYE 实时取流的 C++ 推理。模型输入为 `512x384`，输出视差
会恢复到相机的 `640x480` 分辨率。详细依赖、模型校验值和运行命令见
[`samples/rt_monster_npu/README.md`](samples/rt_monster_npu/README.md)。

```bash
export OPENVINO_PYTHON=/path/to/openvino-venv/bin/python

# 真实 NPU 离线回归测试 + C++ demo 构建测试
./samples/rt_monster_npu/scripts/test.sh

# MYNT EYE 双目实时 NPU 推理
./samples/rt_monster_npu/scripts/run_live.sh
```

## Mirrors

国内镜像：[码云](https://gitee.com/mynt/MYNT-EYE-D-SDK)。

## License

This project is licensed under the [Apache License, Version 2.0](/LICENSE). Copyright 2018 Slightech Co., Ltd.
