#include "vulkan_context.hpp"
#ifndef _WIN32
#include <X11/Xlib.h>
#endif

#include <cstdio>
#include <cstring>
#include <type_traits>

#include "shaders/rectangle_frag.spv.h"
#include "shaders/rectangle_vert.spv.h"

#include <fmt/format.h>

// Rendering model (Vulkan 1.4)
//
// Two Vulkan 1.0 objects this demo used to create are gone. The comments at
// the call sites below spell out what replaced each one.
//
// 1. Render pass + framebuffer  vs  dynamic rendering
//    A VkRenderPass baked attachment formats, load/store ops, and the
//    initialLayout -> finalLayout transition. A VkFramebuffer then tied that
//    pass to concrete image views, and drawing started with
//    vkCmdBeginRenderPass. VK_KHR_dynamic_rendering was promoted in Vulkan 1.3
//    and is still the core path in 1.4: vkCmdBeginRendering describes the
//    attachment in the command buffer. There is no render pass and no
//    framebuffer. Dynamic rendering also does not transition layouts, so the
//    old initialLayout/finalLayout pair is now an explicit image barrier on
//    either side of the rendering scope.
//
// 2. Graphics pipeline  vs  shader objects
//    vkCreateGraphicsPipelines linked the SPIR-V, the fixed-function state, and
//    a compatible render pass into one VkPipeline, bound with vkCmdBindPipeline.
//    Any change of state meant another pipeline. VK_EXT_shader_object (still an
//    extension in 1.4, not core) compiles each stage into a VkShaderEXT and
//    binds it with vkCmdBindShadersEXT. Nothing here calls
//    vkCreateGraphicsPipelines. State that used to be baked is set with
//    vkCmdSet* before the draw. A VkPipelineLayout is still created: shader
//    objects replace the graphics pipeline, not the layout push constants are
//    addressed through.

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

namespace
{
    struct Position
    {
        float x;
    };
}

static Position delta = {0.0f};


// Function declaration
//

static auto format_as(VkResult result) { return fmt::underlying(result); }
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
    // VK_KHR_get_physical_device_properties2 is core since Vulkan 1.1, so it is
    // not listed. The instance requests 1.4 below.
#ifdef _WIN32
    m_enabledInstanceExt = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        "VK_KHR_win32_surface",
    };
#else
    m_enabledInstanceExt = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
        VK_KHR_WAYLAND_SURFACE,

    #if USE_DIRECT_DISPLAY
        VK_KHR_DISPLAY_EXTENSION_NAME,
        VK_EXT_DIRECT_MODE_DISPLAY_EXTENSION_NAME,
        VK_EXT_ACQUIRE_XLIB_DISPLAY_EXTENSION_NAME,
    #endif
    };
