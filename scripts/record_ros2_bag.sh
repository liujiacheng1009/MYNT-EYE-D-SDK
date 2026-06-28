#!/usr/bin/env bash
# Launch the ROS 2 wrapper and record camera/depth/IMU topics into a rosbag2.

set -e

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ros_setup="${ROS_SETUP:-/opt/ros/jazzy/setup.bash}"
timestamp="${MYNTEYE_RECORD_STAMP:-$(date +%Y%m%d_%H%M%S)}"
bag_dir_arg="${1:-${MYNTEYE_BAG_DIR:-recordings/ros2/}}"
duration="${MYNTEYE_RECORD_DURATION:-0}"
framerate="${MYNTEYE_FRAMERATE:-30}"
stream_mode="${MYNTEYE_STREAM_MODE:-3}"
publish_depth="${MYNTEYE_PUBLISH_DEPTH:-true}"
color_encoding="${MYNTEYE_COLOR_ENCODING:-bgr8}"
qos_depth="${MYNTEYE_QOS_DEPTH:-30}"
qos_reliable="${MYNTEYE_QOS_RELIABLE:-true}"
bag_cache_size="${MYNTEYE_BAG_CACHE_SIZE:-1073741824}"
storage_preset="${MYNTEYE_BAG_STORAGE_PRESET:-none}"
topic_wait="${MYNTEYE_TOPIC_WAIT:-20}"
bag_stop_wait="${MYNTEYE_BAG_STOP_WAIT:-2}"
bag_pid=""

stamp_path() {
  local path="$1"

  if [ "${MYNTEYE_BAG_EXACT_PATH:-false}" = "true" ]; then
    printf '%s\n' "$path"
    return
  fi

  case "$path" in
    */) printf '%s%s\n' "$path" "$timestamp" ;;
    *_????????_??????) printf '%s\n' "$path" ;;
    *) printf '%s_%s\n' "$path" "$timestamp" ;;
  esac
}

bag_dir_arg="$(stamp_path "$bag_dir_arg")"
case "$bag_dir_arg" in
  /*) bag_dir="$bag_dir_arg" ;;
  *) bag_dir="$repo_dir/$bag_dir_arg" ;;
esac

echo "Recording ROS 2 bag to: $bag_dir"
source "$ros_setup"
cd "$repo_dir/wrappers/ros2"

mkdir -p "$(dirname "$bag_dir")"

if [ ! -f install/setup.bash ]; then
  colcon build --symlink-install
fi
source install/setup.bash

node_args=(
  --ros-args
  --params-file "$repo_dir/wrappers/ros2/src/mynteye_wrapper_d_ros2/config/mynteye.yaml"
  -p framerate:="$framerate"
  -p stream_mode:="$stream_mode"
  -p color_encoding:="$color_encoding"
  -p qos_depth:="$qos_depth"
  -p qos_reliable:="$qos_reliable"
  -p publish_right:=true
  -p publish_depth:="$publish_depth"
  -p publish_depth_color:="$publish_depth"
)

setsid ros2 run mynteye_wrapper_d_ros2 mynteye_ros2_node "${node_args[@]}" &
launch_pid=$!
terminate_tree() {
  local sig="$1"
  local pid="$2"

  kill "-$sig" "-$pid" >/dev/null 2>&1 || true
  pkill "-$sig" -P "$pid" >/dev/null 2>&1 || true
  kill "-$sig" "$pid" >/dev/null 2>&1 || true
}

wait_for_exit() {
  local pid="$1"
  local seconds="${2:-5}"
  for _ in $(seq 1 "$seconds"); do
    kill -0 "$pid" >/dev/null 2>&1 || return 0
    sleep 1
  done
  return 1
}

stop_bag() {
  if [ -z "${bag_pid:-}" ] || ! kill -0 "$bag_pid" >/dev/null 2>&1; then
    return 0
  fi

  terminate_tree INT "$bag_pid"
  if ! wait_for_exit "$bag_pid" "$bag_stop_wait"; then
    echo "Warning: rosbag2 did not stop after SIGINT; sending SIGTERM." >&2
    terminate_tree TERM "$bag_pid"
    sleep 2
  fi
  if kill -0 "$bag_pid" >/dev/null 2>&1; then
    echo "Warning: rosbag2 still running; sending SIGKILL." >&2
    terminate_tree KILL "$bag_pid"
  fi
  wait "$bag_pid" >/dev/null 2>&1 || true
}

cleanup() {
  trap - EXIT INT TERM

  stop_bag

  if kill -0 "$launch_pid" >/dev/null 2>&1; then
    terminate_tree INT "$launch_pid"
    wait_for_exit "$launch_pid" || true
  fi
  if kill -0 "$launch_pid" >/dev/null 2>&1; then
    terminate_tree TERM "$launch_pid"
    sleep 1
  fi
  if kill -0 "$launch_pid" >/dev/null 2>&1; then
    terminate_tree KILL "$launch_pid"
  fi
  wait "$launch_pid" >/dev/null 2>&1 || true
}
stop_and_exit() {
  cleanup
  exit 130
}
trap cleanup EXIT
trap stop_and_exit INT TERM

topics=(
  /mynteye/left/image_color
  /mynteye/left/image_color/camera_info
  /mynteye/right/image_color
  /mynteye/right/image_color/camera_info
  /mynteye/imu/data_raw
)
if [ "$publish_depth" = "true" ]; then
  topics+=(
    /mynteye/depth/image_raw
    /mynteye/depth/image_raw/camera_info
    /mynteye/depth/image_color
  )
fi

echo "Waiting up to ${topic_wait}s for ${topics[0]} ..."
for ((i = 0; i < topic_wait; ++i)); do
  if ! kill -0 "$launch_pid" >/dev/null 2>&1; then
    echo "Error: MYNT EYE ROS 2 node exited before publishing topics." >&2
    exit 1
  fi
  if ros2 topic list | grep -qx "${topics[0]}"; then
    break
  fi
  sleep 1
done

if ! ros2 topic list | grep -qx "${topics[0]}"; then
  echo "Error: ${topics[0]} did not appear before timeout." >&2
  exit 1
fi
if ! kill -0 "$launch_pid" >/dev/null 2>&1; then
  echo "Error: MYNT EYE ROS 2 node exited before recording started." >&2
  exit 1
fi

sleep "${MYNTEYE_RECORD_SETTLE:-1}"

record_cmd=(
  ros2 bag record
  -o "$bag_dir"
  --storage-preset-profile "$storage_preset"
  --max-cache-size "$bag_cache_size"
  --disable-keyboard-controls
  --topics "${topics[@]}"
)

if [ "$duration" -gt 0 ]; then
  set +e
  setsid "${record_cmd[@]}" &
  bag_pid=$!
  for _ in $(seq 1 "$duration"); do
    sleep 1
    kill -0 "$bag_pid" >/dev/null 2>&1 || break
  done
  stop_bag
  status=0
  set -e
  if [ "$status" -ne 0 ] && [ "$status" -ne 130 ]; then
    exit "$status"
  fi
else
  set +e
  setsid "${record_cmd[@]}" &
  bag_pid=$!
  wait "$bag_pid"
  status=$?
  set -e
  if [ "$status" -ne 0 ]; then
    exit "$status"
  fi
fi

if [ ! -f "$bag_dir/metadata.yaml" ]; then
  echo "Error: rosbag2 did not create $bag_dir/metadata.yaml" >&2
  echo "Check that the requested topics are publishing messages." >&2
  exit 1
fi
