# MYNT EYE D SDK 验证指令

本文档用于在当前环境验证 MYNT EYE D SDK 的图像、深度和 IMU 数据。

仓库路径假设为：

```bash
cd /home/jc/Downloads/MYNT-EYE-D-SDK
```

## 1. 准备环境

插入 MYNT EYE D 相机后，先确认 USB 设备可见：

```bash
lsusb | grep -i -E 'mynt|slightech|e-con|camera'
```

如果之前没有安装 udev 规则，可以执行：

```bash
sudo cp scripts/config/mynteye-d1000.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
```

然后拔插一次相机。若当前系统是 Ubuntu 24.x，可先安装依赖：

```bash
make deps-u24
```

## 2. 构建 SDK 和示例程序

推荐安装到仓库内的 `_install`，避免污染 `/usr/local`：

```bash
cmake -S . -B _build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$PWD/_install
cmake --build _build -j2
cmake --install _build --prefix $PWD/_install

cmake -S samples -B samples/_build -DCMAKE_BUILD_TYPE=Release
cmake --build samples/_build -j2
```

示例程序会生成到：

```bash
ls samples/_output/bin
```

## 3. 不依赖 ROS 验证

以下命令直接使用 SDK sample，不需要启动 ROS 或 ROS 2。

### 3.1 设备枚举

```bash
./samples/_output/bin/get_device_info
```

预期现象：

- 能看到设备信息列表。
- 只有一台相机时会自动选择 `index: 0`。
- 如果提示 `Device not found`，优先检查 USB 连接、权限规则和相机供电。

### 3.2 图像验证

```bash
./samples/_output/bin/get_image
```

预期现象：

- 弹出 `left color` 窗口。
- 如果当前模式启用了深度，也会弹出 `depth` 窗口。
- 窗口中能看到实时画面和帧率信息。
- 按 `q`、`Q` 或 `Esc` 退出。

### 3.3 深度验证

```bash
./samples/_output/bin/get_depth
```

预期现象：

- 弹出 `color`、`depth`、`region` 窗口。
- 鼠标移动到深度图上可以查看局部深度值。
- 深度单位为 mm。
- 按 `q`、`Q` 或 `Esc` 退出。

如果需要验证深度滤波链路：

```bash
./samples/_output/bin/get_depth_with_filter
```

### 3.4 IMU 验证

```bash
./samples/_output/bin/get_imu
```

预期现象：

- 终端持续输出 `Imu count`。
- 能看到 `[accel]` 加速度数据。
- 能看到 `[gyro]` 陀螺仪数据。
- 按任意键退出。

如果输出 `IMU is not supported on your device`，说明当前设备型号或固件不支持 IMU。

## 4. 依赖 ROS 2 验证

ROS 2 wrapper 位于 `wrappers/ros2`。推荐 Ubuntu 24.04 + ROS 2 Jazzy。

### 4.1 构建 ROS 2 wrapper

先确认 SDK 已安装到本地 `_install`：

```bash
test -f _install/lib/cmake/mynteyed/mynteyed-config.cmake
```

构建 ROS 2 workspace：

```bash
source /opt/ros/jazzy/setup.bash
cd /home/jc/Downloads/MYNT-EYE-D-SDK/wrappers/ros2
rosdep install --from-paths src --ignore-src -r -y
colcon build --symlink-install
source install/setup.bash
```

### 4.2 启动节点

```bash
source /opt/ros/jazzy/setup.bash
cd /home/jc/Downloads/MYNT-EYE-D-SDK/wrappers/ros2
source install/setup.bash
ros2 launch mynteye_wrapper_d_ros2 mynteye.launch.py
```

保持这个终端运行，另开一个终端执行后续检查。

### 4.3 检查 topic

```bash
source /opt/ros/jazzy/setup.bash
cd /home/jc/Downloads/MYNT-EYE-D-SDK/wrappers/ros2
source install/setup.bash

ros2 topic list | grep mynteye
```

