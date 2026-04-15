module;
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
#include <vector>
#include <iostream>
#include <cstring>
#include <fstream>
#include <array>
#include <cmath>
#include <algorithm> 
#include <cstdlib>
#include <span> 
#include <functional>

export module ShaderController;

import Types;
import Window;

export namespace Render
{
    /**
     * @brief Decoupled render parameters provided by external UI.
     */
    struct RenderParams {
        float camAzimuth;
        float camElevation;
        float camDistance;
        bool flipUp;
        int maxBounces;
        float light1Color[3];
        float light2Color[3];
        float light1Pos[3];
        float light2Pos[3];
    };

    struct PushConstants {
        float minBounds[4];
        float extent[4];   
        float camPos[4];   
        float camDir[4];   
        float camUp[4];    
        int frameCount;    
        int padding[3];    
    };

    /**
     * @brief Handles Vulkan buffer creation and memory allocation.
     * Separates memory management logic from the rendering pipeline.
     */
    class BufferAllocator {
    public:
        BufferAllocator(VulkanContext* context) : ctx(context) {}

        /**
         * @brief Creates a Vulkan buffer and allocates memory for it.
         * @param size Size of the buffer in bytes.
         * @param usage Usage flags for the buffer.
         * @param properties Memory property flags.
         * @param buffer Output buffer handle.
         * @param bufferMemory Output memory handle.
         * @warning Assumes context is valid. Destroys existing buffer if already allocated.
         */
        void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
            if (buffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(ctx->device, buffer, nullptr);
                buffer = VK_NULL_HANDLE;
            }
            if (bufferMemory != VK_NULL_HANDLE) {
                vkFreeMemory(ctx->device, bufferMemory, nullptr);
                bufferMemory = VK_NULL_HANDLE;
            }

            VkBufferCreateInfo bufferInfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = usage, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
            check(vkCreateBuffer(ctx->device, &bufferInfo, nullptr, &buffer) == VK_SUCCESS, "Failed to create buffer");
            
            VkMemoryRequirements memRequirements; 
            vkGetBufferMemoryRequirements(ctx->device, buffer, &memRequirements);
            
