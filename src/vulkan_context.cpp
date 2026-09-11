#include "vulkan_context.hpp"
#include <X11/Xlib.h>

#include "shaders/rectangle_frag.spv.h"
#include "shaders/rectangle_vert.spv.h"

#include <fmt/core.h>
#include <fmt/format.h>


// Definitions
//

#define USE_DIRECT_DISPLAY 0

#define VK_KHR_WAYLAND_SURFACE                     "VK_KHR_wayland_surface"
#define VK_KHR_XLIB_SURFACE_EXTENSION_NAME         "VK_KHR_xlib_surface"
#define VK_EXT_ACQUIRE_XLIB_DISPLAY_EXTENSION_NAME "VK_EXT_acquire_xlib_display"

#if USE_DIRECT_DISPLAY
  typedef VkResult (VKAPI_PTR *PFN_vkAcquireXlibDisplayEXT)(VkPhysicalDevice physicalDevice, Display* dpy, VkDisplayKHR display);
#endif



// Helper structs
//

typedef struct Position_t
{
    float x;
} Position;

static Position delta = {0.0f};


// Function declaration
//

auto format_as(VkResult result) { return fmt::underlying(result); }
#if VULKAN_DEBUG
  static PFN_vkCreateDebugReportCallbackEXT SDL2_vkCreateDebugReportCallbackEXT;
#endif

#if USE_DIRECT_DISPLAY
  static PFN_vkAcquireXlibDisplayEXT pfn_vkAcquireXlibDisplayEXT = VK_NULL_HANDLE;
#endif


// ------ Public API ----------
//

VulkanContext::VulkanContext(SDL_Window *sdlWindow, int width, int height)
    : m_outWidth(width), m_outHeight(height)
{
    m_enabledInstanceExt = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
        VK_KHR_WAYLAND_SURFACE,

    #if USE_DIRECT_DISPLAY
        VK_KHR_DISPLAY_EXTENSION_NAME,
        VK_EXT_DIRECT_MODE_DISPLAY_EXTENSION_NAME,
        VK_EXT_ACQUIRE_XLIB_DISPLAY_EXTENSION_NAME,
    #endif
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME
    };

    m_enabledDeviceExt = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

#if VULKAN_DEBUG
    m_enabledLayers = { "VK_LAYER_KHRONOS_validation" };
#endif

    m_sdlWindow = sdlWindow;

    InitVulkan();
}

// Main starting point for Vulkan
//
bool VulkanContext::InitVulkan()
{
    if (!initVulkanCore()) {
        return false;
    }

    if (!initLogicalDevice()) {
        return false;
    }

    if (!initSwapchain()) {
        return false;
    }

    if (!createRenderPass()) {
        return false;
    }

    if (!createGraphicsPipeline()) {
        return false;
    }

    if (!createFramebuffers()) {
        return false;
    }

    if (!createCommandBuffers()) {
        return false;
    }

    if (!createSyncObjects()) {
        return false;
    }

    return true;
}

void VulkanContext::Update(float position)
{
    delta.x = position;
}

