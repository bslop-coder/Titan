cmake_minimum_required(VERSION 3.22.1)
project("titan")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SD_VULKAN ON CACHE BOOL "" FORCE)
set(SD_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SD_WEBP OFF CACHE BOOL "" FORCE)

add_subdirectory(sdcpp)

add_library(titan_jni SHARED
    sd_jni.cpp
)

find_library(log-lib log)
find_library(android-lib android)
find_library(vulkan-lib vulkan)

target_include_directories(titan_jni PRIVATE
    sdcpp/
    sdcpp/include
)

target_link_libraries(titan_jni
    stable-diffusion
    ${log-lib}
    ${android-lib}
    ${vulkan-lib}
)
