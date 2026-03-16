module;
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
#include <iostream>
#include <vector>
#include <optional>
#include <set>
#include <algorithm> 
#include <limits>

export module Window;

import Types;

export namespace Render
{
    /**
     * @brief Encapsulates the Vulkan state context to avoid global mutable state.
     */
    struct VulkanContext {
        VkInstance instance = VK_NULL_HANDLE;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkQueue computeQueue = VK_NULL_HANDLE;
        VkQueue presentQueue = VK_NULL_HANDLE;
        GLFWwindow* window = nullptr;
        
        VkSwapchainKHR swapChain = VK_NULL_HANDLE;
        std::vector<VkImage> swapChainImages;
        VkFormat swapChainImageFormat;
        VkExtent2D swapChainExtent;
        std::vector<VkImageView> swapChainImageViews;
        
        uint32_t computeQueueFamilyIndex = 0;
        bool framebufferResized = false;
    };

    /**
     * @brief Manages the Vulkan window and core instance lifecycle using RAII.
     */
    class WindowManager {
    public:
        VulkanContext ctx;

        WindowManager() = default;
        ~WindowManager() { cleanup(); }

        WindowManager(const WindowManager&) = delete;
        WindowManager& operator=(const WindowManager&) = delete;

        /**
         * @brief Initializes Vulkan core components like Instance, Device, and Swapchain.
         * @warning Throws or aborts on critical Vulkan initialization failure.
         */
        void init_vulkan() {
            init_window();
            create_instance();
            check(glfwCreateWindowSurface(ctx.instance, ctx.window, nullptr, &ctx.surface) == VK_SUCCESS, "Failed to create window surface!");
            pick_physical_device();
            create_logical_device();
            
            create_swapchain();
            create_image_views();
            
            std::cout << "Vulkan Initialized with Swapchain (OOP Mode)!\n";
        }

        /**
         * @brief Recreates swapchain when window is resized.
         */
        void recreate_swapchain() {
            int width = 0, height = 0;
            glfwGetFramebufferSize(ctx.window, &width, &height);
            while (width == 0 || height == 0) {
                glfwGetFramebufferSize(ctx.window, &width, &height);
                glfwWaitEvents();
            }

            vkDeviceWaitIdle(ctx.device);

            cleanup_swapchain();
            create_swapchain();
            create_image_views();
        }

    private:
        struct QueueFamilyIndices {
            std::optional<uint32_t> computeFamily;
            std::optional<uint32_t> presentFamily;
            bool isComplete() { return computeFamily.has_value() && presentFamily.has_value(); }
        };

        struct SwapChainSupportDetails {
            VkSurfaceCapabilitiesKHR capabilities;
            std::vector<VkSurfaceFormatKHR> formats;
            std::vector<VkPresentModeKHR> presentModes;
        };

        static void framebufferResizeCallback(GLFWwindow* window, int /*width*/, int /*height*/) {
            auto* context = reinterpret_cast<VulkanContext*>(glfwGetWindowUserPointer(window));
            if (context) context->framebufferResized = true;
        }

        QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) {
            QueueFamilyIndices indices;
            uint32_t queueFamilyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
            std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

            int i = 0;
            for (const auto& queueFamily : queueFamilies) {
                if (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) indices.computeFamily = i;
                VkBool32 presentSupport = false;
                vkGetPhysicalDeviceSurfaceSupportKHR(device, i, ctx.surface, &presentSupport);
                if (presentSupport) indices.presentFamily = i;
                if (indices.isComplete()) break;
                i++;
            }
            return indices;
        }

        SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device) {
            SwapChainSupportDetails details;
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, ctx.surface, &details.capabilities);
            uint32_t formatCount;
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, ctx.surface, &formatCount, nullptr);
            if (formatCount != 0) {
                details.formats.resize(formatCount);
                vkGetPhysicalDeviceSurfaceFormatsKHR(device, ctx.surface, &formatCount, details.formats.data());
            }
            uint32_t presentModeCount;
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, ctx.surface, &presentModeCount, nullptr);
            if (presentModeCount != 0) {
                details.presentModes.resize(presentModeCount);
                vkGetPhysicalDeviceSurfacePresentModesKHR(device, ctx.surface, &presentModeCount, details.presentModes.data());
            }
            return details;
        }

        VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
            for (const auto& availableFormat : availableFormats) {
                if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                    return availableFormat;
                }
            }
            return availableFormats[0];
        }

        VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
            for (const auto& availablePresentMode : availablePresentModes) {
                if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) return availablePresentMode;
            }
            return VK_PRESENT_MODE_FIFO_KHR;
        }

        VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
            if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
                return capabilities.currentExtent;
            } else {
                int width, height;
                glfwGetFramebufferSize(ctx.window, &width, &height);
                VkExtent2D actualExtent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
                actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
                actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
                return actualExtent;
            }
        }

        void create_swapchain() {
            SwapChainSupportDetails swapChainSupport = querySwapChainSupport(ctx.physicalDevice);
            VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
            VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
            VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

            uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
            if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
                imageCount = swapChainSupport.capabilities.maxImageCount;
            }

            VkSwapchainCreateInfoKHR createInfo = {
                .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
                .surface = ctx.surface,
                .minImageCount = imageCount,
                .imageFormat = surfaceFormat.format,
                .imageColorSpace = surfaceFormat.colorSpace,
                .imageExtent = extent,
                .imageArrayLayers = 1,
                .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT, 
                .preTransform = swapChainSupport.capabilities.currentTransform,
                .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                .presentMode = presentMode,
                .clipped = VK_TRUE,
                .oldSwapchain = VK_NULL_HANDLE
            };

            QueueFamilyIndices indices = findQueueFamilies(ctx.physicalDevice);
            uint32_t queueFamilyIndices[] = {indices.computeFamily.value(), indices.presentFamily.value()};

            if (indices.computeFamily != indices.presentFamily) {
                createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
                createInfo.queueFamilyIndexCount = 2;
                createInfo.pQueueFamilyIndices = queueFamilyIndices;
            } else {
                createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            }

            check(vkCreateSwapchainKHR(ctx.device, &createInfo, nullptr, &ctx.swapChain) == VK_SUCCESS, "Failed to create swapchain!");

            vkGetSwapchainImagesKHR(ctx.device, ctx.swapChain, &imageCount, nullptr);
            ctx.swapChainImages.resize(imageCount);
            vkGetSwapchainImagesKHR(ctx.device, ctx.swapChain, &imageCount, ctx.swapChainImages.data());

            ctx.swapChainImageFormat = surfaceFormat.format;
            ctx.swapChainExtent = extent;
        }

        void create_image_views() {
            ctx.swapChainImageViews.resize(ctx.swapChainImages.size());
            for (size_t i = 0; i < ctx.swapChainImages.size(); i++) {
                VkImageViewCreateInfo createInfo = {
                    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                    .image = ctx.swapChainImages[i],
                    .viewType = VK_IMAGE_VIEW_TYPE_2D,
                    .format = ctx.swapChainImageFormat,
                    .components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY },
                    .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
                };
                check(vkCreateImageView(ctx.device, &createInfo, nullptr, &ctx.swapChainImageViews[i]) == VK_SUCCESS, "Failed to create image views!");
            }
        }

        void cleanup_swapchain() {
            for (auto imageView : ctx.swapChainImageViews) {
                vkDestroyImageView(ctx.device, imageView, nullptr);
            }
            vkDestroySwapchainKHR(ctx.device, ctx.swapChain, nullptr);
        }

        void init_window() {
            check(glfwInit() == GLFW_TRUE, "Failed to initialize GLFW!");
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); 
            glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE); 
            
            ctx.window = glfwCreateWindow(800, 600, "Vulkan Raytracing", nullptr, nullptr);
            check(ctx.window != nullptr, "Failed to create GLFW window!");

            glfwSetWindowUserPointer(ctx.window, &ctx);
            glfwSetFramebufferSizeCallback(ctx.window, framebufferResizeCallback);
        }

        void create_instance() {
            VkApplicationInfo appInfo = {
                .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                .pApplicationName = "RayTracing App",
                .apiVersion = VK_API_VERSION_1_2
            };

            uint32_t glfwExtensionCount = 0;
            const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

            VkInstanceCreateInfo createInfo = {
                .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                .pApplicationInfo = &appInfo,
                .enabledExtensionCount = glfwExtensionCount,
                .ppEnabledExtensionNames = glfwExtensions
            };

            check(vkCreateInstance(&createInfo, nullptr, &ctx.instance) == VK_SUCCESS, "Failed to create Vulkan instance!");
        }

        void pick_physical_device() {
            uint32_t deviceCount = 0;
            vkEnumeratePhysicalDevices(ctx.instance, &deviceCount, nullptr);
            check(deviceCount > 0, "Failed to find GPUs with Vulkan support!");
            
            std::vector<VkPhysicalDevice> devices(deviceCount);
            vkEnumeratePhysicalDevices(ctx.instance, &deviceCount, devices.data());
            
            for (const auto& dev : devices) {
                QueueFamilyIndices indices = findQueueFamilies(dev);
                SwapChainSupportDetails details = querySwapChainSupport(dev);
                if (indices.isComplete() && !details.formats.empty() && !details.presentModes.empty()) {
                    ctx.physicalDevice = dev;
                    break;
                }
            }
            check(ctx.physicalDevice != VK_NULL_HANDLE, "Failed to find a suitable GPU!");
        }

        void create_logical_device() {
            QueueFamilyIndices indices = findQueueFamilies(ctx.physicalDevice);
            ctx.computeQueueFamilyIndex = indices.computeFamily.value();
            
            std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
            std::set<uint32_t> uniqueQueueFamilies = {indices.computeFamily.value(), indices.presentFamily.value()};
            float queuePriority = 1.0f;
            
            for (uint32_t queueFamily : uniqueQueueFamilies) {
                VkDeviceQueueCreateInfo queueCreateInfo = {
                    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                    .queueFamilyIndex = queueFamily,
                    .queueCount = 1,
                    .pQueuePriorities = &queuePriority
                };
                queueCreateInfos.push_back(queueCreateInfo);
            }

            const std::vector<const char*> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
            
            VkDeviceCreateInfo createInfo = {
                .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                .queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size()),
                .pQueueCreateInfos = queueCreateInfos.data(),
                .enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
                .ppEnabledExtensionNames = deviceExtensions.data()
            };

            check(vkCreateDevice(ctx.physicalDevice, &createInfo, nullptr, &ctx.device) == VK_SUCCESS, "Failed to create logical device!");
            
            vkGetDeviceQueue(ctx.device, indices.computeFamily.value(), 0, &ctx.computeQueue);
            vkGetDeviceQueue(ctx.device, indices.presentFamily.value(), 0, &ctx.presentQueue);
        }

        void cleanup() {
            if (!ctx.device) return;
            vkDeviceWaitIdle(ctx.device);
            cleanup_swapchain();
            vkDestroyDevice(ctx.device, nullptr);
            vkDestroySurfaceKHR(ctx.instance, ctx.surface, nullptr);
            vkDestroyInstance(ctx.instance, nullptr);
            glfwDestroyWindow(ctx.window);
            glfwTerminate();
            ctx.device = VK_NULL_HANDLE;
        }
    };
}