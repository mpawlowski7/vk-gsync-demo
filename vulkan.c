#include "vulkan.h"
#include <X11/Xlib.h>

#include <stdio.h>

#include "rectangle_frag.spv.h"
#include "rectangle_vert.spv.h"

#define USE_DIRECT_DISPLAY 0
#define VULKAN_DEBUG 0

#define VK_KHR_XLIB_SURFACE_EXTENSION_NAME         "VK_KHR_xlib_surface"
#define VK_EXT_ACQUIRE_XLIB_DISPLAY_EXTENSION_NAME "VK_EXT_acquire_xlib_display"

#if USE_DIRECT_DISPLAY
typedef VkResult (VKAPI_PTR *PFN_vkAcquireXlibDisplayEXT)(VkPhysicalDevice physicalDevice, Display* dpy, VkDisplayKHR display);
#endif

// Global variables declaration
//
static VkInstance                        g_instance;
#if VULKAN_DEBUG
static VkDebugReportCallbackEXT          g_debugCallbackEXT;
#endif
static VkPhysicalDevice                  g_physicalDevice;
static VkPhysicalDeviceProperties        g_physicalDeviceProperties;
static VkPhysicalDeviceFeatures          g_physicalDeviceFeatures;
static VkPhysicalDeviceMemoryProperties  g_physicalDeviceMemoryProperties;
static VkQueueFamilyProperties          *g_queueFamilyProperties;
static VkDevice                          g_device;
static VkQueue                           g_presentQueue;
static VkCommandPool                     g_commandPool;
static VkCommandBuffer                   g_cmdBufferDraw;

static VkSurfaceKHR                      g_surface;
static VkSwapchainKHR                    g_swapchain;
static VkSurfaceCapabilitiesKHR          g_surfaceCapabilities;
static VkSurfaceFormatKHR                g_surfaceFormat;
static VkExtent2D                        g_swapchainExtent;
static VkImage                          *g_swapchainImages;
static VkImageView                      *g_colorImageViews;
static uint32_t                          g_swapchainImageCount;

static VkRenderPass                      g_renderPass;
static VkFramebuffer                    *g_framebuffers;
static VkFence                           g_renderFence;
static VkSemaphore                       g_renderSemaphore, g_presentSemaphore;

static VkPipelineLayout                  g_pipelineLayout;
static VkPipeline                        g_pipeline;

typedef struct Position_t {
  float x;
} Position;

static Position delta = { 0.0f };

// Config
//
#if VULKAN_DEBUG
static const char* g_enabledValidationLayers[] = {
  "VK_LAYER_KHRONOS_validation"
};
#endif

const char* g_requiredInstanceExtensions[] = {
  VK_KHR_SURFACE_EXTENSION_NAME,
  VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
#if USE_DIRECT_DISPLAY
  VK_KHR_DISPLAY_EXTENSION_NAME,
  VK_EXT_DIRECT_MODE_DISPLAY_EXTENSION_NAME,
  VK_EXT_ACQUIRE_XLIB_DISPLAY_EXTENSION_NAME,
#endif
  VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME
};

