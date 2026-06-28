# MYNT EYE D ROS 2 Wrapper

This is a minimal ROS 2 wrapper for MYNT EYE D cameras. It publishes:

- `mynteye/left/image_color`
- `mynteye/right/image_color`
- `mynteye/depth/image_raw`
- `mynteye/imu/data_raw`
- matching `*/camera_info` topics for image streams

Point cloud publishing is intentionally not included yet. The current Ubuntu
24.10 PCL/VTK stack has extra CMake dependency issues, and image/depth/IMU are
the first validation targets.

## Recommended System

Use Ubuntu 24.04 with ROS 2 Jazzy. Do not add Ubuntu 24.04 ROS apt sources to an
Ubuntu 24.10 system.

## Build

First build and install the SDK into the local `_install` directory:

```bash
cd /home/jc/Downloads/MYNT-EYE-D-SDK
cmake -S . -B _build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$PWD/_install
cmake --build _build -j2
cmake --install _build --prefix $PWD/_install
```

Then build the ROS 2 workspace:

```bash
source /opt/ros/jazzy/setup.bash
cd /home/jc/Downloads/MYNT-EYE-D-SDK/wrappers/ros2
rosdep install --from-paths src --ignore-src -r -y
colcon build --symlink-install
source install/setup.bash
```

## Run

```bash
ros2 launch mynteye_wrapper_d_ros2 mynteye.launch.py
```

Check topics:

```bash
ros2 topic list | grep mynteye
ros2 topic hz /mynteye/left/image_color
ros2 topic hz /mynteye/depth/image_raw
ros2 topic hz /mynteye/imu/data_raw
```