void VulkanContext::Draw()
{
    VkClearValue clearValue = {0.2f, 0.2f, 0.2f, 1.0f};

    vkWaitForFences(m_device, 1, &m_renderFence, VK_TRUE, UINT64_MAX);
    vkResetFences(m_device, 1, &m_renderFence);

    uint32_t swapchainImageIndex = 0;
    vkAcquireNextImageKHR(
        m_device,
        m_swapchain,
        UINT64_MAX,
        m_presentSemaphore,
        VK_NULL_HANDLE,
        &swapchainImageIndex);

    vkResetCommandBuffer(m_cmdBufferDraw, 0);

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(m_cmdBufferDraw, &beginInfo);
    {
        VkRenderPassBeginInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;

        renderPassInfo.renderPass = m_renderPass;
        renderPassInfo.renderArea.offset.x = 0;
        renderPassInfo.renderArea.offset.y = 0;
        renderPassInfo.renderArea.extent = m_swapchainExtent;
        renderPassInfo.framebuffer = m_framebuffers[swapchainImageIndex];

        //connect clear values
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearValue;

        vkCmdBeginRenderPass(m_cmdBufferDraw, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        drawRectangle();

        vkCmdEndRenderPass(m_cmdBufferDraw);
    }
    vkEndCommandBuffer(m_cmdBufferDraw);

    // Submit
    {
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        VkSubmitInfo submit = {};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.pWaitDstStageMask = &waitStage;
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &m_presentSemaphore;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &m_renderSemaphore;

        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &m_cmdBufferDraw;

        vkQueueSubmit(m_presentQueue, 1, &submit, m_renderFence);
    }

    // Present
    {
        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.pSwapchains = &m_swapchain;
        presentInfo.swapchainCount = 1;

        presentInfo.pWaitSemaphores = &m_renderSemaphore;
        presentInfo.waitSemaphoreCount = 1;

        presentInfo.pImageIndices = &swapchainImageIndex;

        vkQueuePresentKHR(m_presentQueue, &presentInfo);
    }
}

// Release Vulkan resources
//
void VulkanContext::CleanupVulkan()
{
    if (m_instance != VK_NULL_HANDLE) {
        vkDestroySemaphore(m_device, m_presentSemaphore, VK_NULL_HANDLE);
        vkDestroySemaphore(m_device, m_renderSemaphore, VK_NULL_HANDLE);
        vkDestroyFence(m_device, m_renderFence, VK_NULL_HANDLE);
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, VK_NULL_HANDLE);
        vkDestroyPipeline(m_device, m_pipeline, VK_NULL_HANDLE);
        vkDestroyCommandPool(m_device, m_commandPool, VK_NULL_HANDLE);
        vkDestroyRenderPass(m_device, m_renderPass, VK_NULL_HANDLE);
        vkDestroySwapchainKHR(m_device, m_swapchain, VK_NULL_HANDLE);

        for (int i = 0; i < m_swapchainImageCount; i++) {
            vkDestroyFramebuffer(m_device, m_framebuffers[i], VK_NULL_HANDLE);
        }

        vkDestroyDevice(m_device, VK_NULL_HANDLE);
        vkDestroyInstance(m_instance, VK_NULL_HANDLE);

        m_instance = VK_NULL_HANDLE;
    }
}

// ------ Private API ----------
//
#if VULKAN_DEBUG
  static VkBool32 VKAPI_CALL vulkanDebugCallback(
    VkDebugReportFlagsEXT flags,
    VkDebugReportObjectTypeEXT objectType,
    uint64_t object,
    size_t location,
    int32_t messageCode,
    const char *pLayerPrefix,
    const char *pMessage,
    void *pUserData)
  {
    fmt::print("Debug cb called with msg:\n{}", pMessage);
    return VK_TRUE;
  }
#endif

bool VulkanContext::initVulkanCore()
{
    fmt::print("{} called\n", __func__);

    if (m_sdlWindow == nullptr) {
        fmt::print("App window not initialized.\n");
        return false;
    }

    VkApplicationInfo appInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = VK_NULL_HANDLE,
        .pApplicationName = APP_NAME,
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "vvv",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_0,
    };

    VkInstanceCreateInfo instanceInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = VK_NULL_HANDLE,
        .pApplicationInfo = &appInfo,
#if VULKAN_DEBUG
        .ppEnabledLayerNames = m_enabledLayers.data(),
#endif
        .enabledExtensionCount = static_cast<uint32_t>(m_enabledInstanceExt.size()),
        .ppEnabledExtensionNames = m_enabledInstanceExt.data(),
    };

    VkResult result = vkCreateInstance(&instanceInfo, VK_NULL_HANDLE, &m_instance);
    if (result != VK_SUCCESS) {
        fmt::print("Failed to create Vulkan instance. Result = %d\n", result);
        return false;
    }