#endif

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

    // Previously: createRenderPass(), createGraphicsPipeline(), createFramebuffers().
    // Dynamic rendering has no pass or framebuffer, and shader objects replace
    // the graphics pipeline. The swapchain image views are consumed directly
    // by vkCmdBeginRendering.
    if (!createShaderObjects()) {
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

void VulkanContext::Draw() const
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
        // A render pass transitioned the swapchain image as part of
        // vkCmdBeginRenderPass (initialLayout UNDEFINED, finalLayout
        // PRESENT_SRC). Dynamic rendering does not, so the same pair of
        // transitions is recorded as synchronization2 barriers. UNDEFINED as
        // the old layout discards whatever present left behind; the load op
        // below clears, so the previous contents are not needed.
        transitionColorImage(
            m_swapchainImages[swapchainImageIndex],
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        // Replaces VkRenderPassBeginInfo + vkCmdBeginRenderPass. The image
        // view, layout, and clear are named here instead of being looked up
        // from a framebuffer compatible with a prebuilt render pass.
        VkRenderingAttachmentInfo colorAttachment = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = m_colorImageViews[swapchainImageIndex],
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = clearValue,
        };

        VkRenderingInfo renderingInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {
                .extent = m_swapchainExtent,
            },
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachment,
        };

        vkCmdBeginRendering(m_cmdBufferDraw, &renderingInfo);

        // The graphics pipeline used to bind shaders and fixed-function state
        // together. Those are now separate: state first, then the shader objects.
        setShaderObjectDynamicState();
        drawRectangle();

        vkCmdEndRendering(m_cmdBufferDraw);

        transitionColorImage(
            m_swapchainImages[swapchainImageIndex],
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
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
        // vkDestroyPipeline / vkDestroyRenderPass / vkDestroyFramebuffer are
        // gone with the objects they destroyed. Shader objects are destroyed
        // individually; the layout outlives them only because push constants
        // still name it. The proc is null if device creation never finished.
        if (m_fns.vkDestroyShaderEXT != nullptr) {
            m_fns.vkDestroyShaderEXT(m_device, m_vertexShader, VK_NULL_HANDLE);
            m_fns.vkDestroyShaderEXT(m_device, m_fragmentShader, VK_NULL_HANDLE);
        }
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, VK_NULL_HANDLE);
        vkDestroyCommandPool(m_device, m_commandPool, VK_NULL_HANDLE);
        vkDestroySwapchainKHR(m_device, m_swapchain, VK_NULL_HANDLE);

        for (VkImageView view : m_colorImageViews) {
            vkDestroyImageView(m_device, view, VK_NULL_HANDLE);
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

#ifdef _WIN32
    {
        uint32_t layerCount = 0;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> availableLayers(layerCount);
        if (layerCount != 0) {
            vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
        }

        std::vector<const char*> presentLayers;
        for (const char* wanted : m_enabledLayers) {
            for (const auto& layer : availableLayers) {
                if (std::strcmp(layer.layerName, wanted) == 0) {
                    presentLayers.push_back(wanted);
                    break;
                }
            }
        }
        m_enabledLayers = std::move(presentLayers);

#if VULKAN_DEBUG
        uint32_t extensionCount = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        if (extensionCount != 0) {
            vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, availableExtensions.data());
        }
        for (const auto& extension : availableExtensions) {
            if (std::strcmp(extension.extensionName, VK_EXT_DEBUG_REPORT_EXTENSION_NAME) == 0) {
                m_enabledInstanceExt.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
                break;
            }
        }
#endif
    }
#endif

    VkApplicationInfo appInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = VK_NULL_HANDLE,
        .pApplicationName = APP_NAME,
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "vvv",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        // Latest core version. Dynamic rendering and synchronization2 have been
        // core since 1.3; 1.4 is what this demo is written against. Shader
        // objects are not part of that core version, so the device path enables
        // VK_EXT_shader_object separately.
        .apiVersion = VK_API_VERSION_1_4,
    };

    VkInstanceCreateInfo instanceInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = VK_NULL_HANDLE,
        .pApplicationInfo = &appInfo,
#ifdef _WIN32
        .enabledLayerCount = static_cast<uint32_t>(m_enabledLayers.size()),
        .ppEnabledLayerNames = m_enabledLayers.empty() ? nullptr : m_enabledLayers.data(),
#elif VULKAN_DEBUG
        .ppEnabledLayerNames = m_enabledLayers.data(),
#endif
        .enabledExtensionCount = static_cast<uint32_t>(m_enabledInstanceExt.size()),
        .ppEnabledExtensionNames = m_enabledInstanceExt.data(),
    };

    VkResult result = vkCreateInstance(&instanceInfo, VK_NULL_HANDLE, &m_instance);
    if (result == VK_ERROR_INCOMPATIBLE_DRIVER) {
        // Dynamic rendering is core in 1.3. Shader objects are an extension on
        // both 1.3 and 1.4, so a 1.3 loader can still run this path.
        fmt::print("Vulkan 1.4 instance is unavailable, retrying with Vulkan 1.3.\n");
        appInfo.apiVersion = VK_API_VERSION_1_3;
        m_instance = VK_NULL_HANDLE;
        result = vkCreateInstance(&instanceInfo, VK_NULL_HANDLE, &m_instance);
    }
    if (result != VK_SUCCESS) {
        fmt::print("Failed to create Vulkan instance. Result = {}\n", result);
        return false;
    }

