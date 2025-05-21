# To force a specific revision. This is usually set in the build environment.
# set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.rev3")

# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(ucd3-firmware)

target_add_frogfs(${PROJECT_NAME}.elf)

include(CheckGit.cmake)
CheckGitSetup()
idf_build_set_property(COMPILE_OPTIONS "-D DOCK_VERSION=\"$ENV{GIT_VERSION}\"" APPEND)