            VkMemoryAllocateInfo allocInfo = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = memRequirements.size, .memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties) };
            check(vkAllocateMemory(ctx->device, &allocInfo, nullptr, &bufferMemory) == VK_SUCCESS, "Failed to allocate buffer memory");
            vkBindBufferMemory(ctx->device, buffer, bufferMemory, 0);
        }

    private:
        VulkanContext* ctx;

        uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
            VkPhysicalDeviceMemoryProperties memProperties; 
            vkGetPhysicalDeviceMemoryProperties(ctx->physicalDevice, &memProperties);
            for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) 
                if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) return i;
            
            check(false, "Failed to find suitable memory type!");
            return -1;
        }
    };

    /**
     * @brief Handles file I/O and shader compilation.
     * Separates system calls and file reading from the rendering pipeline.
     */
    class ShaderLoader {
    public:
        /**
         * @brief Compiles a GLSL shader to SPIR-V and loads it into a Vulkan module.
         * @param ctx Pointer to active VulkanContext.
         * @param sourcePath Path to the .comp source file.
         * @param spvPath Path for the compiled .spv output.
         */
        static VkShaderModule compileAndLoad(VulkanContext* ctx, const std::string& sourcePath, const std::string& spvPath) {
            std::string command = "glslc " + sourcePath + " -o " + spvPath;
            int result = std::system(command.c_str());
            if (result != 0) {
                std::cerr << "[Warning] Shader compilation failed or 'glslc' not found.\n";
            }

            auto code = readFile(spvPath);
            VkShaderModuleCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = code.size(), .pCode = reinterpret_cast<const uint32_t*>(code.data()) };
            VkShaderModule m; 
            check(vkCreateShaderModule(ctx->device, &ci, nullptr, &m) == VK_SUCCESS, "Failed to create shader module");
            return m;
        }

    private:
        static std::vector<char> readFile(const std::string& filename) {
            std::ifstream file(filename, std::ios::ate | std::ios::binary);
            check(file.is_open(), ("Failed to open file: " + filename).c_str());
            size_t size = (size_t)file.tellg(); 
            std::vector<char> buf(size); 
            file.seekg(0); 
            file.read(buf.data(), size); 
            return buf;
        }
    };

    /**
     * @brief Controls compute shader execution and resource binding.
     * Focused strictly on pipeline state, descriptor sets, and command recording.
     */
    class ShaderController {
    public:
        ShaderController() = default;
        ~ShaderController() { cleanup(); }
        
        ShaderController(const ShaderController&) = delete;
        ShaderController& operator=(const ShaderController&) = delete;

        /**
         * @brief Initializes shader resources using the provided context.
         * @param context Pointer to active VulkanContext.
         * @warning Assumes context pointer is valid.
         */
        bool init(VulkanContext* context) {
            ctx = context;
            BufferAllocator allocator(ctx);

            allocator.createBuffer(sizeof(SceneSettingsUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, uboSettingsBuffer, uboSettingsBufferMemory);
            vkMapMemory(ctx->device, uboSettingsBufferMemory, 0, sizeof(SceneSettingsUBO), 0, &uboMappedData);

            std::array<VkDescriptorSetLayoutBinding, 4> bindings{};
            bindings[0] = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
            bindings[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
            bindings[2] = {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
            bindings[3] = {3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
            
            VkDescriptorSetLayoutCreateInfo li = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 4, .pBindings = bindings.data() };
            check(vkCreateDescriptorSetLayout(ctx->device, &li, nullptr, &computeDescriptorSetLayout) == VK_SUCCESS, "Layout creation failed");

            VkPushConstantRange pc = { .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = sizeof(PushConstants) };
            VkPipelineLayoutCreateInfo pli = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &computeDescriptorSetLayout, .pushConstantRangeCount = 1, .pPushConstantRanges = &pc };
            check(vkCreatePipelineLayout(ctx->device, &pli, nullptr, &computePipelineLayout) == VK_SUCCESS, "Pipeline layout failed");

            VkShaderModule mod = ShaderLoader::compileAndLoad(ctx, "src/shaders/raytrace.comp", "src/shaders/raytrace.comp.spv");
            
            VkPipelineShaderStageCreateInfo ssi = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = mod, .pName = "main" };
            VkComputePipelineCreateInfo cpi = { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, .stage = ssi, .layout = computePipelineLayout };
            check(vkCreateComputePipelines(ctx->device, VK_NULL_HANDLE, 1, &cpi, nullptr, &computePipeline) == VK_SUCCESS, "Pipeline creation failed");
            vkDestroyShaderModule(ctx->device, mod, nullptr);

            std::array<VkDescriptorPoolSize, 3> ps{}; 
            ps[0] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 20}; 
            ps[1] = {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 10}; 
            ps[2] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10}; 
            
            VkDescriptorPoolCreateInfo pi = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = static_cast<uint32_t>(ctx->swapChainImages.size()), .poolSizeCount = 3, .pPoolSizes = ps.data() };
            check(vkCreateDescriptorPool(ctx->device, &pi, nullptr, &descriptorPool) == VK_SUCCESS, "Pool creation failed");

            std::vector<VkDescriptorSetLayout> layouts(ctx->swapChainImages.size(), computeDescriptorSetLayout);
            VkDescriptorSetAllocateInfo dai = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = descriptorPool, .descriptorSetCount = static_cast<uint32_t>(layouts.size()), .pSetLayouts = layouts.data() };
            descriptorSets.resize(layouts.size()); 
            check(vkAllocateDescriptorSets(ctx->device, &dai, descriptorSets.data()) == VK_SUCCESS, "Set allocation failed");

            update_descriptor_sets();

            VkCommandPoolCreateInfo cpi2 = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = ctx->computeQueueFamilyIndex };
            check(vkCreateCommandPool(ctx->device, &cpi2, nullptr, &commandPool) == VK_SUCCESS, "Cmd pool failed");
            
            commandBuffers.resize(MAX_FRAMES); 
            VkCommandBufferAllocateInfo cbai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = commandPool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = static_cast<uint32_t>(MAX_FRAMES) };
            check(vkAllocateCommandBuffers(ctx->device, &cbai, commandBuffers.data()) == VK_SUCCESS, "Cmd buffer alloc failed");

            imgSem.resize(MAX_FRAMES); renSem.resize(MAX_FRAMES); fltFen.resize(MAX_FRAMES);
            VkSemaphoreCreateInfo sci = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
            VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT };
            for(int i=0; i<MAX_FRAMES; i++) { 
                check(vkCreateSemaphore(ctx->device, &sci, nullptr, &imgSem[i]) == VK_SUCCESS, "Sem create failed");
                check(vkCreateSemaphore(ctx->device, &sci, nullptr, &renSem[i]) == VK_SUCCESS, "Sem create failed");
                check(vkCreateFence(ctx->device, &fci, nullptr, &fltFen[i]) == VK_SUCCESS, "Fence create failed");
            }
            return true;
        }

        void on_resize() { update_descriptor_sets(); }

        void reload_buffers(std::span<const RaytraceTriangle> triangles, std::span<const BVHNode> nodes) {
            ssbo_triangle(triangles);
            ssbo_bvh(nodes);
            update_descriptor_sets();
            std::cout << "[GPU] Buffers updated successfully.\n";
        }

        /**
         * @brief Records command buffers and submits the frame to the GPU.
         * @param bounds The bounding box of the loaded mesh.
         * @param params Rendering parameters configured by the UI.
         * @param uiRenderCallback Callback to record ImGui rendering commands.
         * @warning Synchronizes with GPU using fences.
         */
        void draw_frame(const MeshBounds& bounds, const RenderParams& params, const std::function<void(VkCommandBuffer, uint32_t)>& uiRenderCallback) {
            if (!ctx) return;
            vkWaitForFences(ctx->device, 1, &fltFen[currentFrame], VK_TRUE, UINT64_MAX);
            uint32_t ii; 
            VkResult r = vkAcquireNextImageKHR(ctx->device, ctx->swapChain, UINT64_MAX, imgSem[currentFrame], VK_NULL_HANDLE, &ii);
            if(r == VK_ERROR_OUT_OF_DATE_KHR) return; 
            else check(r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR, "Swapchain acquire failed");
            
            if (uboMappedData) {
                SceneSettingsUBO ubo{};
                ubo.light1Color = {params.light1Color[0], params.light1Color[1], params.light1Color[2], 0.0f};
                ubo.light2Color = {params.light2Color[0], params.light2Color[1], params.light2Color[2], 0.0f};
                ubo.light1Pos = {params.light1Pos[0], params.light1Pos[1], params.light1Pos[2], 0.0f};
                ubo.light2Pos = {params.light2Pos[0], params.light2Pos[1], params.light2Pos[2], 0.0f};
                ubo.maxBounces = params.maxBounces;
                memcpy(uboMappedData, &ubo, sizeof(SceneSettingsUBO));
            }

            vkResetFences(ctx->device, 1, &fltFen[currentFrame]);
            vkResetCommandBuffer(commandBuffers[currentFrame], 0);
            
            recordCommandBuffer(commandBuffers[currentFrame], ii, bounds, params, uiRenderCallback);

            VkPipelineStageFlags ws[] = {VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT}; 
            VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .waitSemaphoreCount = 1, .pWaitSemaphores = &imgSem[currentFrame], .pWaitDstStageMask = ws, .commandBufferCount = 1, .pCommandBuffers = &commandBuffers[currentFrame], .signalSemaphoreCount = 1, .pSignalSemaphores = &renSem[currentFrame] };
            check(vkQueueSubmit(ctx->computeQueue, 1, &si, fltFen[currentFrame]) == VK_SUCCESS, "Submit failed");

            VkPresentInfoKHR pi = { .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .waitSemaphoreCount = 1, .pWaitSemaphores = &renSem[currentFrame], .swapchainCount = 1, .pSwapchains = &ctx->swapChain, .pImageIndices = &ii };
            vkQueuePresentKHR(ctx->presentQueue, &pi);
            currentFrame = (currentFrame + 1) % MAX_FRAMES;
        }

    private:
        VulkanContext* ctx = nullptr;
        
        VkBuffer triangleBuffer = VK_NULL_HANDLE; VkDeviceMemory triangleBufferMemory = VK_NULL_HANDLE;
        VkBuffer bvhBuffer = VK_NULL_HANDLE; VkDeviceMemory bvhBufferMemory = VK_NULL_HANDLE;
        VkBuffer uboSettingsBuffer = VK_NULL_HANDLE; VkDeviceMemory uboSettingsBufferMemory = VK_NULL_HANDLE;
        void* uboMappedData = nullptr; 

        VkDescriptorSetLayout computeDescriptorSetLayout = VK_NULL_HANDLE; 
        VkPipelineLayout computePipelineLayout = VK_NULL_HANDLE;
        VkPipeline computePipeline = VK_NULL_HANDLE; 
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> descriptorSets;
        
        VkCommandPool commandPool = VK_NULL_HANDLE; 
        std::vector<VkCommandBuffer> commandBuffers;
        std::vector<VkSemaphore> imgSem, renSem; 
        std::vector<VkFence> fltFen;
        
        int currentFrame = 0; 
        const int MAX_FRAMES = 2;

        void cleanup() {
            if (!ctx) return;
            vkDeviceWaitIdle(ctx->device);

            if (triangleBuffer) vkDestroyBuffer(ctx->device, triangleBuffer, nullptr);
            if (triangleBufferMemory) vkFreeMemory(ctx->device, triangleBufferMemory, nullptr);
            if (bvhBuffer) vkDestroyBuffer(ctx->device, bvhBuffer, nullptr);
            if (bvhBufferMemory) vkFreeMemory(ctx->device, bvhBufferMemory, nullptr);
            if (uboSettingsBuffer) vkDestroyBuffer(ctx->device, uboSettingsBuffer, nullptr);
            if (uboSettingsBufferMemory) vkFreeMemory(ctx->device, uboSettingsBufferMemory, nullptr);

            if (computeDescriptorSetLayout) vkDestroyDescriptorSetLayout(ctx->device, computeDescriptorSetLayout, nullptr);
            if (computePipelineLayout) vkDestroyPipelineLayout(ctx->device, computePipelineLayout, nullptr);
            if (computePipeline) vkDestroyPipeline(ctx->device, computePipeline, nullptr);
            if (descriptorPool) vkDestroyDescriptorPool(ctx->device, descriptorPool, nullptr);
            if (commandPool) vkDestroyCommandPool(ctx->device, commandPool, nullptr);

            for (auto sem : imgSem) vkDestroySemaphore(ctx->device, sem, nullptr);
            for (auto sem : renSem) vkDestroySemaphore(ctx->device, sem, nullptr);
            for (auto fen : fltFen) vkDestroyFence(ctx->device, fen, nullptr);
            
            ctx = nullptr;
        }

        void update_descriptor_sets() {
            if (descriptorSets.empty()) return; 

            for(size_t i=0; i<ctx->swapChainImages.size(); i++) {
                VkDescriptorBufferInfo bi1{triangleBuffer, 0, VK_WHOLE_SIZE};
                VkDescriptorBufferInfo bi2{bvhBuffer, 0, VK_WHOLE_SIZE};
                if (triangleBuffer == VK_NULL_HANDLE) continue;
                VkDescriptorImageInfo ii{VK_NULL_HANDLE, ctx->swapChainImageViews[i], VK_IMAGE_LAYOUT_GENERAL};
                VkDescriptorBufferInfo bi3{uboSettingsBuffer, 0, VK_WHOLE_SIZE}; 
                
                std::array<VkWriteDescriptorSet, 4> w{};
                w[0] = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = descriptorSets[i], .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &bi1 };
                w[1] = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = descriptorSets[i], .dstBinding = 1, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &bi2 };
                w[2] = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = descriptorSets[i], .dstBinding = 2, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .pImageInfo = &ii };
                w[3] = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = descriptorSets[i], .dstBinding = 3, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &bi3 };
                
                vkUpdateDescriptorSets(ctx->device, 4, w.data(), 0, nullptr);
            }
        }

        bool ssbo_triangle(std::span<const RaytraceTriangle> triangles) {
            if (triangles.empty()) return false;
            BufferAllocator allocator(ctx);
            allocator.createBuffer(sizeof(RaytraceTriangle) * triangles.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, triangleBuffer, triangleBufferMemory);
            void* data; 
            vkMapMemory(ctx->device, triangleBufferMemory, 0, sizeof(RaytraceTriangle) * triangles.size(), 0, &data);
            memcpy(data, triangles.data(), sizeof(RaytraceTriangle) * triangles.size()); 
            vkUnmapMemory(ctx->device, triangleBufferMemory);
            return true;
        }

        bool ssbo_bvh(std::span<const BVHNode> nodes) {
            if (nodes.empty()) return false;
            BufferAllocator allocator(ctx);
            allocator.createBuffer(sizeof(BVHNode) * nodes.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, bvhBuffer, bvhBufferMemory);
            void* data; 
            vkMapMemory(ctx->device, bvhBufferMemory, 0, sizeof(BVHNode) * nodes.size(), 0, &data);
            memcpy(data, nodes.data(), sizeof(BVHNode) * nodes.size()); 
            vkUnmapMemory(ctx->device, bvhBufferMemory);
            return true;
        }

        void recordCommandBuffer(VkCommandBuffer cb, uint32_t ii, const MeshBounds& b, const RenderParams& params, const std::function<void(VkCommandBuffer, uint32_t)>& uiCallback) {
            VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            vkBeginCommandBuffer(cb, &bi);
            
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout, 0, 1, &descriptorSets[ii], 0, nullptr);

            vec3 ext = {b.maxPos.x - b.minPos.x, b.maxPos.y - b.minPos.y, b.maxPos.z - b.minPos.z};
            vec3 center = {(b.maxPos.x + b.minPos.x)/2.0f, (b.maxPos.y + b.minPos.y)/2.0f, (b.maxPos.z + b.minPos.z)/2.0f};
            
            float radius = params.camDistance;
            float theta = params.camAzimuth;
            float phi = params.camElevation;

            float camX = center.x + radius * cos(phi) * cos(theta);
            float camY = center.y + radius * cos(phi) * sin(theta);
            float camZ = center.z + radius * sin(phi);

            PushConstants pc{};
            pc.minBounds[0] = b.minPos.x; pc.minBounds[1] = b.minPos.y; pc.minBounds[2] = b.minPos.z;
            pc.extent[0] = ext.x; pc.extent[1] = ext.y; pc.extent[2] = ext.z;
            pc.camPos[0] = camX; pc.camPos[1] = camY; pc.camPos[2] = camZ; 
            pc.camDir[0] = center.x; pc.camDir[1] = center.y; pc.camDir[2] = center.z;
            pc.camUp[0] = 0.0f; pc.camUp[1] = 0.0f; pc.camUp[2] = params.flipUp ? -1.0f : 1.0f;

            static int accFrame = 0;
            pc.frameCount = accFrame++; 
            
            vkCmdPushConstants(cb, computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pc);

            VkImageMemoryBarrier bar = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .srcAccessMask = 0, .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_GENERAL, .image = ctx->swapChainImages[ii], .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1} };
            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &bar);
            vkCmdDispatch(cb, (ctx->swapChainExtent.width + 15) / 16, (ctx->swapChainExtent.height + 15) / 16, 1);
            bar.oldLayout = VK_IMAGE_LAYOUT_GENERAL; bar.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; bar.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; bar.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &bar);

            if(uiCallback) uiCallback(cb, ii);
            vkEndCommandBuffer(cb);
        }
    };

    std::vector<RaytraceTriangle> write_in_order(const std::vector<CachedTriangle>& data, const std::vector<uint32_t>& indices) {
        std::vector<RaytraceTriangle> sorted; 
        sorted.reserve(indices.size());
        for (uint32_t idx : indices) { 
            const auto& ct = data[idx]; 
            sorted.push_back({ct.v1, ct.v2, ct.v3, ct.normal}); 
        }
        return sorted;
    }
}