#if VULKAN_DEBUG
#ifdef _WIN32
    auto vkGetInstanceProcAddrFn = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
    SDL2_vkCreateDebugReportCallbackEXT = vkGetInstanceProcAddrFn
        ? reinterpret_cast<PFN_vkCreateDebugReportCallbackEXT>(
              vkGetInstanceProcAddrFn(m_instance, "vkCreateDebugReportCallbackEXT"))
        : nullptr;
    if (SDL2_vkCreateDebugReportCallbackEXT == nullptr) {
        fmt::print("Debug report callback is unavailable; continuing without it.\n");
    } else {
        VkDebugReportCallbackCreateInfoEXT debugCallbackCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT,
            .flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT,
            .pfnCallback = vulkanDebugCallback,
        };

        result = SDL2_vkCreateDebugReportCallbackEXT(m_instance, &debugCallbackCreateInfo, VK_NULL_HANDLE, &m_debugCallbackEXT);
        if (result != VK_SUCCESS) {
            fmt::print("Failed to create debug callback.\n");
        }
    }
#else
    SDL2_vkCreateDebugReportCallbackEXT = reinterpret_cast<PFN_vkCreateDebugReportCallbackEXT>(SDL_Vulkan_GetVkGetInstanceProcAddr());

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
#endif

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, VK_NULL_HANDLE);
    if (deviceCount == 0) {
        return false;
    }

    std::vector<VkPhysicalDevice> deviceList(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, deviceList.data());

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

    const uint32_t deviceApi = m_physicalDeviceProperties.apiVersion;
    fmt::print("Using {} (Vulkan {}.{}.{})\n",
        m_physicalDeviceProperties.deviceName,
        VK_VERSION_MAJOR(deviceApi),
        VK_VERSION_MINOR(deviceApi),
        VK_VERSION_PATCH(deviceApi));

    // Core dynamic rendering needs Vulkan 1.3. The instance asked for 1.4, but
    // a 1.4 instance can still enumerate an older device.
    if (deviceApi < VK_API_VERSION_1_3) {
        fmt::print("Vulkan 1.3 or newer is required for core dynamic rendering.\n");
        return false;
    }

    if (m_queueFamilyProperties.empty() ||
        (m_queueFamilyProperties[0].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) {
        fmt::print("Queue family 0 does not support graphics.\n");
        return false;
    }

    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    if (extensionCount != 0) {
        vkEnumerateDeviceExtensionProperties(
            m_physicalDevice, nullptr, &extensionCount, availableExtensions.data());
    }

    auto hasExtension = [&](const char *name) {
        for (const auto &extension : availableExtensions) {
            if (std::strcmp(extension.extensionName, name) == 0) {
                return true;
            }
        }
        return false;
    };

    // These were not promoted with Vulkan 1.3/1.4. Dynamic rendering itself
    // was, so VK_KHR_dynamic_rendering is not enabled as an extension.
    // extendedDynamicState and extendedDynamicState2 are core commands in 1.3;
    // their feature bits are still enabled below, without listing the promoted
    // extension names.
    const char *requiredExtensions[] = {
        VK_EXT_SHADER_OBJECT_EXTENSION_NAME,
        VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME,
        VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME,
    };
    for (const char *name : requiredExtensions) {
        if (!hasExtension(name)) {
            fmt::print("Required device extension {} is not supported.\n", name);
            return false;
        }
        m_enabledDeviceExt.push_back(name);
    }

    VkPhysicalDeviceExtendedDynamicState3FeaturesEXT queriedDynamicState3 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT,
    };
    VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT queriedVertexInput = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT,
        .pNext = &queriedDynamicState3,
    };
    VkPhysicalDeviceExtendedDynamicState2FeaturesEXT queriedDynamicState2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT,
        .pNext = &queriedVertexInput,
    };
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT queriedDynamicState = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT,
        .pNext = &queriedDynamicState2,
    };
    VkPhysicalDeviceShaderObjectFeaturesEXT queriedShaderObject = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT,
        .pNext = &queriedDynamicState,
    };
    VkPhysicalDeviceVulkan13Features queriedVulkan13 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &queriedShaderObject,
    };
    VkPhysicalDeviceFeatures2 queriedFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &queriedVulkan13,
    };
    vkGetPhysicalDeviceFeatures2(m_physicalDevice, &queriedFeatures);

    if (!queriedVulkan13.dynamicRendering || !queriedVulkan13.synchronization2) {
        fmt::print("dynamicRendering and synchronization2 are required.\n");
        return false;
    }
    if (!queriedShaderObject.shaderObject || !queriedVertexInput.vertexInputDynamicState ||
        !queriedDynamicState.extendedDynamicState || !queriedDynamicState2.extendedDynamicState2) {
        fmt::print("shaderObject and dynamic-state features are required.\n");
        return false;
    }
    if (!queriedDynamicState3.extendedDynamicState3PolygonMode ||
        !queriedDynamicState3.extendedDynamicState3RasterizationSamples ||
        !queriedDynamicState3.extendedDynamicState3SampleMask ||
        !queriedDynamicState3.extendedDynamicState3AlphaToCoverageEnable ||
        !queriedDynamicState3.extendedDynamicState3AlphaToOneEnable ||
        !queriedDynamicState3.extendedDynamicState3LogicOpEnable ||
        !queriedDynamicState3.extendedDynamicState3ColorBlendEnable ||
        !queriedDynamicState3.extendedDynamicState3ColorBlendEquation ||
        !queriedDynamicState3.extendedDynamicState3ColorWriteMask) {
        fmt::print("extendedDynamicState3 features required by shader objects are missing.\n");
        return false;
    }

    // Enable only the bits this demo sets dynamically. Copying the query
    // structs into vkCreateDevice would also turn on geometryShader and
    // tessellationShader when the device supports them, and those stages
    // would then have to be unbound before every draw.
    VkPhysicalDeviceExtendedDynamicState3FeaturesEXT dynamicState3 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT,
        .extendedDynamicState3PolygonMode = VK_TRUE,
        .extendedDynamicState3RasterizationSamples = VK_TRUE,
        .extendedDynamicState3SampleMask = VK_TRUE,
        .extendedDynamicState3AlphaToCoverageEnable = VK_TRUE,
        .extendedDynamicState3AlphaToOneEnable = VK_TRUE,
        .extendedDynamicState3LogicOpEnable = VK_TRUE,
        .extendedDynamicState3ColorBlendEnable = VK_TRUE,
        .extendedDynamicState3ColorBlendEquation = VK_TRUE,
        .extendedDynamicState3ColorWriteMask = VK_TRUE,
    };
    VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT vertexInput = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT,
        .pNext = &dynamicState3,
        .vertexInputDynamicState = VK_TRUE,
    };
    VkPhysicalDeviceExtendedDynamicState2FeaturesEXT dynamicState2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT,
        .pNext = &vertexInput,
        .extendedDynamicState2 = VK_TRUE,
    };
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT dynamicState = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT,
        .pNext = &dynamicState2,
        .extendedDynamicState = VK_TRUE,
    };
    VkPhysicalDeviceShaderObjectFeaturesEXT shaderObject = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT,
        .pNext = &dynamicState,
        .shaderObject = VK_TRUE,
    };
    VkPhysicalDeviceVulkan13Features vulkan13 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &shaderObject,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE,
    };
    // Present on a 1.4 device so creation matches the instance version. No 1.4
    // feature is required: dynamic rendering has been core since 1.3, and
    // shader objects are still VK_EXT_shader_object.
    VkPhysicalDeviceVulkan14Features vulkan14 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
        .pNext = &vulkan13,
    };
    const bool deviceIsVulkan14 = deviceApi >= VK_API_VERSION_1_4;

    VkPhysicalDeviceFeatures2 features2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = deviceIsVulkan14 ? static_cast<void *>(&vulkan14)
                                  : static_cast<void *>(&vulkan13),
    };

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
        .pNext = &features2,
        .flags = 0,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueInfo,
