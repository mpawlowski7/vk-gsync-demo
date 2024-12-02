#ifndef VULKAN_H
#define VULKAN_H

#define APP_NAME "vk-gsync-demo"

#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>

SDL_bool InitializeVulkan(SDL_Window* pWindowHandle, int width, int height);
void Update(float position);
void Draw();
void CleanupVulkan();

#endif //VULKAN_H
