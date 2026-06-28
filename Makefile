# Copyright 2018 Slightech Co., Ltd. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
MKFILE_PATH := $(abspath $(lastword $(MAKEFILE_LIST)))
MKFILE_DIR := $(patsubst %/,%,$(dir $(MKFILE_PATH)))

include CommonDefs.mk

SUDO ?= sudo
CMAKE_BUILD_EXTRA_OPTIONS ?=
INSTALL_PREFIX ?= $(MKFILE_DIR)/_install

.DEFAULT_GOAL := all

.PHONY: help
help:
	@echo "Usage:"
	@echo "  make help      show help message"
	@echo "  make init      init project"
	@echo "  make deps-u24  install Ubuntu 24.x OpenCV/ROS 2 deps"
	@echo "  make build     build project"
	@echo "  make install   build and install"
	@echo "  make samples   build samples"
	@echo "  make ros       build ros wrapper"
	@echo "  make ros2      build ros2 wrapper"
	@echo "  make record-native   record SDK camera/depth/IMU files"
	@echo "  make record-ros2-bag record ROS 2 camera/depth/IMU bag"
	@echo "  make record-ros1-bag record ROS 1 camera/depth/IMU bag"
	@echo "  make apidoc    build api doc"
	@echo "  make pkg       package sdk"
	@echo "  make clean     clean"
	@echo "  make cleanall  cleanall"

# all

all: init samples ros

.PHONY: all

# deps

submodules:
	@git submodule update --init

deps-u24:
	@$(call echo,Make $@)
	@$(SH) ./scripts/install_u24_deps.sh

.PHONY: deps-u24

# init

init:
	@$(call echo,Make $@)
	@$(SH) ./scripts/init.sh $(INIT_OPTIONS)

.PHONY: init

# build

build:
	@$(call echo,Make $@)
ifeq ($(HOST_OS),Win)
	@$(call cmake_build,./_build,..,-DCMAKE_INSTALL_PREFIX=$(MKFILE_DIR)/_install $(CMAKE_BUILD_EXTRA_OPTIONS))
else
	@$(call cmake_build,./_build,..,-DCMAKE_INSTALL_PREFIX=$(INSTALL_PREFIX) $(CMAKE_BUILD_EXTRA_OPTIONS))
endif

.PHONY: build

# install

install: uninstall build
	@$(call echo,Make $@)
ifeq ($(HOST_OS),Win)
ifneq ($(HOST_NAME),MinGW)
	@cd ./_build; msbuild.exe INSTALL.vcxproj /property:Configuration=Release
else
	@cd ./_build; make install
endif
else
ifeq ($(HOST_OS),Linux)
	@cd ./_build; make install
else
	@cd ./_build; make install
endif
endif

.PHONY: install

uninstall:
	@$(call echo,Make $@)
ifeq ($(HOST_OS),Linux)
ifneq ($(INSTALL_PREFIX),/usr/local)
	$(call rm,$(INSTALL_PREFIX))
else
	$(SUDO) rm -rf /usr/local/include/mynteyed/
	$(SUDO) rm -rf /usr/local/lib/libmynteye_depth.so*
	$(SUDO) rm -rf /usr/local/lib/3rdparty/libeSPDI.so*
	$(SUDO) rm -rf /usr/local/lib/cmake/mynteyed/
	$(SUDO) rm -rf /usr/local/share/mynteyed/
endif
endif

.PHONY: uninstall

# samples

samples: install
	@$(call echo,Make $@)
	@if [ -f ./_install/lib/cmake/mynteyed/mynteyed-config.cmake ] && \
		grep -q "set(mynteyed_WITH_OPENCV FALSE)" ./_install/lib/cmake/mynteyed/mynteyed-config.cmake; then \
		$(ECHO) "Skip samples: OpenCV was not found when building SDK."; \
		$(ECHO) "Install libopencv-dev and rerun make samples to build sample programs."; \
	else \
		$(call cmake_build,./samples/_build); \
	fi

.PHONY: samples

# ros

ros: install
ifeq ($(HOST_OS),Linux)
	@$(call echo,Make $@)
	@if command -v catkin_make >/dev/null 2>&1; then \
		cd ./wrappers/ros && catkin_make; \
	elif command -v ros2 >/dev/null 2>&1 || [ -n "$${AMENT_PREFIX_PATH}" ]; then \
		$(ECHO) "Skip ros: this repository contains a ROS 1 catkin/nodelet wrapper."; \
		$(ECHO) "Build the ROS 2 rclcpp wrapper from wrappers/ros2 with colcon."; \
	else \
		$(ECHO) "Skip ros: catkin_make not found. Install/source ROS 1 before running make ros."; \
	fi
endif

cleanros:
	@$(call echo,Make $@)
	@$(call rm,./wrappers/ros/build/)
	@$(call rm,./wrappers/ros/devel/)
	@$(call rm,./wrappers/ros/install/)
	@$(call rm,./wrappers/ros/.catkin_workspace)
	@$(call rm,./wrappers/ros/src/CMakeLists.txt)

.PHONY: ros cleanros