#if VULKAN_DEBUG
        .enabledLayerCount =  static_cast<uint32_t>(m_enabledLayers.size()),
        .ppEnabledLayerNames = m_enabledLayers.data(),
#endif
        // pEnabledFeatures stays null: the features live in the pNext chain.
        // Setting both is invalid.
        .enabledExtensionCount = static_cast<uint32_t>(m_enabledDeviceExt.size()),
        .ppEnabledExtensionNames = m_enabledDeviceExt.data(),
    };

    VkResult result = vkCreateDevice(m_physicalDevice, &deviceInfo, VK_NULL_HANDLE, &m_device);
    if (result != VK_SUCCESS) {
        fmt::print("Failed to create logical device. Result = {}\n", result);
        return false;
    }

    vkGetDeviceQueue(m_device, 0, 0, &m_presentQueue);
    if (!loadShaderObjectFunctions()) {
        vkDestroyDevice(m_device, VK_NULL_HANDLE);
        m_device = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

bool VulkanContext::loadShaderObjectFunctions()
{
    // The loader's export table has core commands (vkCmdBeginRendering,
    // vkCmdSetCullMode, vkCmdPipelineBarrier2, ...) and WSI. It does not export
    // VK_EXT_shader_object or VK_EXT_extended_dynamic_state3, so calling
    // vkCmdSetAlphaToOneEnableEXT directly is an undefined reference. Those
    // entry points exist only once a device that enabled the extension exists.
    auto load = [this](auto &fn, const char *name) -> bool {
        fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(
            vkGetDeviceProcAddr(m_device, name));
        if (fn == nullptr) {
            fmt::print("Failed to load {}\n", name);
            return false;
        }
        return true;
    };

    return load(m_fns.vkCreateShadersEXT, "vkCreateShadersEXT")
        && load(m_fns.vkDestroyShaderEXT, "vkDestroyShaderEXT")
        && load(m_fns.vkCmdBindShadersEXT, "vkCmdBindShadersEXT")
        && load(m_fns.vkCmdSetVertexInputEXT, "vkCmdSetVertexInputEXT")
        && load(m_fns.vkCmdSetPolygonModeEXT, "vkCmdSetPolygonModeEXT")
        && load(m_fns.vkCmdSetRasterizationSamplesEXT, "vkCmdSetRasterizationSamplesEXT")
        && load(m_fns.vkCmdSetSampleMaskEXT, "vkCmdSetSampleMaskEXT")
        && load(m_fns.vkCmdSetAlphaToCoverageEnableEXT, "vkCmdSetAlphaToCoverageEnableEXT")
        && load(m_fns.vkCmdSetAlphaToOneEnableEXT, "vkCmdSetAlphaToOneEnableEXT")
        && load(m_fns.vkCmdSetLogicOpEnableEXT, "vkCmdSetLogicOpEnableEXT")
        && load(m_fns.vkCmdSetColorBlendEnableEXT, "vkCmdSetColorBlendEnableEXT")
        && load(m_fns.vkCmdSetColorBlendEquationEXT, "vkCmdSetColorBlendEquationEXT")
        && load(m_fns.vkCmdSetColorWriteMaskEXT, "vkCmdSetColorWriteMaskEXT");
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
            fmt::print("X display opened {} \n", fmt::ptr(dpy));
        }

        uint32_t displayCount;
        vkGetPhysicalDeviceDisplayPropertiesKHR(m_physicalDevice, &displayCount, VK_NULL_HANDLE);

        VkDisplayPropertiesKHR displayProperties[displayCount];
        vkGetPhysicalDeviceDisplayPropertiesKHR(m_physicalDevice, &displayCount, displayProperties);

        fmt::print("Found displays = {}\n", displayCount);
        for (int i=0; i < displayCount; i++) {
            fmt::print("\t[{}] {}\n", i, displayProperties[i].displayName);
        }

        VkDisplayKHR selectedDisplay = displayProperties[0].display;
        VkResult result = pfn_vkAcquireXlibDisplayEXT(m_physicalDevice, dpy, selectedDisplay);
        if (result != VK_SUCCESS) {
            fmt::print("Failed to acquire display result = {}\n", result);
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
            fmt::print("swapchainExtent.width = {}, swapchainExtent.height = {}\n",
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
            fmt::print("Failed to create display plane surface result = {}\n", result);
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
        fmt::print("Failed to get surface capabilites = {}\n", result);
        return false;
    }

    uint32_t surfaceFormatsCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &surfaceFormatsCount, VK_NULL_HANDLE);

    int surfaceFormatIndex = 0;
    std::vector<VkSurfaceFormatKHR> surfaceFormats(surfaceFormatsCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &surfaceFormatsCount, surfaceFormats.data());

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

#ifdef _WIN32
    /* Win32 reports the drawable size in currentExtent, including DPI scale.
       0xFFFFFFFF means the app must choose an extent inside min/max. */
    if (m_surfaceCapabilities.currentExtent.width != 0xFFFFFFFFu &&
        m_surfaceCapabilities.currentExtent.width != 0 &&
        m_surfaceCapabilities.currentExtent.height != 0) {
        m_swapchainExtent = m_surfaceCapabilities.currentExtent;
    } else {
        int pixelW = m_outWidth;
        int pixelH = m_outHeight;
        SDL_GetWindowSizeInPixels(m_sdlWindow, &pixelW, &pixelH);

        const uint32_t minW = m_surfaceCapabilities.minImageExtent.width;
        const uint32_t maxW = m_surfaceCapabilities.maxImageExtent.width;
        const uint32_t minH = m_surfaceCapabilities.minImageExtent.height;
        const uint32_t maxH = m_surfaceCapabilities.maxImageExtent.height;
        uint32_t width = pixelW > 0 ? static_cast<uint32_t>(pixelW) : minW;
        uint32_t height = pixelH > 0 ? static_cast<uint32_t>(pixelH) : minH;
        if (width < minW) width = minW;
        if (width > maxW) width = maxW;
        if (height < minH) height = minH;
        if (height > maxH) height = maxH;
        m_swapchainExtent.width = width;
        m_swapchainExtent.height = height;
    }

    VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    const VkCompositeAlphaFlagBitsKHR alphaCandidates[] = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
    };
    for (VkCompositeAlphaFlagBitsKHR candidate : alphaCandidates) {
        if (m_surfaceCapabilities.supportedCompositeAlpha & candidate) {
            compositeAlpha = candidate;
            break;
        }
    }
