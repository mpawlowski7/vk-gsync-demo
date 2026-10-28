cmake_minimum_required(VERSION 4.0)

project(vk-gsync-demo CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/fmt/CMakeLists.txt"
   OR NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/SDL/CMakeLists.txt")
  message(FATAL_ERROR
    "fmt and SDL3 submodules are missing. From the repo root run:\n"
    "  git submodule update --init --recursive")
endif()

# Vendored fmt 12.2.0. Header-only module builds are unnecessary here.
set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
set(FMT_DOC OFF CACHE BOOL "" FORCE)
set(FMT_TEST OFF CACHE BOOL "" FORCE)
set(FMT_FUZZ OFF CACHE BOOL "" FORCE)
set(FMT_CUDA_TEST OFF CACHE BOOL "" FORCE)
set(FMT_MODULE OFF CACHE BOOL "" FORCE)
add_subdirectory(deps/fmt EXCLUDE_FROM_ALL)

# Vendored SDL 3.4.18. Shared SDL3::SDL3 matches the previous link line.
# Only video + Vulkan are needed; other subsystems stay off so configure
# does not look for audio, joystick, or gamepad packages.
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL_CPACK OFF CACHE BOOL "" FORCE)
set(SDL_UNINSTALL OFF CACHE BOOL "" FORCE)
set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_RENDER OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_JOYSTICK OFF CACHE BOOL "" FORCE)
set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
set(SDL_HIDAPI OFF CACHE BOOL "" FORCE)
set(SDL_POWER OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
set(SDL_DIALOG OFF CACHE BOOL "" FORCE)
set(SDL_TRAY OFF CACHE BOOL "" FORCE)
set(SDL_VULKAN ON CACHE BOOL "" FORCE)
# Optional X11 text/input helpers. The Linux Vulkan path does not use them.
set(SDL_X11_XTEST OFF CACHE BOOL "" FORCE)
set(SDL_FRIBIDI OFF CACHE BOOL "" FORCE)
set(SDL_LIBTHAI OFF CACHE BOOL "" FORCE)
add_subdirectory(deps/SDL EXCLUDE_FROM_ALL)

# Vulkan loader and headers still come from the system SDK.
if(WIN32 AND NOT DEFINED ENV{VULKAN_SDK})
  message(STATUS
    "VULKAN_SDK is not set. Install the LunarG Vulkan SDK and open a new shell before configuring.")
endif()
find_package(Vulkan REQUIRED)

add_executable(vk-gsync-demo
  src/vulkan_context.cpp
  src/main.cpp
)

target_link_libraries(vk-gsync-demo
  PRIVATE fmt::fmt
  PRIVATE SDL3::SDL3
  PRIVATE Vulkan::Vulkan
)

if(WIN32)
  # Windows variant. The Linux X11/Wayland path is left unchanged.
  message(STATUS "vk-gsync-demo: Windows variant (Win32 Vulkan surface)")
  target_compile_definitions(vk-gsync-demo PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    VK_USE_PLATFORM_WIN32_KHR
  )
  # Console app: fps logging uses stdout, and the entry point stays main().
  set_target_properties(vk-gsync-demo PROPERTIES WIN32_EXECUTABLE FALSE)
  if(MSVC)
    target_compile_options(vk-gsync-demo PRIVATE /Zc:__cplusplus /permissive- /EHsc)
    target_link_options(vk-gsync-demo PRIVATE /SUBSYSTEM:CONSOLE)
  endif()
  add_custom_command(TARGET vk-gsync-demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3>
            $<TARGET_FILE_DIR:vk-gsync-demo>
    VERBATIM
  )
else()
  message(STATUS "vk-gsync-demo: Linux variant (X11/Wayland Vulkan surface)")
  # Vendored libSDL3.so is not on the system library path.
  set_target_properties(vk-gsync-demo PROPERTIES
    BUILD_RPATH "$<TARGET_FILE_DIR:SDL3::SDL3>"
    INSTALL_RPATH "$ORIGIN"
  )
endif()
