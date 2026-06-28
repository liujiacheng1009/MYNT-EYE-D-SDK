from launch import LaunchDescription
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory
from pathlib import Path


def generate_launch_description():
    pkg_share = Path(get_package_share_directory("mynteye_wrapper_d_ros2"))
    params = pkg_share / "config" / "mynteye.yaml"

    return LaunchDescription([
        Node(
            package="mynteye_wrapper_d_ros2",
            executable="mynteye_ros2_node",
            name="mynteye_ros2_node",
            output="screen",
            parameters=[str(params)],
        )
    ])