#if VULKAN_DEBUG
    SDL2_vkCreateDebugReportCallbackEXT = (PFN_vkCreateDebugReportCallbackEXT) SDL_Vulkan_GetVkGetInstanceProcAddr();

    VkDebugReportCallbackCreateInfoEXT debugCallbackCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT,
        .flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT,
        .pfnCallback = vulkanDebugCallback,
    };

    result = SDL2_vkCreateDebugReportCallbackEXT(m_instance, &debugCallbackCreateInfo, VK_NULL_HANDLE, &m_debugCallbackEXT);
    if (result != VK_SUCCESS) {
      fmt::print("Failed to create debug callback.\n");
      return false;
    }
#endif

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, VK_NULL_HANDLE);
    if (deviceCount == 0) {
        return false;
    }

    VkPhysicalDevice deviceList[deviceCount];
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, deviceList);

    m_physicalDevice = deviceList[0];

    vkGetPhysicalDeviceProperties(m_physicalDevice, &m_physicalDeviceProperties);
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &m_physicalDeviceMemoryProperties);
    vkGetPhysicalDeviceFeatures(m_physicalDevice, &m_physicalDeviceFeatures);

    uint32_t queueFamilyPropertyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(
        m_physicalDevice,
        &queueFamilyPropertyCount,
        VK_NULL_HANDLE);

    m_queueFamilyProperties.resize(queueFamilyPropertyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(
        m_physicalDevice,
        &queueFamilyPropertyCount,
        m_queueFamilyProperties.data());

#if USE_DIRECT_DISPLAY
    pfn_vkAcquireXlibDisplayEXT =  (PFN_vkAcquireXlibDisplayEXT) vkGetInstanceProcAddr(m_instance, "vkAcquireXlibDisplayEXT");
    if (pfn_vkAcquireXlibDisplayEXT == VK_NULL_HANDLE) {
      fmt::print("Failed to load vkAcquireXlibDisplayEXT\n");
      return false;
    }
#endif
    return true;
}

bool VulkanContext::initLogicalDevice()
{
    fmt::print("{} called\n", __func__);

    float priority = 0.0;

    VkDeviceQueueCreateInfo queueInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .flags = 0,
        .queueFamilyIndex = 0,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };

    VkDeviceCreateInfo deviceInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .flags = 0,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueInfo,
#if VULKAN_DEBUG
        .enabledLayerCount =  static_cast<uint32_t>(m_enabledLayers.size()),
        .ppEnabledLayerNames = m_enabledLayers.data(),
#endif
        .enabledExtensionCount = static_cast<uint32_t>(m_enabledDeviceExt.size()),
        .ppEnabledExtensionNames = m_enabledDeviceExt.data(),
    };

    VkResult result = vkCreateDevice(m_physicalDevice, &deviceInfo, VK_NULL_HANDLE, &m_device);
    if (result != VK_SUCCESS) {
        return false;
    }

    vkGetDeviceQueue(m_device, 0, 0, &m_presentQueue);
    return true;
}

