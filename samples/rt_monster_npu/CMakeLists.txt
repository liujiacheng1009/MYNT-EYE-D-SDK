cmake_minimum_required(VERSION 3.16)
project(mynteye_rt_monster_demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(mynteyed REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs highgui viz)
find_package(OpenVINO REQUIRED COMPONENTS Runtime)

# Some existing MYNT EYE installations were exported with every module named
# by Ubuntu's OpenCVConfig.cmake. Ubuntu 26.04 advertises opencv_barcode without
# shipping that optional library, although neither the SDK nor this demo uses
# it. Remove only that unavailable transitive dependency from the imported SDK
# target so the demo also works with an installation created before the SDK's
# OpenCV filtering fix.
find_library(OPENCV_BARCODE_LIBRARY NAMES opencv_barcode)
if(NOT OPENCV_BARCODE_LIBRARY)
  get_target_property(MYNTEYE_INTERFACE_LIBRARIES
    mynteye_depth INTERFACE_LINK_LIBRARIES)
  if(MYNTEYE_INTERFACE_LIBRARIES)
    list(REMOVE_ITEM MYNTEYE_INTERFACE_LIBRARIES opencv_barcode)
    set_target_properties(mynteye_depth PROPERTIES
      INTERFACE_LINK_LIBRARIES "${MYNTEYE_INTERFACE_LIBRARIES}")
  endif()
endif()

if(mynteyed_WITH_OPENCV)
  add_compile_definitions(WITH_OPENCV)
endif()

add_executable(mynteye_rt_monster src/mynteye_rt_monster.cc)
target_link_libraries(mynteye_rt_monster PRIVATE
  mynteye_depth
  openvino::runtime
  ${OpenCV_LIBS}
)
target_compile_options(mynteye_rt_monster PRIVATE -Wall -Wextra -O3)
