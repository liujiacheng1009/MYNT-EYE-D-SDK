# MYNT EYE D ROS 2 Wrapper

这是 MYNT EYE D 相机的最小 ROS 2 wrapper。它发布：

- `mynteye/left/image_color`
- `mynteye/right/image_color`
- `mynteye/depth/image_raw`
- `mynteye/imu/data_raw`
- 图像流对应的 `*/camera_info` topic

暂时没有包含点云发布。当前 Ubuntu 24.10 的 PCL/VTK 依赖链还有额外 CMake
问题，因此这里先以图像、深度和 IMU 验证为主。

## 推荐系统

推荐 Ubuntu 24.04 + ROS 2 Jazzy。不要把 Ubuntu 24.04 的 ROS apt 源直接加到
Ubuntu 24.10 系统里。

## 编译

先把 SDK 编译并安装到仓库内的 `_install` 目录：

```bash
cd MYNT-EYE-D-SDK
cmake -S . -B _build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$PWD/_install
cmake --build _build -j2
cmake --install _build --prefix $PWD/_install
```

然后编译 ROS 2 workspace：

```bash
source /opt/ros/jazzy/setup.bash
cd wrappers/ros2
colcon build --symlink-install
source install/setup.bash
```

如果 `rosdep` 已经初始化并更新过，可以按需安装 ROS 依赖：

```bash
cd wrappers/ros2
rosdep install --from-paths src --ignore-src -r -y
```

## 运行

```bash
ros2 launch mynteye_wrapper_d_ros2 mynteye.launch.py
```

检查 topic：

```bash
ros2 topic list | grep mynteye
ros2 topic hz /mynteye/left/image_color
ros2 topic hz /mynteye/depth/image_raw
ros2 topic hz /mynteye/imu/data_raw
```

## 录制

### 脚本录制 ROS 2 Bag

把双目图像、深度和 IMU topic 录成 rosbag2 目录。
`MYNTEYE_RECORD_DURATION` 控制录制秒数。相对输出路径会按仓库根目录解析：

```bash
cd /path/to/MYNT-EYE-D-SDK
MYNTEYE_RECORD_DURATION=10 ./scripts/record_ros2_bag.sh recordings/ros2/test
```

脚本会给输出目录自动追加时间戳，例如
`recordings/ros2/test_20260628_153012`，并在启动时打印完整路径。需要固定输出目录时，
加 `MYNTEYE_BAG_EXACT_PATH=true`。

如果你已经在 `MYNT-EYE-D-SDK/wrappers/ros2` 目录下，先回到仓库根目录再运行：

```bash
cd ../..
MYNTEYE_RECORD_DURATION=10 ./scripts/record_ros2_bag.sh recordings/ros2/test
```

录制脚本会用 `stream_mode:=3` 和 `publish_right:=true` 启用右目。如果设备或固件
在双目模式下不能稳定发布深度，加上 `MYNTEYE_PUBLISH_DEPTH=false`。双目 2560x720
bag 数据量很大；如果 rosbag 提示丢消息，设置 `MYNTEYE_FRAMERATE` 或录到更快的磁盘；
该变量会传给 ROS 2 node 的 `framerate` 参数，并进入 SDK 的 `OpenParams.framerate`。
脚本默认使用
`MYNTEYE_COLOR_ENCODING=bgr8`、`MYNTEYE_QOS_RELIABLE=true`、
`MYNTEYE_QOS_DEPTH=30`、`MYNTEYE_BAG_STORAGE_PRESET=none` 和 1GiB
rosbag 缓存，实测双目图像可接近 30Hz，且 `rqt_image_view`/`cv_bridge`
可以直接显示。需要进一步降低 CPU 和写盘压力时，可改用
`MYNTEYE_COLOR_ENCODING=yuyv`，但部分图像工具不能直接转换该格式。需要极限写入速度时，
可临时加 `MYNTEYE_BAG_STORAGE_PRESET=fastwrite`，但生成的 MCAP 没有 message index，
读取时可能出现 `no message index` warning。

常用方式：

```bash
# 双目 + IMU，不录深度。
MYNTEYE_RECORD_DURATION=10 MYNTEYE_PUBLISH_DEPTH=false \
  ./scripts/record_ros2_bag.sh recordings/ros2/stereo

# 左目 + 深度 + IMU。
MYNTEYE_STREAM_MODE=2 MYNTEYE_FRAMERATE=10 MYNTEYE_RECORD_DURATION=10 \
  ./scripts/record_ros2_bag.sh recordings/ros2/left_depth

# 查看输出。
latest=$(ls -td recordings/ros2/stereo_* | head -1)
ros2 bag info "$latest"
```

停止录制请用 `Ctrl+C`，或者等待 `MYNTEYE_RECORD_DURATION` 自动结束。不要用
`Ctrl+Z`，它只会挂起 shell job，可能留下子进程继续运行。脚本收到停止信号后会
先让 rosbag2 写完缓存；如果短时间内没有退出，会继续发送终止信号并保留已写出的
bag metadata。

如果之前误用了 `Ctrl+Z`，可以清理残留进程：

```bash
pkill -f 'ros2 bag record'
pkill -f 'mynteye_ros2_node'
```

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

### 手动录制 ROS 2 Bag

启动节点后，也可以手动录制：

```bash
ros2 bag record -o recordings/ros2/test \
  /mynteye/left/image_color \
  /mynteye/left/image_color/camera_info \
  /mynteye/right/image_color \
  /mynteye/right/image_color/camera_info \
  /mynteye/depth/image_raw \
  /mynteye/depth/image_raw/camera_info \
  /mynteye/depth/image_color \
  /mynteye/imu/data_raw
```

只录双目和 IMU：

```bash
ros2 bag record -o recordings/ros2/stereo \
  /mynteye/left/image_color \
  /mynteye/left/image_color/camera_info \
  /mynteye/right/image_color \
  /mynteye/right/image_color/camera_info \
  /mynteye/imu/data_raw
```