ros2: install
ifeq ($(HOST_OS),Linux)
	@$(call echo,Make $@)
	@if [ -f "$${ROS_SETUP:-/opt/ros/jazzy/setup.bash}" ]; then \
		. "$${ROS_SETUP:-/opt/ros/jazzy/setup.bash}"; \
	fi; \
	cd ./wrappers/ros2 && colcon build --symlink-install
endif

.PHONY: ros2

record-native: samples
ifeq ($(HOST_OS),Linux)
	@$(call echo,Make $@)
	@./scripts/record_native.sh
endif

record-ros2-bag: ros2
ifeq ($(HOST_OS),Linux)
	@$(call echo,Make $@)
	@./scripts/record_ros2_bag.sh
endif

record-ros1-bag: ros
ifeq ($(HOST_OS),Linux)
	@$(call echo,Make $@)
	@./scripts/record_ros1_bag.sh
endif

.PHONY: record-native record-ros2-bag record-ros1-bag

# doc

doc: apidoc

apidoc: cleandoc
	@$(call echo,Make $@)
	@cd docs; make html

opendoc: apidoc
	@$(call echo,Make $@)
	@$(SH) ./scripts/open.sh docs/_build/html/index.html

cleandoc:
	@$(call rm,./docs/_build/)
	@$(call rm,./docs/_doxygen/)

.PHONY: doc apidoc opendoc cleandoc

# pkg

.PHONY: pkg
pkg:
	@$(call echo,Make $@)
ifeq ($(HOST_OS),Win)
	@$(MAKE) clean
	@$(SH) ./scripts/win/winpack.sh "$(PKGNAME)"
else ifeq ($(HOST_OS),Linux)
	@$(MAKE) install cleanros

	@$(call echo,Copy ./cmake to ./_install/cmake ...,1;35)
	@$(call cp,./cmake/Common.cmake,./_install/cmake/Common.cmake)
	@$(call cp,./cmake/DetectCXX11.cmake,./_install/cmake/DetectCXX11.cmake)
	@$(call cp,./cmake/DetectOpenCV.cmake,./_install/cmake/DetectOpenCV.cmake)
	@$(call cp,./cmake/IncludeGuard.cmake,./_install/cmake/IncludeGuard.cmake)

	@$(call echo,Copy ./samples to ./_install/samples ...,1;35)
	@$(call rm,./samples/_build/)
	@$(call rm,./samples/_output/)
	@./scripts/_cp_subs.sh ./samples ./_install/samples

	@$(call echo,Copy ./wrappers to ./_install/wrappers ...,1;35)
	@./scripts/_cp_subs.sh ./wrappers ./_install/wrappers

	@$(call echo,Copy ./platforms/? to ./_install ...,1;35)
	@$(call get_platform_path,./platforms); \
	if [ -n "$${plat_path}" ]; then \
		$(ECHO) "Copy $${plat_path} to ./_install ..."; \
		./scripts/_cp_subs.sh $${plat_path} ./_install; \
	fi

	@$(shell sh ./pkginfo.sh); dst=$(PKGNAME)-opencv-$$OpenCV_VERSION; \
	$(call echo,Copy ./_install to $$dst ...,1;35); \
	$(call rm,$$dst); $(ECHO) "CP: ./_install > $$dst"; cp -Rp "./_install/." "$$dst"; \
	$(call echo,Compress $$dst.tar.gz ...,1;35); \
	tar -zcf $$dst.tar.gz $$dst; \
	$(call echo,Compress $$dst.tar.gz done,1;35)
else
	$(error "Can't make pkg on $(HOST_OS)")
endif

.PHONY: cleanpkg
cleanpkg:
	@$(call echo,Make $@)
	@$(call rm_f,$(PKGNAME)*)

# clean

.PHONY: clean
clean:
	@$(call echo,Make $@)
	@$(call rm,./_build/)
	@$(call rm,./_output/)
	@$(call rm,./_install/)
	@$(call rm,./samples/_build/)
	@$(call rm,./samples/_output/)
	@$(call rm,./pkginfo.sh)
	@$(FIND) . -type f -name ".DS_Store" -print0 | xargs -0 rm -f

.PHONY: cleanall
cleanall: clean cleanros cleandoc cleanpkg
	@$(call echo,Make $@)
	@$(call rm_f,build-*,./apps/)

.PHONY: host
host:
	@$(call echo,Make $@)
	@echo HOST_OS: $(HOST_OS)
	@echo HOST_ARCH: $(HOST_ARCH)
	@echo HOST_NAME: $(HOST_NAME)
	@echo ECHO: $(ECHO)
	@echo CC: $(CC)
	@echo CXX: $(CXX)
	@echo MAKE: $(MAKE)
	@echo BUILD: $(BUILD)
	@echo CMAKE: $(CMAKE)
	@echo LDD: $(LDD)
	@echo FIND: $(FIND)
	@echo PKGNAME: $(PKGNAME)
	@echo CMAKE_BUILD_EXTRA_OPTIONS: $(CMAKE_BUILD_EXTRA_OPTIONS)