预期至少看到：

```text
/mynteye/left/image_color
/mynteye/depth/image_raw
/mynteye/depth/image_color
/mynteye/imu/data_raw
```

### 4.4 验证图像和深度频率

```bash
ros2 topic hz /mynteye/left/image_color
ros2 topic hz /mynteye/depth/image_raw
ros2 topic hz /mynteye/depth/image_color
```

预期现象：

- 每个命令都能持续输出平均频率。
- 默认配置通常接近相机配置帧率，实际值会受 USB、曝光、CPU 负载影响。

### 4.5 验证 IMU 数据

```bash
ros2 topic hz /mynteye/imu/data_raw
ros2 topic echo --once /mynteye/imu/data_raw
```

预期现象：

- `topic hz` 能看到持续频率。
- `topic echo` 能看到 `linear_acceleration` 和 `angular_velocity` 字段。
- `orientation_covariance[0]` 为 `-1.0`，表示该 wrapper 不发布姿态解算结果。

### 4.6 使用 rqt 或 RViz2 查看

如果提示 `rqt_image_view: command not found`，先安装 rqt 图像插件：

```bash
sudo apt-get update
sudo apt-get install -y ros-jazzy-rqt-image-view
source /opt/ros/jazzy/setup.bash
```

查看图像：

```bash
ros2 run rqt_image_view rqt_image_view /mynteye/left/image_color
ros2 run rqt_image_view rqt_image_view /mynteye/depth/image_color
```

或使用 RViz2：

```bash
rviz2
```

在 RViz2 中添加 `Image` 显示项，并选择：

- `/mynteye/left/image_color`
- `/mynteye/depth/image_color`

默认配置不会发布右目图像：`wrappers/ros2/src/mynteye_wrapper_d_ros2/config/mynteye.yaml`
中 `publish_right: false`，并且 `stream_mode: 2` 是 `STREAM_1280x720`
左图 + 深度模式。因此默认不要选择 `/mynteye/right/image_color`，否则 RViz2
可能显示 `Status: Ok` 但画面仍是 `No Image`。

如果 RViz2 输出类似下面的 warning：

```text
offering incompatible QoS. No messages will be sent to it.
Last incompatible policy: RELIABILITY_QOS_POLICY
```

需要在对应的 `Image` 显示项里展开 `Topic` 或 `QoS` 设置，将
`Reliability Policy` 从 `Reliable` 改成 `Best Effort`。该 ROS 2 wrapper
使用 `SensorDataQoS` 发布图像和 IMU，RViz2 默认 QoS 可能无法直接匹配。

如果 QoS 已经是 `Best Effort` 但仍然没有图像，先确认该 topic 是否真的有帧：

```bash
ros2 topic hz /mynteye/left/image_color
ros2 topic hz /mynteye/depth/image_color
ros2 topic hz /mynteye/right/image_color
```

只有能持续输出频率的 topic 才能在 RViz2 或 rqt 中显示图像。默认配置下右目
topic 不应有频率输出。

## 5. 常见问题

### 找不到设备

```bash
lsusb
groups
```

确认相机已连接，udev 规则已加载。必要时重新拔插相机，或临时用 `sudo` 运行 sample 做权限排查。

### OpenCV 窗口没有弹出

确认是在桌面会话或可用 X11/Wayland 显示环境中执行：

```bash
echo $DISPLAY
```

远程 SSH 时需要启用 X11 转发，或改用 ROS 2 topic 命令验证数据流。

### ROS 2 没有 topic

确认 launch 终端没有报错，并确认已 source：

```bash
source /opt/ros/jazzy/setup.bash
source /home/jc/Downloads/MYNT-EYE-D-SDK/wrappers/ros2/install/setup.bash
```

如果节点启动时报 `No MYNT EYE device found`，先回到不依赖 ROS 的 `get_device_info` 排查设备连接和权限。