bool VulkanContext::initSwapchain()
{
    fmt::print("{} called\n", __func__);

    // Direct display surface
#if USE_DIRECT_DISPLAY
    {
        Display* dpy = XOpenDisplay(0);
        if (dpy == NULL) {
            fmt::print("Failed to open X display\n");
            return 1;
        } else {
            fmt::print("X display opened %p \n", dpy);
        }

        uint32_t displayCount;
        vkGetPhysicalDeviceDisplayPropertiesKHR(m_physicalDevice, &displayCount, VK_NULL_HANDLE);

        VkDisplayPropertiesKHR displayProperties[displayCount];
        vkGetPhysicalDeviceDisplayPropertiesKHR(m_physicalDevice, &displayCount, displayProperties);

        fmt::print( "Found displays = %d\n", displayCount);
        for (int i=0; i < displayCount; i++) {
            fmt::print("\t[%d] {} \n", i, displayProperties[i].displayName);
        }

        VkDisplayKHR selectedDisplay = displayProperties[0].display;
        VkResult result = pfn_vkAcquireXlibDisplayEXT(m_physicalDevice, dpy, selectedDisplay);
        if (result != VK_SUCCESS) {
            fmt::print("Failed to acquire display result = %d \n", result);
            return false;
        }

        uint32_t displayModesCount = 0;
        vkGetDisplayModePropertiesKHR(m_physicalDevice, selectedDisplay, &displayModesCount, VK_NULL_HANDLE);

        VkDisplayModePropertiesKHR displayModeProperites[displayModesCount];
        vkGetDisplayModePropertiesKHR(m_physicalDevice, selectedDisplay, &displayModesCount, displayModeProperites);

        // Select the highest refresh rate and resolution
        VkDisplayModePropertiesKHR selectedMode = displayModeProperites[0];
        for (int i=0; i < displayModesCount; i++) {
            VkExtent2D ires = displayModeProperites[i].parameters.visibleRegion;
            uint32_t   ifreq = displayModeProperites[i].parameters.refreshRate;

            VkExtent2D cres = selectedMode.parameters.visibleRegion;
            uint32_t cfreq = selectedMode.parameters.refreshRate;

            if(ires.height * ires.width + ifreq > cres.height * cres.width + cfreq ) {
                selectedMode = displayModeProperites[i];
            }
        }

        {
            m_swapchainExtent.width = selectedMode.parameters.visibleRegion.width;
            m_swapchainExtent.height = selectedMode.parameters.visibleRegion.height;
            fmt::print("swapchainExtent.width = %d, swapchainExtent.height = %d\n",
              m_swapchainExtent.width, m_swapchainExtent.height);
        }

        uint32_t planePropertiesCount = 0;
        vkGetPhysicalDeviceDisplayPlanePropertiesKHR(m_physicalDevice, &planePropertiesCount, VK_NULL_HANDLE);

        VkDisplayPlanePropertiesKHR planeProperties[planePropertiesCount];
        vkGetPhysicalDeviceDisplayPlanePropertiesKHR(m_physicalDevice, &planePropertiesCount, planeProperties);

        uint32_t planeIndex;
        SDL_bool foundPlane = false;
        for(uint32_t i = 0; i < planePropertiesCount; ++i) {
            VkDisplayPlanePropertiesKHR property = planeProperties[i];

            // skip planes bound to different display
            if(property.currentDisplay && (property.currentDisplay != selectedDisplay)) {
                continue;
            }

            uint32_t supportedDisplayCount = 0;
            vkGetDisplayPlaneSupportedDisplaysKHR(m_physicalDevice, i, &supportedDisplayCount, VK_NULL_HANDLE);

            VkDisplayKHR supportedDisplays[supportedDisplayCount];
            vkGetDisplayPlaneSupportedDisplaysKHR(m_physicalDevice, i, &supportedDisplayCount, supportedDisplays);
            for(int i = 0; i < supportedDisplayCount; i++) {
                if(supportedDisplays[i] == selectedDisplay) {
                    foundPlane = true;
                    planeIndex = i;
                    break;
                }
            }

            if(foundPlane) {
                break;
            }
        }

        if(!foundPlane) {
            fmt::print("Could not find a compatible display plane!\n");
            return false;
        }

        // find alpha mode bit
        VkDisplayPlaneCapabilitiesKHR planeCapabilites;
        vkGetDisplayPlaneCapabilitiesKHR(m_physicalDevice, selectedMode.displayMode, planeIndex, &planeCapabilites);

        VkDisplayPlaneAlphaFlagBitsKHR alphaMode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
        VkDisplayPlaneAlphaFlagBitsKHR alphaModes[4] = {
            VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR,
            VK_DISPLAY_PLANE_ALPHA_GLOBAL_BIT_KHR,
            VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_BIT_KHR,
            VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_PREMULTIPLIED_BIT_KHR
          };

        for(uint32_t i = 0; i < 4; i++) {
            if(planeCapabilites.supportedAlpha & alphaModes[i]) {
                alphaMode = alphaModes[i];
                break;
            }
        }

        VkDisplaySurfaceCreateInfoKHR displaySurfaceInfo = {};
        displaySurfaceInfo.sType = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR;
        displaySurfaceInfo.displayMode = selectedMode.displayMode;
        displaySurfaceInfo.planeIndex = planeIndex;
        displaySurfaceInfo.planeStackIndex = planeProperties[planeIndex].currentStackIndex;
        displaySurfaceInfo.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        displaySurfaceInfo.globalAlpha = 1.0f;
        displaySurfaceInfo.alphaMode = alphaMode;
        displaySurfaceInfo.imageExtent = m_swapchainExtent;

        result = vkCreateDisplayPlaneSurfaceKHR(m_instance, &displaySurfaceInfo, VK_NULL_HANDLE, &m_surface);
        if (result != VK_SUCCESS) {
            fmt::print("Failed to create display plane surface result = %d \n", result);
            return false;
        }
    }
#else
    // SDL surface
{
    if (!SDL_Vulkan_CreateSurface(m_sdlWindow, m_instance, VK_NULL_HANDLE, &m_surface)) {
        fmt::print("Failed to create surface: {}", SDL_GetError());
        return false;
    }

    m_swapchainExtent.width = m_outWidth;
    m_swapchainExtent.height = m_outHeight;
}
#endif
    VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        m_physicalDevice,
        m_surface,
        &m_surfaceCapabilities);
    if (result != VK_SUCCESS) {
        fmt::print("Failed to get surface capabilites = %d\n", result);
        return false;
    }

    uint32_t surfaceFormatsCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &surfaceFormatsCount, VK_NULL_HANDLE);

    int surfaceFormatIndex = 0;
    VkSurfaceFormatKHR surfaceFormats[surfaceFormatsCount];
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &surfaceFormatsCount, surfaceFormats);

    for (int i = 0; i < surfaceFormatsCount; i++) {
        if (surfaceFormats[i].format == VK_FORMAT_B8G8R8A8_UNORM) {
            surfaceFormatIndex = i;
        }
    }

    if (surfaceFormats[surfaceFormatIndex].format != VK_FORMAT_B8G8R8A8_UNORM) {

        fmt::print("VK_FORMAT_B8G8R8A8_UNORM not supported\n");
        return false;
    }

    m_surfaceFormat = surfaceFormats[surfaceFormatIndex];

    uint32_t imageCount = m_surfaceCapabilities.minImageCount + 1;
    if (m_surfaceCapabilities.maxImageCount > 0 &&
        imageCount > m_surfaceCapabilities. maxImageCount) {

        imageCount = m_surfaceCapabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR swapchainInfo = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = m_surface,
        .minImageCount = imageCount,
        .imageFormat = m_surfaceFormat.format,
        .imageColorSpace = m_surfaceFormat.colorSpace,
        .imageExtent = m_swapchainExtent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = m_surfaceCapabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR, // V-SYNC -> expose to enable/disable
        .clipped = VK_TRUE
}   ;

    result = vkCreateSwapchainKHR(m_device, &swapchainInfo, VK_NULL_HANDLE, &m_swapchain);
    if (result != VK_SUCCESS) {
        fmt::print("Failed to create swapchain result = %d\n", result);
        return false;
    }

    vkGetSwapchainImagesKHR(m_device, m_swapchain, &m_swapchainImageCount, VK_NULL_HANDLE);

    m_swapchainImages.resize(m_swapchainImageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &m_swapchainImageCount, m_swapchainImages.data());

    // Create ImageViews
    {
        m_colorImageViews.resize(m_swapchainImageCount);

        for (int i = 0; i < m_swapchainImageCount; i++) {
            VkImageViewCreateInfo colorInfo = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .flags = 0,
                .image = m_swapchainImages[i],
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = m_surfaceFormat.format,

                .components = {
                    .r = VK_COMPONENT_SWIZZLE_R,
                    .g = VK_COMPONENT_SWIZZLE_G,
                    .b = VK_COMPONENT_SWIZZLE_B,
                    .a = VK_COMPONENT_SWIZZLE_A,
                },

                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                }
            };

            result = vkCreateImageView(m_device, &colorInfo, VK_NULL_HANDLE, &m_colorImageViews[i]);
            if (result != VK_SUCCESS) {
                fmt::print("Failed to create image view for image index: %d \n", i);
                return false;
            }
        }
    }

    return true;
}

