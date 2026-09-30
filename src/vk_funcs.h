// Hand-rolled Vulkan function loader: every entry point is fetched at runtime
// through GLFW, so the game never links against a Vulkan SDK library.
#pragma once
#include <vulkan/vulkan.h>

#define VK_GLOBAL_FUNCS(X)                  \
    X(vkCreateInstance)                     \
    X(vkEnumerateInstanceLayerProperties)   \
    X(vkEnumerateInstanceExtensionProperties)

#define VK_INSTANCE_FUNCS(X)                        \
    X(vkDestroyInstance)                            \
    X(vkEnumeratePhysicalDevices)                   \
    X(vkGetPhysicalDeviceProperties)                \
    X(vkGetPhysicalDeviceFeatures)                  \
    X(vkGetPhysicalDeviceQueueFamilyProperties)     \
    X(vkGetPhysicalDeviceSurfaceSupportKHR)         \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)    \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR)         \
    X(vkGetPhysicalDeviceSurfacePresentModesKHR)    \
    X(vkGetPhysicalDeviceMemoryProperties)          \
    X(vkGetPhysicalDeviceFormatProperties)          \
    X(vkEnumerateDeviceExtensionProperties)         \
    X(vkCreateDevice)                               \
    X(vkGetDeviceProcAddr)                          \
    X(vkDestroySurfaceKHR)

#define VK_DEVICE_FUNCS(X)              \
    X(vkGetDeviceQueue)                 \
    X(vkDestroyDevice)                  \
    X(vkCreateSwapchainKHR)             \
    X(vkDestroySwapchainKHR)            \
    X(vkGetSwapchainImagesKHR)          \
    X(vkAcquireNextImageKHR)            \
    X(vkQueuePresentKHR)                \
    X(vkCreateImageView)                \
    X(vkDestroyImageView)               \
    X(vkCreateImage)                    \
    X(vkDestroyImage)                   \
    X(vkGetImageMemoryRequirements)     \
    X(vkBindImageMemory)                \
    X(vkAllocateMemory)                 \
    X(vkFreeMemory)                     \
    X(vkCreateBuffer)                   \
    X(vkDestroyBuffer)                  \
    X(vkGetBufferMemoryRequirements)    \
    X(vkBindBufferMemory)               \
    X(vkMapMemory)                      \
    X(vkUnmapMemory)                    \
    X(vkCreateRenderPass)               \
    X(vkDestroyRenderPass)              \
    X(vkCreateFramebuffer)              \
    X(vkDestroyFramebuffer)             \
    X(vkCreateShaderModule)             \
    X(vkDestroyShaderModule)            \
    X(vkCreatePipelineLayout)           \
    X(vkDestroyPipelineLayout)          \
    X(vkCreateGraphicsPipelines)        \
    X(vkDestroyPipeline)                \
    X(vkCreateDescriptorSetLayout)      \
    X(vkDestroyDescriptorSetLayout)     \
    X(vkCreateDescriptorPool)           \
    X(vkDestroyDescriptorPool)          \
    X(vkAllocateDescriptorSets)         \
    X(vkUpdateDescriptorSets)           \
    X(vkCreateSampler)                  \
    X(vkDestroySampler)                 \
    X(vkCreateCommandPool)              \
    X(vkDestroyCommandPool)             \
    X(vkAllocateCommandBuffers)         \
    X(vkFreeCommandBuffers)             \
    X(vkBeginCommandBuffer)             \
    X(vkEndCommandBuffer)               \
    X(vkResetCommandBuffer)             \
    X(vkCmdBeginRenderPass)             \
    X(vkCmdEndRenderPass)               \
    X(vkCmdBindPipeline)                \
    X(vkCmdBindVertexBuffers)           \
    X(vkCmdBindIndexBuffer)             \
    X(vkCmdBindDescriptorSets)          \
    X(vkCmdDraw)                        \
    X(vkCmdDrawIndexed)                 \
    X(vkCmdSetViewport)                 \
    X(vkCmdSetScissor)                  \
    X(vkCmdPushConstants)               \
    X(vkCmdCopyBuffer)                  \
    X(vkCmdPipelineBarrier)             \
    X(vkCmdCopyImageToBuffer)           \
    X(vkCreateSemaphore)                \
    X(vkDestroySemaphore)               \
    X(vkCreateFence)                    \
    X(vkDestroyFence)                   \
    X(vkWaitForFences)                  \
    X(vkResetFences)                    \
    X(vkQueueSubmit)                    \
    X(vkQueueWaitIdle)                  \
    X(vkDeviceWaitIdle)

#define MB_VK_DECLARE(name) extern PFN_##name name;
VK_GLOBAL_FUNCS(MB_VK_DECLARE)
VK_INSTANCE_FUNCS(MB_VK_DECLARE)
VK_DEVICE_FUNCS(MB_VK_DECLARE)
#undef MB_VK_DECLARE

bool vkLoadGlobal(PFN_vkGetInstanceProcAddr gipa);
bool vkLoadInstance(VkInstance instance);
bool vkLoadDevice(VkDevice device);
PFN_vkVoidFunction vkLoadInstanceFunc(VkInstance instance, const char* name);
