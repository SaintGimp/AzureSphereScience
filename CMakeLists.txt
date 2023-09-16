#  Copyright (c) Microsoft Corporation. All rights reserved.
#  Licensed under the MIT License.

cmake_minimum_required (VERSION 3.20)

project (AzureSphereScience C)

azsphere_configure_tools(TOOLS_REVISION "23.05")
azsphere_configure_api(TARGET_API_SET "16")

# Create executable
add_executable(${PROJECT_NAME} main.c eventloop_timer_utilities.c geiger.c "logstash.c" "upload.c" log_utils.c "bmp180.c")
target_link_libraries(${PROJECT_NAME} applibs gcc_s c curl)
azsphere_target_hardware_definition(${PROJECT_NAME} TARGET_DEFINITION "seeed_mt3620_mdb.json")

azsphere_target_add_image_package(${PROJECT_NAME})
