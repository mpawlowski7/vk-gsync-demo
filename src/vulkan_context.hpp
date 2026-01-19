#ifndef VULKAN_CONTEXT_HPP
#define VULKAN_CONTEXT_HPP

#define VULKAN_DEBUG 1
#define APP_NAME "vk-gsync-demo"

#include <vector>

#include <SDL3/SDL_vulkan.h>
// #include <vulkan/vulkan.h>
#include <vulkan/vulkan.hpp>

class VulkanContext {
public:
    VulkanContext() = default;
    VulkanContext(SDL_Window *window, int width, int height);
    // TODO move to scene like class not related to vulkan
    void Update(float position);
    void Draw();

private:
    bool InitVulkan();
    void CleanupVulkan();

    bool initVulkanCore();
    bool initLogicalDevice();
    bool initSwapchain();
    bool createRenderPass();
    bool createFramebuffers();
    bool createSyncObjects();
    bool createPipeline();
    bool createCommandBuffers();
    void drawRectangle() const;

    // Utils
    bool prepareShaderModule(const uint32_t *shaderBinary,
                             uint32_t shaderSize,
                             VkShaderModule *pShaderModule) const;

    std::vector<const char*>             m_enabledLayers;
    std::vector<const char*>             m_enabledInstanceExt;
    std::vector<const char*>             m_enabledDeviceExt;

    VkInstance                           m_instance;
#if VULKAN_DEBUG
    VkDebugReportCallbackEXT             m_debugCallbackEXT;
#endif
    VkPhysicalDevice                     m_physicalDevice;
    VkPhysicalDeviceProperties           m_physicalDeviceProperties;
    VkPhysicalDeviceFeatures             m_physicalDeviceFeatures;
    VkPhysicalDeviceMemoryProperties     m_physicalDeviceMemoryProperties;
    std::vector<VkQueueFamilyProperties> m_queueFamilyProperties;
    VkDevice                             m_device;
    VkQueue                              m_presentQueue;

    VkCommandPool                        m_commandPool;
    VkCommandBuffer                      m_cmdBufferDraw;

    VkSurfaceKHR                         m_surface{};
    VkSwapchainKHR                       m_swapchain;
    VkSurfaceCapabilitiesKHR             m_surfaceCapabilities;
    VkSurfaceFormatKHR                   m_surfaceFormat;
    VkExtent2D                           m_swapchainExtent;
    std::vector<VkImage>                 m_swapchainImages;
    std::vector<VkImageView>             m_colorImageViews;
    uint32_t                             m_swapchainImageCount;

    VkRenderPass                         m_renderPass;
    std::vector<VkFramebuffer>           m_framebuffers;
    VkFence                              m_renderFence;
    VkSemaphore                          m_renderSemaphore,
                                         m_presentSemaphore;

    VkPipelineLayout                     m_pipelineLayout;
    VkPipeline                           m_pipeline;


    SDL_Window                          *m_sdlWindow = nullptr;
    int                                  m_outWidth  = 1280;
    int                                  m_outHeight = 800;
};

#endif //VULKAN_CONTEXT_HPP
