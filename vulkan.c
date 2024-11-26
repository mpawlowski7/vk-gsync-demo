#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "vulkan/vulkan.h"
#include "SDL2/SDL_vulkan.h"

#include <vulkan/vulkan_core.h>

#include <stdio.h>
#include <time.h>

// Global variables declaration
//
static VkInstance                        g_instance;
#ifdef VULKAN_DEBUG
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
#ifdef VULKAN_DEBUG
static const char **g_enabledValidationLayers = {};
#endif

// Function declaration
//
#ifdef VULKAN_DEBUG
static PFN_vkCreateDebugReportCallbackEXT SDL2_vkCreateDebugReportCallbackEXT;
#endif

// ------ Helper functions -----
//
static SDL_bool getMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties, uint32_t *pMemoryTypeIndex)
{
  for (uint32_t i = 0; i < g_physicalDeviceMemoryProperties.memoryTypeCount; i++) {
    if ((typeFilter & (1 << i))
        && (g_physicalDeviceMemoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
      *pMemoryTypeIndex = i;
      return SDL_TRUE;
    }
  }
  return SDL_FALSE;
}

static SDL_bool prepareShaderModule(const char* shaderBinPath, VkShaderModule *pShaderModule)
{
  FILE *shaderFile = fopen(shaderBinPath, "rb");
  if (shaderFile == NULL) {
    printf("Failed to open shader file %s\n", shaderBinPath);
    return SDL_FALSE;
  }

  fseek(shaderFile, 0, SEEK_END);
  uint32_t shaderBinSize = ftell(shaderFile);
  fseek(shaderFile, 0, SEEK_SET);

  uint32_t* shaderCode = malloc(shaderBinSize);
  fread(shaderCode, shaderBinSize, 1, shaderFile);
  fclose(shaderFile);

  VkShaderModuleCreateInfo shaderInfo = {};
  shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  shaderInfo.codeSize = shaderBinSize;
  shaderInfo.pCode = shaderCode;

  VkResult result = vkCreateShaderModule(g_device, &shaderInfo, VK_NULL_HANDLE, pShaderModule);
  if (result != VK_SUCCESS) {
    printf("Failed to create shader module %s\n", shaderBinPath);
    free(shaderCode);
    return SDL_FALSE;
  }

  free(shaderCode);
  return SDL_TRUE;
}

// ------ Private API ----------
//
#ifdef VULKAN_DEBUG
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

  uint32_t extensionCount = 0;
  SDL_Vulkan_GetInstanceExtensions(appWindow, &extensionCount, VK_NULL_HANDLE);

  const char *extensionNames[extensionCount];
  SDL_Vulkan_GetInstanceExtensions(appWindow, &extensionCount, extensionNames);

  printf("Vulkan extensions count: %d\n", extensionCount);
  for (int i = 0; i < extensionCount; i++) {
    printf("%s\n", extensionNames[i]);
  }

  VkApplicationInfo appInfo = {};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pNext = VK_NULL_HANDLE;
  appInfo.pApplicationName = "vk-gsync-demo";
  appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.pEngineName = "DXVK";
  appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.apiVersion = VK_API_VERSION_1_0;

  VkInstanceCreateInfo instanceInfo = {};
  instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.pNext = VK_NULL_HANDLE;
  instanceInfo.pApplicationInfo = &appInfo;
#ifdef VULKAN_DEBUG
  instanceInfo.ppEnabledLayerNames = g_enabledValidationLayers;
#endif
  instanceInfo.enabledLayerCount = 0;
  instanceInfo.enabledExtensionCount = extensionCount;
  instanceInfo.ppEnabledExtensionNames = extensionNames;

  VkResult result = vkCreateInstance(&instanceInfo, VK_NULL_HANDLE, &g_instance);
  if (result != VK_SUCCESS) {
    printf("Failed to create Vulkan instance. Result = %d\n", result);
    return SDL_FALSE;
  }

#ifdef VULKAN_DEBUG
  SDL2_vkCreateDebugReportCallbackEXT = (PFN_vkCreateDebugReportCallbackEXT) SDL_Vulkan_GetVkGetInstanceProcAddr();

  VkDebugReportCallbackCreateInfoEXT debugCallbackCreateInfo = {};
  debugCallbackCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
  debugCallbackCreateInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
  debugCallbackCreateInfo.pfnCallback = vulkanDebugCallback;

   result = SDL2_vkCreateDebugReportCallbackEXT(*g_instance, &debugCallbackCreateInfo,
                                             VK_NULL_HANDLE, &g_debugCallbackEXT);
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
#ifdef VULKAN_DEBUG
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

SDL_bool initSwapchain(SDL_Window *window)
{
  printf("%s called\n", __func__);

  int width, height = 0;
  SDL_Vulkan_CreateSurface(window, g_instance, &g_surface);

  SDL_Vulkan_GetDrawableSize(window, &width, &height);
  printf("SDL_Vulkan_GetDrawableSize() width = %d height = %d\n", width, height);

  g_swapchainExtent.width = width;
  g_swapchainExtent.height = height;

  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_physicalDevice, g_surface, &g_surfaceCapabilities);

  uint32_t surfaceFormatsCount;
  vkGetPhysicalDeviceSurfaceFormatsKHR(g_physicalDevice, g_surface, &surfaceFormatsCount, VK_NULL_HANDLE);

  VkSurfaceFormatKHR surfaceFormats[surfaceFormatsCount];
  vkGetPhysicalDeviceSurfaceFormatsKHR(g_physicalDevice, g_surface, &surfaceFormatsCount, surfaceFormats);

  if (surfaceFormats[0].format != VK_FORMAT_B8G8R8A8_UNORM) {
    printf("VK_FORMAT_B8G8R8A8_UNORM not supported\n");
    return SDL_FALSE;
  }

  g_surfaceFormat = surfaceFormats[0];

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

  VkResult result = vkCreateSwapchainKHR(g_device, &swapchainInfo, VK_NULL_HANDLE, &g_swapchain);
  if (result != VK_SUCCESS) {
    printf("Failed to create swapchain\n");
    return SDL_FALSE;
  }

  vkGetSwapchainImagesKHR(g_device, g_swapchain, &g_swapchainImageCount, VK_NULL_HANDLE);
  printf("Swapchain image count: %d\n", g_swapchainImageCount);

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
  printf("%s\n", __func__);

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
  prepareShaderModule("./rectangle.vert.spv", &vertShaderModule);

  VkShaderModule fragShaderModule;
  prepareShaderModule("./rectangle.frag.spv", &fragShaderModule);

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
    printf("failed to create pipeline layout!");
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
    printf("failed to create pipeline layout!");
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

  delta.x += 0.01;
    if (delta.x >= 2.0f) { delta.x = 0.0f; }

  return SDL_TRUE;
}

// ------ Public API ----------
//


// Main starting point for Vulkan
//
SDL_bool InitializeVulkan(SDL_Window *appWindow)
{
  if (!initVulkanCore(appWindow)) {
    return SDL_FALSE;
  }

  if (!initLogicalDevice()) {
    return SDL_FALSE;
  }

  if (!initSwapchain(appWindow)) {
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

void Update()
{
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

  // Temp framelimiter;
  struct timespec ts;
  ts.tv_sec = 0;
  ts.tv_nsec = 16600000L;
  nanosleep(&ts, NULL);
}

// Release Vulkan resources
//
void CleanupVulkan()
{
  if (g_instance != VK_NULL_HANDLE) {
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