const char* g_requiredDeviceExtensions[] = {
  VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

// Function declaration
//
#if VULKAN_DEBUG
static PFN_vkCreateDebugReportCallbackEXT SDL2_vkCreateDebugReportCallbackEXT;
#endif

#if USE_DIRECT_DISPLAY
static PFN_vkAcquireXlibDisplayEXT pfn_vkAcquireXlibDisplayEXT = VK_NULL_HANDLE;
#endif


// ------ Helper functions -----
//
static SDL_bool prepareShaderModule(uint32_t* shaderBinary, int shaderSize, VkShaderModule *pShaderModule)
{
  VkShaderModuleCreateInfo shaderInfo = {};
  shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  shaderInfo.codeSize = shaderSize;
  shaderInfo.pCode = shaderBinary;

  VkResult result = vkCreateShaderModule(g_device, &shaderInfo, VK_NULL_HANDLE, pShaderModule);
  if (result != VK_SUCCESS) {
    printf("Failed to create shader module \n");
    return SDL_FALSE;
  }

  return SDL_TRUE;
}

static void beginFrame()
{

}

static void endFrame()
{

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
  printf("Debug cb called with msg:\n%s", pMessage);
  return VK_TRUE;
}
#endif

static SDL_bool initVulkanCore(SDL_Window *appWindow)
{
  printf("%s called\n", __func__);

  if (appWindow == NULL) {
    printf("App window not initialized.\n");
    return SDL_FALSE;
  }

  VkApplicationInfo appInfo = {};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pNext = VK_NULL_HANDLE;
  appInfo.pApplicationName = APP_NAME;
  appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.pEngineName = "vvv";
  appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.apiVersion = VK_API_VERSION_1_0;

  VkInstanceCreateInfo instanceInfo = {};
  instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.pNext = VK_NULL_HANDLE;
  instanceInfo.pApplicationInfo = &appInfo;
#if VULKAN_DEBUG
  instanceInfo.ppEnabledLayerNames = g_enabledValidationLayers;
#endif
  instanceInfo.enabledLayerCount = 0;
  instanceInfo.enabledExtensionCount = sizeof(g_requiredInstanceExtensions)/sizeof(g_requiredInstanceExtensions[0]);
  instanceInfo.ppEnabledExtensionNames = g_requiredInstanceExtensions;

  VkResult result = vkCreateInstance(&instanceInfo, VK_NULL_HANDLE, &g_instance);
  if (result != VK_SUCCESS) {
    printf("Failed to create Vulkan instance. Result = %d\n", result);
    return SDL_FALSE;
  }

#if VULKAN_DEBUG
  SDL2_vkCreateDebugReportCallbackEXT = (PFN_vkCreateDebugReportCallbackEXT) SDL_Vulkan_GetVkGetInstanceProcAddr();

  VkDebugReportCallbackCreateInfoEXT debugCallbackCreateInfo = {};
  debugCallbackCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
  debugCallbackCreateInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
  debugCallbackCreateInfo.pfnCallback = vulkanDebugCallback;

   result = SDL2_vkCreateDebugReportCallbackEXT(g_instance, &debugCallbackCreateInfo, VK_NULL_HANDLE, &g_debugCallbackEXT);
  if (result != VK_SUCCESS) {
    printf("Failed to create debug callback.\n");
    return SDL_FALSE;
  }
#endif

  uint32_t deviceCount = 0;
  vkEnumeratePhysicalDevices(g_instance, &deviceCount, VK_NULL_HANDLE);
  if (deviceCount == 0) {
    return SDL_FALSE;
  }

  VkPhysicalDevice deviceList[deviceCount];
  vkEnumeratePhysicalDevices(g_instance, &deviceCount, deviceList);

  g_physicalDevice = deviceList[0];

  vkGetPhysicalDeviceProperties(g_physicalDevice, &g_physicalDeviceProperties);
  vkGetPhysicalDeviceMemoryProperties(g_physicalDevice, &g_physicalDeviceMemoryProperties);
  vkGetPhysicalDeviceFeatures(g_physicalDevice, &g_physicalDeviceFeatures);

  uint32_t queueFamilyPropertyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(g_physicalDevice, &queueFamilyPropertyCount, VK_NULL_HANDLE);

  if (queueFamilyPropertyCount > 0) {
    g_queueFamilyProperties = calloc(queueFamilyPropertyCount, sizeof(*g_queueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(g_physicalDevice, &queueFamilyPropertyCount, g_queueFamilyProperties);
  } else {
    return SDL_FALSE;
  }

#if USE_DIRECT_DISPLAY
  pfn_vkAcquireXlibDisplayEXT =  (PFN_vkAcquireXlibDisplayEXT) vkGetInstanceProcAddr(g_instance, "vkAcquireXlibDisplayEXT");
  if (pfn_vkAcquireXlibDisplayEXT == VK_NULL_HANDLE) {
    printf("Failed to load vkAcquireXlibDisplayEXT\n");
    return SDL_FALSE;
  }
#endif
  return SDL_TRUE;
}

SDL_bool initLogicalDevice()
{
  printf("%s called\n", __func__);

  const char *deviceExtensions[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
  };

  VkDeviceQueueCreateInfo queueInfo = {};
  float priority = 0.0;
  queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queueInfo.flags = 0;
  queueInfo.queueFamilyIndex = 0;
  queueInfo.queueCount = 1;
  queueInfo.pQueuePriorities = &priority;

  VkDeviceCreateInfo deviceInfo = {};
  deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.flags = 0;
  deviceInfo.queueCreateInfoCount = 1;
  deviceInfo.pQueueCreateInfos = &queueInfo;
#if VULKAN_DEBUG
  deviceInfo.enabledLayerCount = sizeof(g_enabledValidationLayers) / sizeof(*g_enabledValidationLayers);
  deviceInfo.ppEnabledLayerNames = g_enabledValidationLayers;
#endif
  deviceInfo.enabledExtensionCount = sizeof(deviceExtensions) / sizeof(*deviceExtensions);
  deviceInfo.ppEnabledExtensionNames = deviceExtensions;

  VkResult result = vkCreateDevice(g_physicalDevice, &deviceInfo, VK_NULL_HANDLE, &g_device);
  if (result != VK_SUCCESS) {
    return SDL_FALSE;
  }

  vkGetDeviceQueue(g_device, 0, 0, &g_presentQueue);
  return SDL_TRUE;
}

SDL_bool initSwapchain(SDL_Window* pWindowHandle, int width, int height)
{
  printf("%s called\n", __func__);

  // Direct display surface
#if USE_DIRECT_DISPLAY
  {
    Display* dpy = XOpenDisplay(0);
    if (dpy == NULL) {
      printf("Failed to open X display\n");
      return 1;
    } else {
      printf("X display opened %p \n", dpy);
    }

    uint32_t displayCount;
    vkGetPhysicalDeviceDisplayPropertiesKHR(g_physicalDevice, &displayCount, VK_NULL_HANDLE);

    VkDisplayPropertiesKHR displayProperties[displayCount];
    vkGetPhysicalDeviceDisplayPropertiesKHR(g_physicalDevice, &displayCount, displayProperties);

    printf( "Found displays = %d\n", displayCount);
    for (int i=0; i < displayCount; i++) {
      printf("\t[%d] %s \n", i, displayProperties[i].displayName);
    }

    VkDisplayKHR selectedDisplay = displayProperties[0].display;
    VkResult result = pfn_vkAcquireXlibDisplayEXT(g_physicalDevice, dpy, selectedDisplay);
    if (result != VK_SUCCESS) {
      printf("Failed to acquire display result = %d \n", result);
      return SDL_FALSE;
    }

    uint32_t displayModesCount = 0;
    vkGetDisplayModePropertiesKHR(g_physicalDevice, selectedDisplay, &displayModesCount, VK_NULL_HANDLE);

    VkDisplayModePropertiesKHR displayModeProperites[displayModesCount];
    vkGetDisplayModePropertiesKHR(g_physicalDevice, selectedDisplay, &displayModesCount, displayModeProperites);

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
      g_swapchainExtent.width = selectedMode.parameters.visibleRegion.width;
      g_swapchainExtent.height = selectedMode.parameters.visibleRegion.height;
      printf("g_swapchainExtent.width = %d, g_swapchainExtent.height = %d\n",
        g_swapchainExtent.width, g_swapchainExtent.height);
    }

    uint32_t planePropertiesCount = 0;
    vkGetPhysicalDeviceDisplayPlanePropertiesKHR(g_physicalDevice, &planePropertiesCount, VK_NULL_HANDLE);

    VkDisplayPlanePropertiesKHR planeProperties[planePropertiesCount];
    vkGetPhysicalDeviceDisplayPlanePropertiesKHR(g_physicalDevice, &planePropertiesCount, planeProperties);

    uint32_t planeIndex;
    SDL_bool foundPlane = SDL_FALSE;
    for(uint32_t i = 0; i < planePropertiesCount; ++i) {
      VkDisplayPlanePropertiesKHR property = planeProperties[i];

      // skip planes bound to different display
      if(property.currentDisplay && (property.currentDisplay != selectedDisplay)) {
        continue;
      }

      uint32_t supportedDisplayCount = 0;
      vkGetDisplayPlaneSupportedDisplaysKHR(g_physicalDevice, i, &supportedDisplayCount, VK_NULL_HANDLE);

      VkDisplayKHR supportedDisplays[supportedDisplayCount];
      vkGetDisplayPlaneSupportedDisplaysKHR(g_physicalDevice, i, &supportedDisplayCount, supportedDisplays);
      for(int i = 0; i < supportedDisplayCount; i++) {
        if(supportedDisplays[i] == selectedDisplay) {
          foundPlane = SDL_TRUE;
          planeIndex = i;
          break;
        }
      }

      if(foundPlane) {
        break;
      }
    }

    if(!foundPlane) {
      printf("Could not find a compatible display plane!\n");
      return SDL_FALSE;
    }

    // find alpha mode bit
    VkDisplayPlaneCapabilitiesKHR planeCapabilites;
    vkGetDisplayPlaneCapabilitiesKHR(g_physicalDevice, selectedMode.displayMode, planeIndex, &planeCapabilites);

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
    displaySurfaceInfo.imageExtent = g_swapchainExtent;

    result = vkCreateDisplayPlaneSurfaceKHR(g_instance, &displaySurfaceInfo, VK_NULL_HANDLE, &g_surface);
    if (result != VK_SUCCESS) {
      printf("Failed to create display plane surface result = %d \n", result);
      return SDL_FALSE;
    }
  }
#else
  // SDL surface
  {
    SDL_Vulkan_CreateSurface(pWindowHandle, g_instance, &g_surface);

    g_swapchainExtent.width = width;
    g_swapchainExtent.height = height;
  }
#endif
   VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_physicalDevice, g_surface, &g_surfaceCapabilities);
  if (result != VK_SUCCESS) {
    printf("Failed to get surface capabilites = %d\n", result);
    return SDL_FALSE;
  }

  uint32_t surfaceFormatsCount;
  vkGetPhysicalDeviceSurfaceFormatsKHR(g_physicalDevice, g_surface, &surfaceFormatsCount, VK_NULL_HANDLE);

  int surfaceFormatIndex = 0;
  VkSurfaceFormatKHR surfaceFormats[surfaceFormatsCount];
  vkGetPhysicalDeviceSurfaceFormatsKHR(g_physicalDevice, g_surface, &surfaceFormatsCount, surfaceFormats);
  for (int i = 0; i < surfaceFormatsCount; i++) {
    if (surfaceFormats[i].format == VK_FORMAT_B8G8R8A8_UNORM) {
      surfaceFormatIndex = i;
    }
  }

  if (surfaceFormats[surfaceFormatIndex].format != VK_FORMAT_B8G8R8A8_UNORM) {
    printf("VK_FORMAT_B8G8R8A8_UNORM not supported\n");
    return SDL_FALSE;
  }

  g_surfaceFormat = surfaceFormats[surfaceFormatIndex];

  uint32_t imageCount = g_surfaceCapabilities.minImageCount + 1;
  if (g_surfaceCapabilities.maxImageCount > 0 && imageCount > g_surfaceCapabilities.maxImageCount) {
    imageCount = g_surfaceCapabilities.maxImageCount;
  }

  VkSwapchainCreateInfoKHR swapchainInfo = {};
  swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  swapchainInfo.surface = g_surface;
  swapchainInfo.minImageCount = imageCount;
  swapchainInfo.imageFormat = g_surfaceFormat.format;
  swapchainInfo.imageColorSpace = g_surfaceFormat.colorSpace;
  swapchainInfo.imageExtent = g_swapchainExtent;
  swapchainInfo.imageArrayLayers = 1;
  swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  swapchainInfo.preTransform = g_surfaceCapabilities.currentTransform;
  swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  swapchainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR; // V-SYNC -> expose to enable/disable
  swapchainInfo.clipped = VK_TRUE;

  result = vkCreateSwapchainKHR(g_device, &swapchainInfo, VK_NULL_HANDLE, &g_swapchain);
  if (result != VK_SUCCESS) {
    printf("Failed to create swapchain result = %d\n", result);
    return SDL_FALSE;
  }

  vkGetSwapchainImagesKHR(g_device, g_swapchain, &g_swapchainImageCount, VK_NULL_HANDLE);

  g_swapchainImages = calloc(g_swapchainImageCount, sizeof(*g_swapchainImages));
  if (g_swapchainImages == VK_NULL_HANDLE) {
    printf("Failed to allocate swapchain images\n");
    return SDL_FALSE;
  }
  vkGetSwapchainImagesKHR(g_device, g_swapchain, &g_swapchainImageCount, g_swapchainImages);

  // Create ImageViews
  {
    g_colorImageViews = calloc(g_swapchainImageCount, sizeof(*g_colorImageViews));
    if (g_colorImageViews == VK_NULL_HANDLE) {
      printf("Failed to allocate color image views\n");
      return SDL_FALSE;
    }

    for (int i = 0; i < g_swapchainImageCount; i++) {
      VkImageViewCreateInfo colorInfo = {};
      colorInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      colorInfo.format = g_surfaceFormat.format;
      colorInfo.components.r = VK_COMPONENT_SWIZZLE_R;
      colorInfo.components.g = VK_COMPONENT_SWIZZLE_G;
      colorInfo.components.b = VK_COMPONENT_SWIZZLE_B;
      colorInfo.components.a = VK_COMPONENT_SWIZZLE_A;
      colorInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      colorInfo.subresourceRange.baseMipLevel = 0;
      colorInfo.subresourceRange.levelCount = 1;
      colorInfo.subresourceRange.baseArrayLayer = 0;
      colorInfo.subresourceRange.layerCount = 1;
      colorInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
      colorInfo.flags = 0;
      colorInfo.image = g_swapchainImages[i];

      result = vkCreateImageView(g_device, &colorInfo, VK_NULL_HANDLE, &g_colorImageViews[i]);
      if (result != VK_SUCCESS) {
        printf("Failed to create image view for image index: %d \n", i);
        return SDL_FALSE;
      }
    }
  }

  return SDL_TRUE;
}

SDL_bool createRenderPass()
{
  printf("%s called\n", __func__);

  // the renderpass will use this color attachment.
  VkAttachmentDescription colorAttachment = {};
  colorAttachment.format = g_surfaceFormat.format;
  colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference colorAttachmentReference = {};
  colorAttachmentReference.attachment = 0;
  colorAttachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpassDescription = {};
  subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpassDescription.colorAttachmentCount = 1;
  subpassDescription.pColorAttachments = &colorAttachmentReference;

  VkRenderPassCreateInfo renderPassInfo = {};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassInfo.attachmentCount = 1;
  renderPassInfo.pAttachments = &colorAttachment;
  renderPassInfo.subpassCount = 1;
  renderPassInfo.pSubpasses = &subpassDescription;

  VkResult result = vkCreateRenderPass(g_device, &renderPassInfo, VK_NULL_HANDLE, &g_renderPass);
  if ( result != VK_SUCCESS ) {
    printf("Failed to create rectangle renderpass\n");
    return SDL_FALSE;
  }

  return SDL_TRUE;
}

SDL_bool createFramebuffers()
{
  printf("%s called\n", __func__);

  VkResult result;

  VkFramebufferCreateInfo framebufferInfo = {};
  framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  framebufferInfo.renderPass = g_renderPass;
  framebufferInfo.attachmentCount = 1;
  framebufferInfo.width = g_swapchainExtent.width;
  framebufferInfo.height = g_swapchainExtent.height;
  framebufferInfo.layers = 1;

  g_framebuffers = calloc(g_swapchainImageCount, sizeof(*g_framebuffers));
  for (int i = 0; i < g_swapchainImageCount; i++) {
    framebufferInfo.pAttachments = &g_colorImageViews[i];
    result = vkCreateFramebuffer(g_device, &framebufferInfo, VK_NULL_HANDLE, &g_framebuffers[i]);
    if (result != VK_SUCCESS) {
      printf("Failed to create framebuffer\n");
      return SDL_FALSE;
    }
  }

  return SDL_TRUE;
}

SDL_bool createCommandBuffers()
{
  printf("%s called\n", __func__);

  VkCommandPoolCreateInfo commandPoolInfo = {};
  commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  commandPoolInfo.queueFamilyIndex = 0;
  commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

  VkResult result = vkCreateCommandPool(g_device, &commandPoolInfo, VK_NULL_HANDLE, &g_commandPool);
  if (result != VK_SUCCESS) {
    printf("Failed to create command pool\n");
    return SDL_FALSE;
  }

  VkCommandBufferAllocateInfo commandBufferAllocateInfo = {};
  commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  commandBufferAllocateInfo.commandPool = g_commandPool;
  commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  commandBufferAllocateInfo.commandBufferCount = 1;

  result = vkAllocateCommandBuffers(g_device, &commandBufferAllocateInfo, &g_cmdBufferDraw);
  if (result != VK_SUCCESS) {
    printf("Failed to allocate command buffers\n");
    return SDL_FALSE;
  }

  return SDL_TRUE;
}

SDL_bool createSyncObjects()
{
  printf("%s called\n", __func__);

  VkResult result;

  VkSemaphoreCreateInfo semaphoreCreateInfo = {};
  semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  result = vkCreateSemaphore(g_device, &semaphoreCreateInfo, VK_NULL_HANDLE, &g_presentSemaphore);
  if (result != VK_SUCCESS) {
    printf("Failed to create present semaphore.\n");
    return SDL_FALSE;
  }

  result = vkCreateSemaphore(g_device, &semaphoreCreateInfo, VK_NULL_HANDLE, &g_renderSemaphore);
  if (result != VK_SUCCESS) {
    printf("Failed to create render semaphore.\n");
    return SDL_FALSE;
  }

  VkFenceCreateInfo fenceCreateInfo = {};
  fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  result = vkCreateFence(g_device, &fenceCreateInfo, VK_NULL_HANDLE, &g_renderFence);
  if (result != VK_SUCCESS) {
    printf("Failed to create render fence.\n");
    return SDL_FALSE;
  }

  return SDL_TRUE;
}

SDL_bool createPipeline()
{
  printf("%s called\n", __func__);

  VkResult result;

  VkShaderModule vertShaderModule;
  prepareShaderModule(rectangle_vert_spv, sizeof(rectangle_vert_spv), &vertShaderModule);

  VkShaderModule fragShaderModule;
  prepareShaderModule(rectangle_frag_spv, sizeof(rectangle_frag_spv), &fragShaderModule);

  VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
  vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertShaderStageInfo.module = vertShaderModule;
  vertShaderStageInfo.pName = "main";

  VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
  fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragShaderStageInfo.module = fragShaderModule;
  fragShaderStageInfo.pName = "main";

  VkPipelineShaderStageCreateInfo shaderStages[] =  {
    vertShaderStageInfo,
    fragShaderStageInfo
  };

  VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
  vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputInfo.vertexBindingDescriptionCount = 0;
  vertexInputInfo.vertexAttributeDescriptionCount = 0;

  VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo = {};
  inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  inputAssemblyInfo.primitiveRestartEnable = VK_FALSE;

  // Static viewport and scissors
  VkViewport viewport = {};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = (float) g_swapchainExtent.width;
  viewport.height = (float) g_swapchainExtent.height;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;

  VkRect2D scissor = {};
  scissor.extent = g_swapchainExtent;

  VkPipelineViewportStateCreateInfo viewportInfo = {};
  viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportInfo.viewportCount = 1;
  viewportInfo.pViewports = &viewport;
  viewportInfo.scissorCount = 1;
  viewportInfo.pScissors = &scissor;

  VkPipelineRasterizationStateCreateInfo rasterizerInfo = {};
  rasterizerInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizerInfo.depthClampEnable = VK_FALSE;
  rasterizerInfo.rasterizerDiscardEnable = VK_FALSE;
  rasterizerInfo.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizerInfo.lineWidth = 1.0f;
  rasterizerInfo.cullMode = VK_CULL_MODE_NONE;
  rasterizerInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
  rasterizerInfo.depthBiasEnable = VK_FALSE;

  VkPipelineMultisampleStateCreateInfo multisamplingInfo = {};
  multisamplingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisamplingInfo.sampleShadingEnable = VK_FALSE;
  multisamplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState colorBlendingAttachment = {};
  colorBlendingAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  colorBlendingAttachment.blendEnable = VK_FALSE;

  VkPipelineColorBlendStateCreateInfo colorBlendingInfo = {};
  colorBlendingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlendingInfo.logicOpEnable = VK_FALSE;
  colorBlendingInfo.attachmentCount = 1;
  colorBlendingInfo.pAttachments = &colorBlendingAttachment;

  VkPushConstantRange pushConstant = {};
  pushConstant.offset = 0;
  pushConstant.size = sizeof(Position);
  pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = 0;
  pipelineLayoutInfo.pPushConstantRanges = &pushConstant;
  pipelineLayoutInfo.pushConstantRangeCount = 1;

  result = vkCreatePipelineLayout(g_device, &pipelineLayoutInfo, VK_NULL_HANDLE, &g_pipelineLayout);
  if (result != VK_SUCCESS) {
    printf("Failed to create pipeline layout!\n");
    vkDestroyShaderModule(g_device, fragShaderModule, VK_NULL_HANDLE);
    vkDestroyShaderModule(g_device, vertShaderModule, VK_NULL_HANDLE);
    return SDL_FALSE;
  }

  VkGraphicsPipelineCreateInfo pipelineInfo = {};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.stageCount = sizeof(shaderStages) / (sizeof(*shaderStages));
  pipelineInfo.pStages = shaderStages;
  pipelineInfo.pVertexInputState = &vertexInputInfo;
  pipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
  pipelineInfo.pViewportState = &viewportInfo;
  pipelineInfo.pRasterizationState = &rasterizerInfo;
  pipelineInfo.pMultisampleState = &multisamplingInfo;
  pipelineInfo.pColorBlendState = &colorBlendingInfo;
  pipelineInfo.pDynamicState = VK_NULL_HANDLE;
  pipelineInfo.layout = g_pipelineLayout;
  pipelineInfo.renderPass = g_renderPass;
  pipelineInfo.subpass = 0;
  pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

  result = vkCreateGraphicsPipelines(g_device, VK_NULL_HANDLE, 1, &pipelineInfo, VK_NULL_HANDLE, &g_pipeline);
  if (result != VK_SUCCESS) {
    printf("Failed to create graphics pipeline! result = %d\n", result);
    vkDestroyShaderModule(g_device, fragShaderModule, VK_NULL_HANDLE);
    vkDestroyShaderModule(g_device, vertShaderModule, VK_NULL_HANDLE);
    return SDL_FALSE;
  }

  vkDestroyShaderModule(g_device, fragShaderModule, VK_NULL_HANDLE);
  vkDestroyShaderModule(g_device, vertShaderModule, VK_NULL_HANDLE);

  return result == VK_SUCCESS ? SDL_TRUE : SDL_FALSE;
}

SDL_bool drawRectangle()
{
  vkCmdBindPipeline(g_cmdBufferDraw, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipeline);

  vkCmdPushConstants(g_cmdBufferDraw, g_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Position), &delta);
  vkCmdDraw(g_cmdBufferDraw, 6, 1, 0, 0);

  return SDL_TRUE;
}

