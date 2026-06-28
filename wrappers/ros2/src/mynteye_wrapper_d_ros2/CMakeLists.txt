cmake_minimum_required(VERSION 3.16)
project(mynteye_wrapper_d_ros2)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(cv_bridge REQUIRED)
find_package(OpenCV REQUIRED)

get_filename_component(MYNTEYE_SDK_ROOT
  "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)

list(APPEND CMAKE_PREFIX_PATH "${MYNTEYE_SDK_ROOT}/_install/lib/cmake")
find_package(mynteyed REQUIRED)

add_executable(mynteye_ros2_node src/mynteye_ros2_node.cc)
target_include_directories(mynteye_ros2_node PRIVATE
  "${MYNTEYE_SDK_ROOT}/_install/include")
target_link_libraries(mynteye_ros2_node
  mynteye_depth
  ${OpenCV_LIBS})
set_target_properties(mynteye_ros2_node PROPERTIES
  BUILD_RPATH "${MYNTEYE_SDK_ROOT}/_install/lib;${MYNTEYE_SDK_ROOT}/_install/lib/3rdparty"
  INSTALL_RPATH "${MYNTEYE_SDK_ROOT}/_install/lib;${MYNTEYE_SDK_ROOT}/_install/lib/3rdparty")
ament_target_dependencies(mynteye_ros2_node
  rclcpp sensor_msgs std_msgs cv_bridge)

install(TARGETS mynteye_ros2_node
  DESTINATION lib/${PROJECT_NAME})

install(DIRECTORY launch config
  DESTINATION share/${PROJECT_NAME})

ament_package()
