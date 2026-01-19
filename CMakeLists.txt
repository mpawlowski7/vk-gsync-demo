cmake_minimum_required(VERSION 3.30)

project(vk-gsync-demo CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Vulkan REQUIRED)
find_package(fmt REQUIRED)
find_package(SDL3 REQUIRED CONFIG REQUIRED COMPONENTS SDL3-shared)

add_executable(vk-gsync-demo
               src/vulkan_context.cpp
               src/main.cpp
)

target_link_libraries(vk-gsync-demo
                      PRIVATE fmt::fmt
                      PRIVATE SDL3::SDL3
                      PRIVATE Vulkan::Vulkan)