// ------ Public API ----------
//


// Main starting point for Vulkan
//
SDL_bool InitializeVulkan(SDL_Window* pWindowHandle, int width, int height)
{
  if (!initVulkanCore(pWindowHandle)) {
    return SDL_FALSE;
  }

  if (!initLogicalDevice()) {
    return SDL_FALSE;
  }

  if (!initSwapchain(pWindowHandle, width, height)) {
    return SDL_FALSE;
  }

  if (!createRenderPass()) {
    return SDL_FALSE;
  }

  if (!createPipeline()) {
    return SDL_FALSE;
  }

  if (!createFramebuffers()) {
    return SDL_FALSE;
  }

  if (!createCommandBuffers()) {
    return SDL_FALSE;
  }

  if (!createSyncObjects()) {
    return SDL_FALSE;
  }

  return SDL_TRUE;
}

void Update(float position)
{
  delta.x = position;
}

void Draw()
{
  VkClearValue clearValue = { 0.2f, 0.2f, 0.2f, 1.0f };

  vkWaitForFences(g_device, 1, &g_renderFence, VK_TRUE, UINT64_MAX);
  vkResetFences(g_device, 1, &g_renderFence);

  uint32_t swapchainImageIndex = 0;
  vkAcquireNextImageKHR(g_device, g_swapchain, UINT64_MAX, g_presentSemaphore, VK_NULL_HANDLE, &swapchainImageIndex);

  vkResetCommandBuffer(g_cmdBufferDraw, 0);

  VkCommandBufferBeginInfo beginInfo = {};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  vkBeginCommandBuffer(g_cmdBufferDraw, &beginInfo);
  {
    VkRenderPassBeginInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;

    renderPassInfo.renderPass = g_renderPass;
    renderPassInfo.renderArea.offset.x = 0;
    renderPassInfo.renderArea.offset.y = 0;
    renderPassInfo.renderArea.extent = g_swapchainExtent;
    renderPassInfo.framebuffer = g_framebuffers[swapchainImageIndex];

    //connect clear values
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;

    vkCmdBeginRenderPass(g_cmdBufferDraw, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    drawRectangle();

    vkCmdEndRenderPass(g_cmdBufferDraw);
  }
  vkEndCommandBuffer(g_cmdBufferDraw);

  // Submit
  {
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submit = {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.pWaitDstStageMask = &waitStage;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &g_presentSemaphore;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &g_renderSemaphore;

    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &g_cmdBufferDraw;

    vkQueueSubmit(g_presentQueue, 1, &submit, g_renderFence);
  }

  // Present
  {
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.pSwapchains = &g_swapchain;
    presentInfo.swapchainCount = 1;

    presentInfo.pWaitSemaphores = &g_renderSemaphore;
    presentInfo.waitSemaphoreCount = 1;

    presentInfo.pImageIndices = &swapchainImageIndex;

    vkQueuePresentKHR(g_presentQueue, &presentInfo);
  }
}

// Release Vulkan resources
//
void CleanupVulkan()
{
  if (g_instance != VK_NULL_HANDLE) {
    vkDestroySemaphore(g_device, g_presentSemaphore, NULL);
    vkDestroySemaphore(g_device, g_renderSemaphore, NULL);
    vkDestroyFence(g_device, g_renderFence, VK_NULL_HANDLE);
    vkDestroyPipelineLayout(g_device, g_pipelineLayout, VK_NULL_HANDLE);
    vkDestroyPipeline(g_device, g_pipeline, VK_NULL_HANDLE);
    vkDestroyCommandPool(g_device, g_commandPool, VK_NULL_HANDLE);
    vkDestroyRenderPass(g_device, g_renderPass, VK_NULL_HANDLE);
    vkDestroySwapchainKHR(g_device, g_swapchain, VK_NULL_HANDLE);

    for (int i = 0; i < g_swapchainImageCount; i++) {
      vkDestroyFramebuffer(g_device, g_framebuffers[i], VK_NULL_HANDLE);
    }

    vkDestroyDevice(g_device, VK_NULL_HANDLE);
    vkDestroyInstance(g_instance, VK_NULL_HANDLE);

    g_instance = VK_NULL_HANDLE;
  }

  if (g_swapchainImages != VK_NULL_HANDLE) {
    free(g_swapchainImages);
  }

  if (g_queueFamilyProperties != VK_NULL_HANDLE) {
    free(g_queueFamilyProperties);
  }
}
