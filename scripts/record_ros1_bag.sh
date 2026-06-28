#!/usr/bin/env bash
# Launch the ROS 1 wrapper and record camera/depth/IMU topics into a rosbag.

set -e

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ros_setup="${ROS_SETUP:-/opt/ros/noetic/setup.bash}"
timestamp="${MYNTEYE_RECORD_STAMP:-$(date +%Y%m%d_%H%M%S)}"
bag_file_arg="${1:-${MYNTEYE_BAG_FILE:-recordings/ros1/}}"
duration="${MYNTEYE_RECORD_DURATION:-0}"

stamp_path() {
  local path="$1"

  if [ "${MYNTEYE_BAG_EXACT_PATH:-false}" = "true" ]; then
    printf '%s\n' "$path"
    return
  fi

  case "$path" in
    */) printf '%s%s.bag\n' "$path" "$timestamp" ;;
    *_????????_??????.bag) printf '%s\n' "$path" ;;
    *.bag) printf '%s_%s.bag\n' "${path%.bag}" "$timestamp" ;;
    *) printf '%s_%s.bag\n' "$path" "$timestamp" ;;
  esac
}

bag_file_arg="$(stamp_path "$bag_file_arg")"
case "$bag_file_arg" in
  /*) bag_file="$bag_file_arg" ;;
  *) bag_file="$repo_dir/$bag_file_arg" ;;
esac

echo "Recording ROS 1 bag to: $bag_file"
source "$ros_setup"
cd "$repo_dir/wrappers/ros"

if [ ! -f devel/setup.bash ]; then
  catkin_make
fi
source devel/setup.bash

mkdir -p "$(dirname "$bag_file")"

roslaunch mynteye_wrapper_d mynteye.launch &
launch_pid=$!
cleanup() {
  kill "$launch_pid" >/dev/null 2>&1 || true
  wait "$launch_pid" >/dev/null 2>&1 || true
}
trap cleanup EXIT INT TERM

sleep "${MYNTEYE_LAUNCH_WAIT:-3}"

topics=(
  /mynteye/left/image_color
  /mynteye/left/image_color/camera_info
  /mynteye/right/image_color
  /mynteye/right/image_color/camera_info
  /mynteye/depth/image_raw
  /mynteye/depth/image_raw/camera_info
  /mynteye/imu/data_raw
)

if [ "$duration" -gt 0 ]; then
  set +e
  timeout --signal=INT "$duration" rosbag record -O "$bag_file" "${topics[@]}"
  status=$?
  set -e
  if [ "$status" -ne 0 ] && [ "$status" -ne 124 ]; then
    exit "$status"
  fi
else
  rosbag record -O "$bag_file" "${topics[@]}"
fi
