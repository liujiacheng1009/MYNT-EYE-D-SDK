.. _record_data:

录制数据集
==========

SDK 提供 ``record`` 工具，可以不依赖 ROS，直接把相机图像、深度和 IMU 数据录成文件。

Linux 参考命令：

.. code-block:: bash

   ./samples/_output/bin/dataset/record --out ./dataset --duration 10 --no-display

仓库也提供了封装脚本：

.. code-block:: bash

   ./scripts/record_native.sh recordings/native/test

脚本会给输出目录自动追加时间戳，例如
``recordings/native/test_20260628_153012``，并在启动时打印完整路径。

Windows 参考命令：

.. code-block:: bash

   .\samples\_output\bin\dataset\record.bat

Linux 参考输出：

.. code-block:: bash

   $ ./samples/_output/bin/dataset/record --out ./dataset --duration 10 --no-display
   Saved 300 left, 300 right, 300 depth, 2000 accels, 2000 gyros to ./dataset
   Time beg: 2026-06-28 10:00:00.000000, end: 2026-06-28 10:00:10.000000, cost: 10000ms

默认保存到 ``<workdir>/dataset``。可以使用 ``--out DIR`` 或第一个位置参数指定其它目录。

录制内容：

::

   <workdir>/
   └─dataset/
      ├─left/
      │  ├─stream.txt  # 图像时间戳信息
      │  └─...
      ├─right/
      │  ├─stream.txt  # 图像时间戳信息
      │  └─...
      ├─depth/
      │  ├─stream.txt  # 深度帧时间戳信息
      │  └─...         # 16-bit raw depth PNG，单位 mm
      ├─calibration.yaml  # 双目内参、畸变、P/R 矩阵和 left_to_right 外参
      └─motion.txt  # IMU 信息

常用选项：

.. code-block:: bash

   ./samples/_output/bin/dataset/record --help
   ./samples/_output/bin/dataset/record --out ./dataset --duration 30 --no-display
   ./samples/_output/bin/dataset/record --out ./dataset --duration 10 --save-rate 10 --no-display
   ./samples/_output/bin/dataset/record --out ./dataset --duration 10 --save-rate 10 --sync-save --no-display
   ./samples/_output/bin/dataset/record --out ./dataset --stream-mode 2 --left --depth --imu

``--stream-mode 3 --right`` 录制 2560x720 左右目双目流。
如果更需要默认的 1280x720 左目 + 深度模式，使用 ``--stream-mode 2``。
``--save-rate 10`` 只限制图像落盘频率；SDK 实际收到的图像帧数会在结束统计里的
``received`` 字段显示，IMU 默认仍全量保存。
``--sync-save`` 只保存所有已启用图像流共有的 ``frame_id``。如果同时启用了
left/right/depth，输出的 ``left/``、``right/``、``depth/`` 文件序号会一一对应；
IMU 仍写入 ``motion.txt``，后处理时按 timestamp 对齐。
每次录制都会保存 ``calibration.yaml``，其中包含当前 ``stream_mode`` 对应的
left/right 内参、畸变、P/R 矩阵和 ``left_to_right`` 外参；如果启用了 IMU，也会写入
``left_to_imu`` 外参。

脚本形式：

.. code-block:: bash

   MYNTEYE_RECORD_DURATION=10 MYNTEYE_SAVE_RATE=10 MYNTEYE_SYNC_SAVE=true \
     ./scripts/record_native.sh recordings/native/synced_10hz

ROS bag 录制
------------

仓库也提供了录制 ROS wrapper 的脚本。

ROS 2 bag：

.. code-block:: bash

   cd MYNT-EYE-D-SDK
   MYNTEYE_RECORD_DURATION=10 ./scripts/record_ros2_bag.sh recordings/ros2/test

脚本会给 bag 目录自动追加时间戳，例如
``recordings/ros2/test_20260628_153012``。需要固定输出目录时，加
``MYNTEYE_BAG_EXACT_PATH=true``。

ROS 2 只录双目和 IMU：

.. code-block:: bash

   MYNTEYE_RECORD_DURATION=10 MYNTEYE_PUBLISH_DEPTH=false \
     ./scripts/record_ros2_bag.sh recordings/ros2/stereo

设置 ``MYNTEYE_FRAMERATE`` 可以控制 ROS 2 node 打开 SDK 时使用的
``OpenParams.framerate``，例如左目 + 深度 + IMU 录 10Hz：

.. code-block:: bash

   MYNTEYE_STREAM_MODE=2 MYNTEYE_FRAMERATE=10 MYNTEYE_RECORD_DURATION=10 \
     ./scripts/record_ros2_bag.sh recordings/ros2/left_depth

ROS 2 录制脚本默认使用 ``MYNTEYE_COLOR_ENCODING=bgr8``、
``MYNTEYE_QOS_RELIABLE=true``、``MYNTEYE_QOS_DEPTH=30``、
``MYNTEYE_BAG_STORAGE_PRESET=none`` 和 1GiB rosbag 缓存，以减少双目大图像
录制丢包，并保持 ``rqt_image_view``/``cv_bridge`` 可直接显示。需要进一步降低 CPU
和写盘压力时，可改用 ``MYNTEYE_COLOR_ENCODING=yuyv``，但部分图像工具不能直接转换
该格式。需要极限写入速度时，可临时加 ``MYNTEYE_BAG_STORAGE_PRESET=fastwrite``，但生成的
MCAP 没有 message index，读取时可能出现 ``no message index`` warning。

ROS 1 bag：

.. code-block:: bash

   ROS_SETUP=/opt/ros/noetic/setup.bash MYNTEYE_RECORD_DURATION=10 \
     ./scripts/record_ros1_bag.sh recordings/ros1/test.bag

ROS 1 bag 文件同样会自动追加时间戳，例如
``recordings/ros1/test_20260628_153012.bag``。

如果旧 ROS 2 bag 是用 ``MYNTEYE_COLOR_ENCODING=yuyv`` 录的，部分图像工具可能报
``Unsupported image format: yuyv``。可以离线转成 ``bgr8``：

.. code-block:: bash

   source /opt/ros/jazzy/setup.bash
   ./scripts/convert_ros2_bag_yuyv_to_bgr8.py \
     recordings/ros2/stereo_20260628_113719 \
     recordings/ros2/stereo_20260628_113719_bgr8

如果只是看到 MCAP ``no message index`` warning，也可以用同一个脚本重写一份带索引的
bag；已是 ``bgr8`` 的图像会原样保留。

启动节点后，手动录制 ROS 2 bag：

.. code-block:: bash

   ros2 bag record -o recordings/ros2/manual \
     /mynteye/left/image_color /mynteye/left/image_color/camera_info \
     /mynteye/right/image_color /mynteye/right/image_color/camera_info \
     /mynteye/depth/image_raw /mynteye/depth/image_raw/camera_info \
     /mynteye/depth/image_color /mynteye/imu/data_raw

启动节点后，手动录制 ROS 1 bag：

.. code-block:: bash

   rosbag record -O recordings/ros1/manual.bag \
     /mynteye/left/image_color /mynteye/left/image_color/camera_info \
     /mynteye/right/image_color /mynteye/right/image_color/camera_info \
     /mynteye/depth/image_raw /mynteye/depth/image_raw/camera_info \
     /mynteye/imu/data_raw

.. tip::

  录制时 ``cv::imwrite()`` 会写入图像文件，这是比较耗时的操作。如果磁盘或 CPU
  跟不上相机输出速度，可能会丢帧。``record.cc`` 使用的 ``GetStreamDatas()``
  只缓存最近 4 帧图像。
