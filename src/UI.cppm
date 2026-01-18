module;
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

#include <vector>
#include <iostream>
#include <cstring>
#include <cstdlib> 
export module UI;

import Window;
import Types;

export namespace UI
{
    // Application Settings
    struct Settings {
        int maxBounces = 2;
        float light1Color[3] = {0.851f, 0.7569f, 0.5412f};
        float light2Color[3] = {0.3294f, 0.451f, 0.4706f};
        float light1Pos[3] = {0.0f, 0.0f, 0.0f}; // relative 0.0-1.0
        float light2Pos[3] = {1.0f, 1.0f, 1.0f}; // relative 0.0-1.0
        
        char modelPath[512] = "/home/berkut3nko/Downloads/frank.glb";
        bool loadModelTriggered = false;
        // Camera Controls
        bool manualCamera = false;
        float camAzimuth = 0.0f;
        float camElevation = 0.5f;
        float camDistance = 5.0f;
        
        // Input state helpers
        bool mouseCaptured = false;
        float lastMouseX = 0.0f;
        float lastMouseY = 0.0f;

        // Orientation Settings 
        bool invertY = false;
        bool flipUp = false; 
    } settings;

    VkDescriptorPool imguiPool;
    VkRenderPass renderPass;
    std::vector<VkFramebuffer> framebuffers;

    // Helper to check ImGui state 
    bool wants_capture_mouse() {
        return ImGui::GetIO().WantCaptureMouse;
    }

    // Custom Vulkan error check callback for ImGui
    void check_vk_result(VkResult err) {
        if (err == 0) return;
        std::cerr << "[ImGui Vulkan] Error: VkResult = " << err << std::endl;
        if (err < 0) std::abort();
    }

    void create_render_pass() {
        VkAttachmentDescription attachment = {
            .format = Render::swapChainImageFormat,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD, // Don't clear, draw on top
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
        };

        VkAttachmentReference color_attachment = {
            .attachment = 0,
            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        };

        VkSubpassDescription subpass = {
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = 1,
            .pColorAttachments = &color_attachment
        };

        VkSubpassDependency dependency = {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
        };

        VkRenderPassCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments = &attachment,
            .subpassCount = 1,
            .pSubpasses = &subpass,
            .dependencyCount = 1,
            .pDependencies = &dependency
        };

        check(vkCreateRenderPass(Render::device, &info, nullptr, &renderPass) == VK_SUCCESS, "Could not create ImGui render pass");
    }

    void create_framebuffers() {
        framebuffers.resize(Render::swapChainImageViews.size());
        for (size_t i = 0; i < Render::swapChainImageViews.size(); i++) {
            VkImageView attachment[] = { Render::swapChainImageViews[i] };
            VkFramebufferCreateInfo info = {
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                .renderPass = renderPass,
                .attachmentCount = 1,
                .pAttachments = attachment,
                .width = Render::swapChainExtent.width,
                .height = Render::swapChainExtent.height,
                .layers = 1
            };

            check(vkCreateFramebuffer(Render::device, &info, nullptr, &framebuffers[i]) == VK_SUCCESS, "Could not create ImGui framebuffer");
        }
    }

    // --- NEW: Resize Helper ---
    void on_resize() {
        for (auto fb : framebuffers) {
            vkDestroyFramebuffer(Render::device, fb, nullptr);
        }
        create_framebuffers();
    }

    void init() {
        VkDescriptorPoolSize pool_sizes[] = {
            { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
        };

        VkDescriptorPoolCreateInfo pool_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
            .maxSets = 1000,
            .poolSizeCount = std::size(pool_sizes),
            .pPoolSizes = pool_sizes
        };

        check(vkCreateDescriptorPool(Render::device, &pool_info, nullptr, &imguiPool) == VK_SUCCESS, "Could not create ImGui descriptor pool");

        create_render_pass();
        create_framebuffers();

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; 

        ImGui::StyleColorsDark();

        ImGui_ImplGlfw_InitForVulkan(Render::window, true);

        ImGui_ImplVulkan_InitInfo init_info = {
            .Instance = Render::instance,
            .PhysicalDevice = Render::physicalDevice,
            .Device = Render::device,
            .QueueFamily = Render::computeQueueFamilyIndex,
            .Queue = Render::computeQueue,
            .DescriptorPool = imguiPool,
            .RenderPass = renderPass,
            .MinImageCount = 2,
            .ImageCount = static_cast<uint32_t>(Render::swapChainImages.size()),
            .CheckVkResultFn = check_vk_result
        };
        
        ImGui_ImplVulkan_Init(&init_info);

        // Upload fonts using built-in command buffer handling
        ImGui_ImplVulkan_CreateFontsTexture();
    }

    void cleanup() {
        vkDeviceWaitIdle(Render::device);
        
        ImGui_ImplVulkan_DestroyFontsTexture();
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        for (auto fb : framebuffers) vkDestroyFramebuffer(Render::device, fb, nullptr);
        vkDestroyRenderPass(Render::device, renderPass, nullptr);
        vkDestroyDescriptorPool(Render::device, imguiPool, nullptr);
    }

    void render(VkCommandBuffer cb, uint32_t imageIndex) {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        {
            ImGui::Begin("Settings");
            
            if (ImGui::CollapsingHeader("Scene Control", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Text("Model Path (.glb/.gltf):");
                ImGui::InputText("##Path", settings.modelPath, 512);
                if (ImGui::Button("Load Model & Rebuild BVH")) {
                    settings.loadModelTriggered = true;
                }
                
                // Orientation Controls
                ImGui::Separator();
                ImGui::Text("Camera Orientation Fixes:");
                if (ImGui::Checkbox("Invert Y Axis (Mirror)", &settings.invertY)) {
                   // Optional: reset accumulation if needed, though camera logic usually handles it via movement
                }
                ImGui::Checkbox("Flip Camera Up (For Upside Down Models)", &settings.flipUp);
            }

            if (ImGui::CollapsingHeader("Raytracing Config", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::SliderInt("Max Bounces", &settings.maxBounces, 0, 10);
            }
            
            if (ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Text("Light 1");
                ImGui::ColorEdit3("Color 1", settings.light1Color);
                ImGui::SliderFloat3("Pos 1 (Rel)", settings.light1Pos, 0.0f, 1.0f);
                
                ImGui::Separator();
                ImGui::Text("Light 2");
                ImGui::ColorEdit3("Color 2", settings.light2Color);
                ImGui::SliderFloat3("Pos 2 (Rel)", settings.light2Pos, 0.0f, 1.0f);
            }

            ImGui::Separator();
            ImGui::Text("Avg: %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
            ImGui::End();
        }

        ImGui::Render();

        VkRenderPassBeginInfo info = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = renderPass,
            .framebuffer = framebuffers[imageIndex],
            .renderArea = { .extent = Render::swapChainExtent },
            .clearValueCount = 0
        };
        
        vkCmdBeginRenderPass(cb, &info, VK_SUBPASS_CONTENTS_INLINE);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cb);
        vkCmdEndRenderPass(cb);
    }
}