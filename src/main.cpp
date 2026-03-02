#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
#include <iostream>
#include <vector>
#include <future>
#include <atomic>
#include <cstring>
#include <algorithm>
#include <functional>

import Engine;
import Types;
import Window;
import ShaderController;
import UI;

bool frameAccumulationReset = true;
bool lastSpaceState = false;

int main() {
    // RAII managed Core components
    Render::WindowManager windowMgr;
    windowMgr.init_vulkan();

    UI::UIManager uiMgr;
    uiMgr.init(&windowMgr.ctx);

    Render::ShaderController shaderCtrl;
    shaderCtrl.init(&windowMgr.ctx);

    MeshBounds meshBounds;

    std::future<bool> loadingFuture;
    struct PendingModelData {
        Object obj;
        std::vector<Triangle> triangles;
        std::vector<RaytraceTriangle> gpu_triangles;
        std::vector<BVHNode> nodes;
        std::vector<uint> indices;
    } pendingData;
    
    std::atomic<bool> isLoading{false};

    auto load_model_task = [&](std::string path) -> bool {
        std::cout << "[Loader] Thread started for: " << path << std::endl;
        if (!Core::load_mesh(path, pendingData.triangles, pendingData.obj.bounds)) {
            std::cerr << "[Loader] Failed to load mesh.\n";
            return false;
        }

        if(pendingData.obj.bounds == MeshBounds({0,0,0},{0,0,0}))
            Core::load_bounds(pendingData.triangles, pendingData.obj.bounds);

        Core::load_cache(pendingData.triangles, pendingData.obj);
        Core::build_bvh(pendingData.obj, pendingData.indices, pendingData.nodes);
        pendingData.gpu_triangles = Render::write_in_order(pendingData.obj.mesh, pendingData.indices);
        
        return true;
    };

    if (load_model_task(uiMgr.settings.modelPath)) {
         meshBounds = pendingData.obj.bounds;
         shaderCtrl.reload_buffers(pendingData.gpu_triangles, pendingData.nodes);
         std::cout << "[Loader] Initial load complete.\n";

         vec3 ext = sub(meshBounds.maxPos, meshBounds.minPos);
         float maxDim = std::max({ext.x, ext.y, ext.z});
         if (maxDim < 0.1f) maxDim = 5.0f;
         uiMgr.settings.camDistance = maxDim;
         
         pendingData.triangles.clear();
         pendingData.gpu_triangles.clear();
         pendingData.nodes.clear();
         pendingData.indices.clear();
         pendingData.obj.mesh.clear();
    }

    std::cout << "Starting Main Loop...\n";
    
    while (!glfwWindowShouldClose(windowMgr.ctx.window)) {
        glfwPollEvents();
        
        if (windowMgr.ctx.framebufferResized) {
            windowMgr.ctx.framebufferResized = false;
            windowMgr.recreate_swapchain();
            uiMgr.on_resize();
            shaderCtrl.on_resize();
            
            std::cout << "[Window] Resized to " 
                      << windowMgr.ctx.swapChainExtent.width << "x" 
                      << windowMgr.ctx.swapChainExtent.height << "\n";
            continue; 
        }

        bool currentSpaceState = glfwGetKey(windowMgr.ctx.window, GLFW_KEY_SPACE) == GLFW_PRESS;
        if (currentSpaceState && !lastSpaceState) {
            uiMgr.settings.manualCamera = !uiMgr.settings.manualCamera;
            uiMgr.settings.mouseCaptured = false;
            std::cout << "Camera Mode: " << (uiMgr.settings.manualCamera ? "Manual (Mouse)" : "Animation") << "\n";
        }
        lastSpaceState = currentSpaceState;

        if (uiMgr.settings.manualCamera) {
            if (glfwGetMouseButton(windowMgr.ctx.window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
                if (!uiMgr.settings.mouseCaptured && uiMgr.wants_capture_mouse()) {
                    // Do nothing - ImGui captured
                } else {
                    double xpos, ypos;
                    glfwGetCursorPos(windowMgr.ctx.window, &xpos, &ypos);

                    if (!uiMgr.settings.mouseCaptured) {
                        uiMgr.settings.lastMouseX = (float)xpos;
                        uiMgr.settings.lastMouseY = (float)ypos;
                        uiMgr.settings.mouseCaptured = true;
                    }

                    float xoffset = (float)xpos - uiMgr.settings.lastMouseX;
                    float yoffset = uiMgr.settings.lastMouseY - (float)ypos; 

                    uiMgr.settings.lastMouseX = (float)xpos;
                    uiMgr.settings.lastMouseY = (float)ypos;

                    float sensitivity = 0.01f;
                    uiMgr.settings.camAzimuth -= xoffset * sensitivity;
                    uiMgr.settings.camElevation -= yoffset * sensitivity; 

                    if (uiMgr.settings.camElevation > 1.5f) uiMgr.settings.camElevation = 1.5f;
                    if (uiMgr.settings.camElevation < -1.5f) uiMgr.settings.camElevation = -1.5f;
                }
            } else {
                uiMgr.settings.mouseCaptured = false;
            }
        } else {
            uiMgr.settings.camAzimuth = (float)glfwGetTime() * 0.5f;
        }
        
        if (uiMgr.settings.loadModelTriggered && !isLoading) {
            uiMgr.settings.loadModelTriggered = false; 
            isLoading = true;
            loadingFuture = std::async(std::launch::async, load_model_task, std::string(uiMgr.settings.modelPath));
        }

        if (isLoading && loadingFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            bool success = loadingFuture.get(); 
            isLoading = false;

            if (success) {
                vkDeviceWaitIdle(windowMgr.ctx.device);
                
                shaderCtrl.reload_buffers(pendingData.gpu_triangles, pendingData.nodes);
                meshBounds = pendingData.obj.bounds;

                vec3 ext = sub(meshBounds.maxPos, meshBounds.minPos);
                float maxDim = std::max({ext.x, ext.y, ext.z});
                if (maxDim < 0.1f) maxDim = 5.0f;
                
                uiMgr.settings.camDistance = maxDim; 
                if (!uiMgr.settings.manualCamera) uiMgr.settings.camElevation = .5f; 
                
                std::cout << "[Loader] GPU Upload complete. Resume rendering.\n";
            }
            
            pendingData.triangles.clear();
            pendingData.gpu_triangles.clear();
            pendingData.nodes.clear();
            pendingData.indices.clear();
            pendingData.obj.mesh.clear();
        }

        // Bridge UI settings to Shader Rendering completely decoupled
        Render::RenderParams params;
        params.camAzimuth = uiMgr.settings.camAzimuth;
        params.camElevation = uiMgr.settings.camElevation;
        params.camDistance = uiMgr.settings.camDistance;
        params.flipUp = uiMgr.settings.flipUp;
        params.maxBounces = uiMgr.settings.maxBounces;
        memcpy(params.light1Color, uiMgr.settings.light1Color, sizeof(float)*3);
        memcpy(params.light2Color, uiMgr.settings.light2Color, sizeof(float)*3);
        memcpy(params.light1Pos, uiMgr.settings.light1Pos, sizeof(float)*3);
        memcpy(params.light2Pos, uiMgr.settings.light2Pos, sizeof(float)*3);

        shaderCtrl.draw_frame(meshBounds, params, [&](VkCommandBuffer cb, uint32_t ii) {
            uiMgr.render(cb, ii);
        }); 
    }

    // Explicitly wait for idle before destructors fire
    vkDeviceWaitIdle(windowMgr.ctx.device); 

    // RAII takes care of all memory freeing and destruction natively here
    return 0;
}