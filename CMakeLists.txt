# To force a specific revision. This is usually set in the build environment.
# set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.rev6")

# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Disable learning of AC IR protocols
add_compile_options(
    -DDECODE_AIRTON=false
    -DDECODE_AIRWELL=false
    -DDECODE_AMCOR=false
    -DDECODE_ARGO=false
    -DDECODE_BLUESTARHEAVY
    -DDECODE_BOSCH144=false
    -DDECODE_CARRIER_AC=false
    -DDECODE_CARRIER_AC128=false
    -DDECODE_CARRIER_AC40=false
    -DDECODE_CARRIER_AC64=false
    -DDECODE_CARRIER_AC84=false
    -DDECODE_CLIMABUTLER=false
    -DDECODE_COOLIX=false
    -DDECODE_COOLIX48=false
    -DDECODE_CORONA_AC=false
    -DDECODE_DAIKIN=false
    -DDECODE_DAIKIN128=false
    -DDECODE_DAIKIN152=false
    -DDECODE_DAIKIN160=false
    -DDECODE_DAIKIN176=false
    -DDECODE_DAIKIN2=false
    -DDECODE_DAIKIN200=false
    -DDECODE_DAIKIN216=false
    -DDECODE_DAIKIN312=false
    -DDECODE_DAIKIN64=false
    -DDECODE_DELONGHI_AC=false
    -DDECODE_ECOCLIM=false
    -DDECODE_ELECTRA_AC=false
    -DDECODE_FUJITSU_AC=false
    -DDECODE_GREE=false
    -DDECODE_HAIER_AC_YRW02=false
    -DDECODE_HAIER_AC=false
    -DDECODE_HAIER_AC160=false
    -DDECODE_HAIER_AC176=false
    -DDECODE_HITACHI_AC=false
    -DDECODE_HITACHI_AC1=false
    -DDECODE_HITACHI_AC2=false
    -DDECODE_HITACHI_AC264=false
    -DDECODE_HITACHI_AC296=false
    -DDECODE_HITACHI_AC3=false
    -DDECODE_HITACHI_AC344=false
    -DDECODE_HITACHI_AC424=false
    -DDECODE_KELON=false
    -DDECODE_KELON168=false
    -DDECODE_KELVINATOR=false
    -DDECODE_MIDEA=false
    -DDECODE_MIDEA24=false
    -DDECODE_MIRAGE=false
    -DDECODE_MITSUBISHI_AC=false
    -DDECODE_MITSUBISHI112=false
    -DDECODE_MITSUBISHI136=false
    -DDECODE_MITSUBISHIHEAVY=false
    -DDECODE_MWM=false
    -DDECODE_NEOCLIMA=false
    -DDECODE_PANASONIC_AC=false
    -DDECODE_PANASONIC_AC32=false
    -DDECODE_RHOSS=false
    -DDECODE_SAMSUNG_AC=false
    -DDECODE_SANYO_AC=false
    -DDECODE_SANYO_AC152=false
    -DDECODE_SANYO_AC88=false
    -DDECODE_SHARP_AC=false
    -DDECODE_TECO=false
    -DDECODE_TCL112AC=false
    -DDECODE_TCL96AC=false
    -DDECODE_TECHNIBEL_AC=false
    -DDECODE_TEKNOPOINT=false
    -DDECODE_TOSHIBA_AC=false
    -DDECODE_TRANSCOLD=false
    -DDECODE_TROTEC_3550=false
    -DDECODE_TROTEC=false
    -DDECODE_TRUMA=false
    -DDECODE_VESTEL_AC=false
    -DDECODE_VOLTAS=false
    -DDECODE_WHIRLPOOL_AC=false
    -DDECODE_YORK=false
)

# Disable learning of other IR protocols
add_compile_options(
    -DSEND_LASERTAG=false
    -DDECODE_LASERTAG=false
    -DSEND_MILESTAG2=false
    -DDECODE_MILESTAG2=false
)

project(ucd3-firmware)

# Generate lang.js before FrogFS build
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_target(generate_lang ALL
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build_lang.py
    COMMENT "Generating lang.js from language files"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

target_add_frogfs(${PROJECT_NAME}.elf)

include(CheckGit.cmake)
CheckGitSetup()
idf_build_set_property(COMPILE_OPTIONS "-D DOCK_VERSION=\"$ENV{GIT_VERSION}\"" APPEND)