bool VulkanContext::createRenderPass()
{
    fmt::print("{} called\n", __func__);

    // the renderpass will use this color attachment.
    VkAttachmentDescription colorAttachment = {
        .format = m_surfaceFormat.format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    };

    VkAttachmentReference colorAttachmentReference = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };

    VkSubpassDescription subpassDescription = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachmentReference
    };

    VkRenderPassCreateInfo renderPassInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &colorAttachment,
        .subpassCount = 1,
        .pSubpasses = &subpassDescription
    };

    VkResult result = vkCreateRenderPass(m_device, &renderPassInfo, VK_NULL_HANDLE, &m_renderPass);
    if (result != VK_SUCCESS) {
        fmt::print("Failed to create rectangle renderpass\n");
        return false;
    }

    return true;
}

bool VulkanContext::createFramebuffers()
{
    fmt::print("{} called\n", __func__);

    VkResult result;

    VkFramebufferCreateInfo framebufferInfo = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = m_renderPass,
        .attachmentCount = 1,
        .width = m_swapchainExtent.width,
        .height = m_swapchainExtent.height,
        .layers = 1
    };

    m_framebuffers.resize(m_swapchainImageCount);
    for (int i = 0; i < m_swapchainImageCount; i++) {
        framebufferInfo.pAttachments = &m_colorImageViews[i];
        result = vkCreateFramebuffer(m_device, &framebufferInfo, VK_NULL_HANDLE, &m_framebuffers[i]);
        if (result != VK_SUCCESS) {
            fmt::print("Failed to create framebuffer\n");
            return false;
        }
    }

    return true;
}

