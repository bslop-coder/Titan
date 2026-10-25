cmake_minimum_required(VERSION 3.22.1)
project("titan")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SD_VULKAN ON CACHE BOOL "" FORCE)
set(SD_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SD_WEBP OFF CACHE BOOL "" FORCE)
set(SD_WEBM OFF CACHE BOOL "" FORCE)
set(SD_USE_SYSTEM_WEBP ON CACHE BOOL "" FORCE)

find_program(GLSLC_EXECUTABLE NAMES glslc HINTS /usr/bin)
if (GLSLC_EXECUTABLE)
    set(Vulkan_GLSLC_EXECUTABLE "${GLSLC_EXECUTABLE}" CACHE FILEPATH "" FORCE)
endif()

if (EXISTS "/tmp/spirv-headers/install/share/cmake/SPIRV-Headers")
    set(SPIRV-Headers_DIR "/tmp/spirv-headers/install/share/cmake/SPIRV-Headers" CACHE PATH "" FORCE)
endif()

if (EXISTS "/tmp/vulkan-headers/include")
    include_directories(SYSTEM "/tmp/vulkan-headers/include")
    set(Vulkan_INCLUDE_DIR "/tmp/vulkan-headers/include" CACHE PATH "" FORCE)
endif()

add_subdirectory(sdcpp)

add_library(titan_jni SHARED
    sd_jni.cpp
)

find_library(log-lib log)
find_library(android-lib android)
find_library(vulkan-lib vulkan)

target_include_directories(titan_jni PRIVATE
    sdcpp/include
    sdcpp/
)

target_link_libraries(titan_jni
    stable-diffusion
    ${log-lib}
    ${android-lib}
    ${vulkan-lib}
)
