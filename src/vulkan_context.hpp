#ifndef VULKAN_CONTEXT_HPP
#define VULKAN_CONTEXT_HPP

#define VULKAN_DEBUG 1
#define APP_NAME "vk-gsync-demo"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#endif

#include <vector>

#include <SDL3/SDL_vulkan.h>
#ifdef _WIN32
#include <vulkan/vulkan.h>
#else
#include <vulkan/vulkan.hpp>
#endif

// Vulkan 1.4 context. Drawing uses dynamic rendering (core since 1.3) and
// VK_EXT_shader_object instead of a render pass, framebuffer, and graphics
// pipeline. vulkan_context.cpp comments the difference from that 1.0 path.
class VulkanContext
{
public:
    VulkanContext() = default;
    VulkanContext(SDL_Window *window, int width, int height);
    // TODO move to scene like class not related to vulkan
    void Update(float position);
    void Draw() const;

private:
    bool InitVulkan();
    void CleanupVulkan();

    bool initVulkanCore();
    bool initLogicalDevice();
    bool initSwapchain();
    bool createSyncObjects();
    bool createShaderObjects();
    bool createCommandBuffers();
    void drawRectangle() const;
    // Fixed-function state that a graphics pipeline used to bake in.
    void setShaderObjectDynamicState() const;
    // Replaces the render pass initialLayout/finalLayout transitions.
    void transitionColorImage(VkImage image,
                              VkImageLayout oldLayout,
                              VkImageLayout newLayout) const;
    // libvulkan does not export device-extension commands. Load them after
    // vkCreateDevice or the link fails (undefined reference to
    // vkCmdSetAlphaToOneEnableEXT and the other *EXT entry points).
    bool loadShaderObjectFunctions();

    std::vector<const char*>             m_enabledLayers;
    std::vector<const char*>             m_enabledInstanceExt;
    std::vector<const char*>             m_enabledDeviceExt;

    VkInstance                           m_instance{};
#if VULKAN_DEBUG
    VkDebugReportCallbackEXT             m_debugCallbackEXT{};
#endif
    VkPhysicalDevice                     m_physicalDevice{};
    VkPhysicalDeviceProperties           m_physicalDeviceProperties{};
    VkPhysicalDeviceFeatures             m_physicalDeviceFeatures{};
    VkPhysicalDeviceMemoryProperties     m_physicalDeviceMemoryProperties{};
    std::vector<VkQueueFamilyProperties> m_queueFamilyProperties;
    VkDevice                             m_device{};
    VkQueue                              m_presentQueue{};

    VkCommandPool                        m_commandPool{};
    VkCommandBuffer                      m_cmdBufferDraw{};

    VkSurfaceKHR                         m_surface{};
    VkSwapchainKHR                       m_swapchain{};
    VkSurfaceCapabilitiesKHR             m_surfaceCapabilities{};
    VkSurfaceFormatKHR                   m_surfaceFormat{};
    VkExtent2D                           m_swapchainExtent{};
    std::vector<VkImage>                 m_swapchainImages;
    std::vector<VkImageView>             m_colorImageViews;
    uint32_t                             m_swapchainImageCount{};

    VkFence                              m_renderFence{};
    VkSemaphore                          m_renderSemaphore{},
                                         m_presentSemaphore{};

    // No VkRenderPass / VkFramebuffer: dynamic rendering names the image view
    // in vkCmdBeginRendering. No VkPipeline: each stage is a VkShaderEXT.
    // The layout stays. Shader objects replace the pipeline, not the object
    // push constants are addressed through.
    VkPipelineLayout                     m_pipelineLayout{};
    VkShaderEXT                          m_vertexShader{};
    VkShaderEXT                          m_fragmentShader{};

    // Resolved with vkGetDeviceProcAddr. Core 1.3 commands stay direct calls.
    struct ShaderObjectFns {
        PFN_vkCreateShadersEXT                 vkCreateShadersEXT{};
        PFN_vkDestroyShaderEXT                 vkDestroyShaderEXT{};
        PFN_vkCmdBindShadersEXT                vkCmdBindShadersEXT{};
        PFN_vkCmdSetVertexInputEXT             vkCmdSetVertexInputEXT{};
        PFN_vkCmdSetPolygonModeEXT             vkCmdSetPolygonModeEXT{};
        PFN_vkCmdSetRasterizationSamplesEXT    vkCmdSetRasterizationSamplesEXT{};
        PFN_vkCmdSetSampleMaskEXT              vkCmdSetSampleMaskEXT{};
        PFN_vkCmdSetAlphaToCoverageEnableEXT   vkCmdSetAlphaToCoverageEnableEXT{};
        PFN_vkCmdSetAlphaToOneEnableEXT        vkCmdSetAlphaToOneEnableEXT{};
        PFN_vkCmdSetLogicOpEnableEXT           vkCmdSetLogicOpEnableEXT{};
        PFN_vkCmdSetColorBlendEnableEXT        vkCmdSetColorBlendEnableEXT{};
        PFN_vkCmdSetColorBlendEquationEXT      vkCmdSetColorBlendEquationEXT{};
        PFN_vkCmdSetColorWriteMaskEXT          vkCmdSetColorWriteMaskEXT{};
    } m_fns;


    SDL_Window                          *m_sdlWindow = nullptr;
    int                                  m_outWidth  = 1280;
    int                                  m_outHeight = 800;
};

#endif //VULKAN_CONTEXT_HPP
