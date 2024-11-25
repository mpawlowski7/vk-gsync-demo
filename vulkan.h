//
// Created by mipw on 08.07.24.
//

#ifndef VULKAN_H
#define VULKAN_H

#include <SDL2/SDL_vulkan.h>

SDL_bool InitializeVulkan(SDL_Window* appWindow);
void Update();
void Draw();
void CleanupVulkan();

#endif //VULKAN_H
