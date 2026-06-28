#!/usr/bin/env bash
# Install Ubuntu 24.x development dependencies for this legacy SDK.

set -e

if ! command -v lsb_release >/dev/null 2>&1; then
  echo "lsb_release is required to detect Ubuntu release."
  exit 1
fi

dist_id=$(lsb_release -is)
codename=$(lsb_release -cs)

if [ "$dist_id" != "Ubuntu" ]; then
  echo "This helper is intended for Ubuntu 24.x."
  exit 1
fi

sudo apt-get update
sudo apt-get install -y \
  build-essential \
  cmake \
  curl \
  git \
  gnupg \
  libgtk-3-dev \
  libjpeg-dev \
  libopencv-dev \
  libusb-dev \
  libv4l-dev \
  make \
  pkg-config \
  python3-opencv \
  software-properties-common

case "$codename" in
  noble)
    sudo add-apt-repository universe -y
    sudo apt-get update
    sudo apt-get install -y curl gnupg lsb-release
    sudo curl -sSL \
      https://raw.githubusercontent.com/ros/rosdistro/master/ros.key \
      -o /usr/share/keyrings/ros-archive-keyring.gpg
    echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/ros-archive-keyring.gpg] http://packages.ros.org/ros2/ubuntu noble main" | \
      sudo tee /etc/apt/sources.list.d/ros2.list >/dev/null
    sudo apt-get update
    sudo apt-get install -y ros-jazzy-desktop ros-dev-tools
    ;;
  oracular)
    echo "Ubuntu 24.10 (oracular) detected."
    echo "OpenCV dependencies were installed from Ubuntu packages."
    echo "ROS 2 Jazzy targets Ubuntu 24.04 (noble); this script does not add noble ROS repositories to oracular."
    echo "Use Ubuntu 24.04 for ROS 2 Jazzy, or build ROS 2 from source for this release."
    ;;
  *)
    echo "Ubuntu codename '$codename' is not handled by this helper."
    echo "OpenCV dependencies were installed; install the matching ROS 2 distribution manually if needed."
    ;;
esac