bool VulkanContext::createCommandBuffers()
{
    fmt::print("{} called\n", __func__);

    VkCommandPoolCreateInfo commandPoolInfo = {};
    commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolInfo.queueFamilyIndex = 0;
    commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VkResult result = vkCreateCommandPool(
        m_device,
        &commandPoolInfo,
        VK_NULL_HANDLE,
        &m_commandPool);
    if (result != VK_SUCCESS) {
        fmt::print("Failed to create command pool\n");
        return false;
    }

    VkCommandBufferAllocateInfo commandBufferAllocateInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = m_commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };

    result = vkAllocateCommandBuffers(m_device, &commandBufferAllocateInfo, &m_cmdBufferDraw);
    if (result != VK_SUCCESS) {
        fmt::print("Failed to allocate command buffers\n");
        return false;
    }

    return true;
}

bool VulkanContext::createSyncObjects()
{
    fmt::print("{} called\n", __func__);

    VkResult result;

    VkSemaphoreCreateInfo semaphoreCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
    };

    result = vkCreateSemaphore(m_device, &semaphoreCreateInfo, VK_NULL_HANDLE, &m_presentSemaphore);
    if (result != VK_SUCCESS) {
        fmt::print("Failed to create present semaphore.\n");
        return false;
    }

    result = vkCreateSemaphore(m_device, &semaphoreCreateInfo, VK_NULL_HANDLE, &m_renderSemaphore);
    if (result != VK_SUCCESS) {
        fmt::print("Failed to create render semaphore.\n");
        return false;
    }

    VkFenceCreateInfo fenceCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT
    };

    result = vkCreateFence(m_device, &fenceCreateInfo, VK_NULL_HANDLE, &m_renderFence);
    if (result != VK_SUCCESS) {
        fmt::print("Failed to create render fence.\n");
        return false;
    }

    return true;
}