#endif

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
#ifdef _WIN32
        .compositeAlpha = compositeAlpha,
#else
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
#endif
        .presentMode = VK_PRESENT_MODE_FIFO_KHR, // V-SYNC -> expose to enable/disable
        .clipped = VK_TRUE
}   ;

    result = vkCreateSwapchainKHR(m_device, &swapchainInfo, VK_NULL_HANDLE, &m_swapchain);
    if (result != VK_SUCCESS) {
        fmt::print("Failed to create swapchain result = {}\n", result);
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
                fmt::print("Failed to create image view for image index: {}\n", i);
                return false;
            }
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

    VkSemaphoreCreateInfo semaphoreCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
    };

    VkResult result = vkCreateSemaphore(m_device, &semaphoreCreateInfo, VK_NULL_HANDLE, &m_presentSemaphore);
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

bool VulkanContext::createShaderObjects()
{
    fmt::print("{} called\n", __func__);

    // Same push-constant range the graphics pipeline layout used to expose.
    // It is now also written into each VkShaderCreateInfoEXT. vkCmdPushConstants
    // still takes a VkPipelineLayout; that layout must be compatible with the
    // ranges the shader objects were created with. There is no graphics pipeline
    // behind it.
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
    VkResult result = vkCreatePipelineLayout(
        m_device,
        &pipelineLayoutInfo,
        VK_NULL_HANDLE,
        &m_pipelineLayout);
    if (result != VK_SUCCESS) {
        fmt::print("Failed to create pipeline layout. Result = {}\n", result);
        return false;
    }

    // Difference from vkCreateGraphicsPipelines:
    // - SPIR-V is passed directly. There is no VkShaderModule intermediate.
    // - No rasterizer, viewport, blend, or render-pass state is attached.
    //   That state is set per draw in setShaderObjectDynamicState().
    // - LINK_STAGE promises these two stages are always used together, which
    //   is the cross-stage optimization a graphics pipeline used to get for
    //   free. Unlinked shader objects (flags = 0) can instead be mixed across
    //   draws without building a new pipeline for each combination.
    VkShaderCreateInfoEXT shaderInfos[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT,
            .flags = VK_SHADER_CREATE_LINK_STAGE_BIT_EXT,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .nextStage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT,
            .codeSize = sizeof(rectangle_vert_spv),
            .pCode = rectangle_vert_spv,
            .pName = "main",
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &pushConstant,
        },
        {
            .sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT,
            .flags = VK_SHADER_CREATE_LINK_STAGE_BIT_EXT,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .nextStage = 0,
            .codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT,
            .codeSize = sizeof(rectangle_frag_spv),
            .pCode = rectangle_frag_spv,
            .pName = "main",
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &pushConstant,
        },
    };

    VkShaderEXT shaders[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    result = m_fns.vkCreateShadersEXT(m_device, 2, shaderInfos, VK_NULL_HANDLE, shaders);
    m_vertexShader = shaders[0];
    m_fragmentShader = shaders[1];
    if (result != VK_SUCCESS) {
        fmt::print("Failed to create shader objects. Result = {}\n", result);
        m_fns.vkDestroyShaderEXT(m_device, m_vertexShader, VK_NULL_HANDLE);
        m_fns.vkDestroyShaderEXT(m_device, m_fragmentShader, VK_NULL_HANDLE);
        m_vertexShader = VK_NULL_HANDLE;
        m_fragmentShader = VK_NULL_HANDLE;
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, VK_NULL_HANDLE);
        m_pipelineLayout = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

void VulkanContext::setShaderObjectDynamicState() const
{
    // A graphics pipeline froze this in VkGraphicsPipelineCreateInfo (viewport
    // and scissor were not even dynamic). Shader objects have no baked state.
    // Before vkCmdDraw the spec requires each of these to have been set, even
    // when the value matches what the old pipeline would have baked and even
    // when this draw does not use that test (depth, stencil, logic op).
    const VkViewport viewport = {
        .width = static_cast<float>(m_swapchainExtent.width),
        .height = static_cast<float>(m_swapchainExtent.height),
        .maxDepth = 1.0f,
    };
    const VkRect2D scissor = {
        .extent = m_swapchainExtent,
    };
    vkCmdSetViewportWithCount(m_cmdBufferDraw, 1, &viewport);
    vkCmdSetScissorWithCount(m_cmdBufferDraw, 1, &scissor);

    // No vertex buffers: the rectangle is generated from gl_VertexIndex.
    // The old pipeline encoded that as empty vertex-input state. Here it is
    // vkCmdSetVertexInputEXT with zero bindings, called every draw.
    m_fns.vkCmdSetVertexInputEXT(m_cmdBufferDraw, 0, nullptr, 0, nullptr);

    vkCmdSetPrimitiveTopology(m_cmdBufferDraw, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    vkCmdSetPrimitiveRestartEnable(m_cmdBufferDraw, VK_FALSE);
    vkCmdSetRasterizerDiscardEnable(m_cmdBufferDraw, VK_FALSE);
    m_fns.vkCmdSetPolygonModeEXT(m_cmdBufferDraw, VK_POLYGON_MODE_FILL);
    vkCmdSetCullMode(m_cmdBufferDraw, VK_CULL_MODE_NONE);
    vkCmdSetFrontFace(m_cmdBufferDraw, VK_FRONT_FACE_CLOCKWISE);
    vkCmdSetDepthBiasEnable(m_cmdBufferDraw, VK_FALSE);
    vkCmdSetDepthBias(m_cmdBufferDraw, 0.0f, 0.0f, 0.0f);
    vkCmdSetLineWidth(m_cmdBufferDraw, 1.0f);

    vkCmdSetDepthTestEnable(m_cmdBufferDraw, VK_FALSE);
    vkCmdSetDepthWriteEnable(m_cmdBufferDraw, VK_FALSE);
    vkCmdSetDepthCompareOp(m_cmdBufferDraw, VK_COMPARE_OP_ALWAYS);
    vkCmdSetDepthBoundsTestEnable(m_cmdBufferDraw, VK_FALSE);
    vkCmdSetDepthBounds(m_cmdBufferDraw, 0.0f, 1.0f);
    vkCmdSetStencilTestEnable(m_cmdBufferDraw, VK_FALSE);
    vkCmdSetStencilOp(m_cmdBufferDraw, VK_STENCIL_FACE_FRONT_AND_BACK,
                      VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP,
                      VK_COMPARE_OP_ALWAYS);
    vkCmdSetStencilCompareMask(m_cmdBufferDraw, VK_STENCIL_FACE_FRONT_AND_BACK, 0);
    vkCmdSetStencilWriteMask(m_cmdBufferDraw, VK_STENCIL_FACE_FRONT_AND_BACK, 0);
    vkCmdSetStencilReference(m_cmdBufferDraw, VK_STENCIL_FACE_FRONT_AND_BACK, 0);

    m_fns.vkCmdSetRasterizationSamplesEXT(m_cmdBufferDraw, VK_SAMPLE_COUNT_1_BIT);
    const VkSampleMask sampleMask = 0xFFFFFFFF;
    m_fns.vkCmdSetSampleMaskEXT(m_cmdBufferDraw, VK_SAMPLE_COUNT_1_BIT, &sampleMask);
    m_fns.vkCmdSetAlphaToCoverageEnableEXT(m_cmdBufferDraw, VK_FALSE);
    m_fns.vkCmdSetAlphaToOneEnableEXT(m_cmdBufferDraw, VK_FALSE);

    const VkBool32 blendEnable = VK_FALSE;
    const VkColorBlendEquationEXT blendEquation = {
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
    };
    const VkColorComponentFlags writeMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    const float blendConstants[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    m_fns.vkCmdSetLogicOpEnableEXT(m_cmdBufferDraw, VK_FALSE);
    m_fns.vkCmdSetColorBlendEnableEXT(m_cmdBufferDraw, 0, 1, &blendEnable);
    m_fns.vkCmdSetColorBlendEquationEXT(m_cmdBufferDraw, 0, 1, &blendEquation);
    m_fns.vkCmdSetColorWriteMaskEXT(m_cmdBufferDraw, 0, 1, &writeMask);
    vkCmdSetBlendConstants(m_cmdBufferDraw, blendConstants);
}

void VulkanContext::transitionColorImage(VkImage image,
                                         VkImageLayout oldLayout,
                                         VkImageLayout newLayout) const
{
    // synchronization2 (core since 1.3) replaces the old VkImageMemoryBarrier
    // that a render pass used to hide inside initialLayout/finalLayout.
    VkPipelineStageFlags2 srcStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkAccessFlags2 srcAccess = VK_ACCESS_2_NONE;
    VkPipelineStageFlags2 dstStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkAccessFlags2 dstAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;

    if (newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        srcAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        // Present is synchronized by the semaphore in vkQueuePresentKHR, not by
        // a destination access in this barrier. BOTTOM_OF_PIPE is the sync2
        // stand-in for the old render pass's finalLayout transition.
        dstStage = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        dstAccess = VK_ACCESS_2_NONE;
    }

    const VkImageMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = srcStage,
        .srcAccessMask = srcAccess,
        .dstStageMask = dstStage,
        .dstAccessMask = dstAccess,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    const VkDependencyInfo dependency = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    };
    vkCmdPipelineBarrier2(m_cmdBufferDraw, &dependency);
}

void VulkanContext::drawRectangle() const
{
    // vkCmdBindPipeline bound every stage and the baked state at once.
    // vkCmdBindShadersEXT binds stages only. Geometry and tessellation are
    // left unset on purpose: those features are not enabled, so the spec does
    // not require a NULL bind for them.
    const VkShaderStageFlagBits stages[2] = {
        VK_SHADER_STAGE_VERTEX_BIT,
        VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    const VkShaderEXT shaders[2] = {
        m_vertexShader,
        m_fragmentShader,
    };
    m_fns.vkCmdBindShadersEXT(m_cmdBufferDraw, 2, stages, shaders);

    vkCmdPushConstants(
        m_cmdBufferDraw,
        m_pipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT,
        0,
        sizeof(Position),
        &delta);
    vkCmdDraw(m_cmdBufferDraw, 6, 1, 0, 0);
}