bool VulkanContext::createGraphicsPipeline()
{
    printf("%s called\n", __func__);

    VkResult result;

    VkShaderModule vert_shader_module;
    prepareShaderModule(rectangle_vert_spv, sizeof(rectangle_vert_spv), &vert_shader_module);

    VkShaderModule frag_shader_module;
    prepareShaderModule(rectangle_frag_spv, sizeof(rectangle_frag_spv), &frag_shader_module);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = vert_shader_module,
        .pName = "main",
    };

    VkPipelineShaderStageCreateInfo fragShaderStageInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = frag_shader_module,
        .pName = "main"
    };

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        vertShaderStageInfo,
        fragShaderStageInfo
    };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 0,
        .vertexAttributeDescriptionCount = 0
    };

    VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE
    };

    // Static viewport and scissors
    auto vpWidth = static_cast<float>(m_swapchainExtent.width);
    auto vpHeight = static_cast<float>(m_swapchainExtent.height);
    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = vpWidth,
        .height = vpHeight,
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };

    VkRect2D scissor = {
        .extent = m_swapchainExtent
    };

    VkPipelineViewportStateCreateInfo viewportInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor
    };

    VkPipelineRasterizationStateCreateInfo rasterizerInfo = {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable        = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode             = VK_POLYGON_MODE_FILL,
        .cullMode                = VK_CULL_MODE_NONE,
        .frontFace               = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable         = VK_FALSE,
        .lineWidth               = 1.0f
    };

    VkPipelineMultisampleStateCreateInfo multisamplingInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE
    };

    VkPipelineColorBlendAttachmentState colorBlendingAttachment = {
        .blendEnable    = VK_FALSE,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                          VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT |
                          VK_COLOR_COMPONENT_A_BIT
    };

    VkPipelineColorBlendStateCreateInfo colorBlendingInfo = {
        .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable   = VK_FALSE,
        .attachmentCount = 1,
        .pAttachments    = &colorBlendingAttachment
    };

    VkPushConstantRange pushConstant = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(Position),
    };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = 0,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges    = &pushConstant
    };
    result = vkCreatePipelineLayout(
        m_device,
        &pipelineLayoutInfo,
        VK_NULL_HANDLE,
        &m_pipelineLayout);
    if (result != VK_SUCCESS) {
        printf("Failed to create pipeline layout!\n");
        vkDestroyShaderModule(m_device, frag_shader_module, VK_NULL_HANDLE);
        vkDestroyShaderModule(m_device, vert_shader_module, VK_NULL_HANDLE);
        return false;
    }

    VkGraphicsPipelineCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount = sizeof(shaderStages) / (sizeof(*shaderStages));
    info.pStages = shaderStages;
    info.pVertexInputState = &vertexInputInfo;
    info.pInputAssemblyState = &inputAssemblyInfo;
    info.pViewportState = &viewportInfo;
    info.pRasterizationState = &rasterizerInfo;
    info.pMultisampleState = &multisamplingInfo;
    info.pColorBlendState = &colorBlendingInfo;
    info.pDynamicState = VK_NULL_HANDLE;
    info.layout = m_pipelineLayout;
    info.renderPass = m_renderPass;
    info.subpass = 0;
    info.basePipelineHandle = VK_NULL_HANDLE;

    result = vkCreateGraphicsPipelines(
        m_device,
        VK_NULL_HANDLE,
        1,
        &info,
        VK_NULL_HANDLE,
        &m_pipeline);

    if (result != VK_SUCCESS) {
        printf("Failed to create graphics pipeline! result = %d\n", result);
    }

    vkDestroyShaderModule(m_device, frag_shader_module, VK_NULL_HANDLE);
    vkDestroyShaderModule(m_device, vert_shader_module, VK_NULL_HANDLE);

    return result == VK_SUCCESS;
}

void VulkanContext::drawRectangle() const
{
    vkCmdBindPipeline(m_cmdBufferDraw, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    vkCmdPushConstants(
        m_cmdBufferDraw,
        m_pipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT,
        0,
        sizeof(Position),
        &delta);
    vkCmdDraw(m_cmdBufferDraw, 6, 1, 0, 0);
}

bool VulkanContext::prepareShaderModule(const uint32_t *shaderBinary, uint32_t shaderSize,
                                        VkShaderModule *pShaderModule) const
{
    VkShaderModuleCreateInfo shaderInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = shaderSize,
        .pCode = shaderBinary
    };

    VkResult result = vkCreateShaderModule(m_device, &shaderInfo, VK_NULL_HANDLE, pShaderModule);
    if (result != VK_SUCCESS) {
        printf("Failed to create shader module \n");
        return false;
    }

    return true;
}