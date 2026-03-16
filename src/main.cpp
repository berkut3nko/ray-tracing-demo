#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
#include <iostream>
#include <vector>
#include <future>
#include <atomic>
#include <cstring>
#include <algorithm>
#include <functional>
#include <thread>
#include <chrono>

import Engine;
import Types;
import Window;
import ShaderController;
import UI;

/**
 * @brief Abstracts the background loading of meshes and BVH building.
 * Ensures the main loop does not tightly couple with Core structures.
 */
class AsyncModelLoader {
public:
    struct Result {
        MeshBounds bounds;
        std::vector<RaytraceTriangle> gpu_triangles;
        std::vector<BVHNode> nodes;
        bool success = false;
    };

    void start_loading(const std::string& path) {
        if (isLoading) return;
        isLoading = true;
        loadingFuture = std::async(std::launch::async, [path]() {
            return load_internal(path);
        });
    }

    bool check_finished(Result& out_result) {
        if (isLoading && loadingFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            out_result = loadingFuture.get();
            isLoading = false;
            return true;
        }
        return false;
    }

    bool is_loading() const { return isLoading; }

private:
    std::atomic<bool> isLoading{false};
    std::future<Result> loadingFuture;

    static Result load_internal(const std::string& path) {
        Result res;
        Object obj;
        std::vector<Triangle> triangles;
        std::vector<uint> indices;

        std::cout << "[Loader] Thread started for: " << path << std::endl;
        if (!Core::load_mesh(path, triangles, obj.bounds)) {
            std::cerr << "[Loader] Failed to load mesh.\n";
            return res;
        }

        if(obj.bounds == MeshBounds({0,0,0},{0,0,0}))
            Core::load_bounds(triangles, obj.bounds);

        Core::load_cache(triangles, obj);
        Core::build_bvh(obj, indices, res.nodes);
        res.gpu_triangles = Render::write_in_order(obj.mesh, indices);
        res.bounds = obj.bounds;
        res.success = true;
        
        return res;
    }
};

bool lastSpaceState = false;

int main() {
    Render::WindowManager windowMgr;
    windowMgr.init_vulkan();

    UI::UIManager uiMgr;
    uiMgr.init(windowMgr.get_context());

    Render::ShaderController shaderCtrl;
    shaderCtrl.init(windowMgr.get_context());

    MeshBounds meshBounds;
    AsyncModelLoader modelLoader;

    // Initial sync-like loading screen abstraction
    modelLoader.start_loading(uiMgr.settings.modelPath);
    AsyncModelLoader::Result loadedData;
    
    while (!modelLoader.check_finished(loadedData)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (loadedData.success) {
        meshBounds = loadedData.bounds;
        shaderCtrl.reload_buffers(loadedData.gpu_triangles, loadedData.nodes);
        std::cout << "[Loader] Initial load complete.\n";

        vec3 ext = sub(meshBounds.maxPos, meshBounds.minPos);
        float maxDim = std::max({ext.x, ext.y, ext.z});
        if (maxDim < 0.1f) maxDim = 5.0f;
        uiMgr.settings.camDistance = maxDim;
    }

    std::cout << "Starting Main Loop...\n";
    
    while (!glfwWindowShouldClose(windowMgr.get_context()->window)) {
        glfwPollEvents();
        
        if (windowMgr.get_context()->framebufferResized) {
            windowMgr.get_context()->framebufferResized = false;
            windowMgr.recreate_swapchain();
            uiMgr.on_resize();
            shaderCtrl.on_resize();
            
            std::cout << "[Window] Resized to " 
                      << windowMgr.get_context()->swapChainExtent.width << "x" 
                      << windowMgr.get_context()->swapChainExtent.height << "\n";
            continue; 
        }

        bool currentSpaceState = glfwGetKey(windowMgr.get_context()->window, GLFW_KEY_SPACE) == GLFW_PRESS;
        if (currentSpaceState && !lastSpaceState) {
            uiMgr.settings.manualCamera = !uiMgr.settings.manualCamera;
            uiMgr.settings.mouseCaptured = false;
            std::cout << "Camera Mode: " << (uiMgr.settings.manualCamera ? "Manual (Mouse)" : "Animation") << "\n";
        }
        lastSpaceState = currentSpaceState;

        if (uiMgr.settings.manualCamera) {
            if (glfwGetMouseButton(windowMgr.get_context()->window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
                if (!uiMgr.settings.mouseCaptured && uiMgr.wants_capture_mouse()) {
                    // Do nothing - ImGui captured the mouse
                } else {
                    double xpos, ypos;
                    glfwGetCursorPos(windowMgr.get_context()->window, &xpos, &ypos);

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
        
        if (uiMgr.settings.loadModelTriggered && !modelLoader.is_loading()) {
            uiMgr.settings.loadModelTriggered = false; 
            modelLoader.start_loading(uiMgr.settings.modelPath);
        }

        if (modelLoader.check_finished(loadedData)) {
            if (loadedData.success) {
                vkDeviceWaitIdle(windowMgr.get_context()->device);
                
                shaderCtrl.reload_buffers(loadedData.gpu_triangles, loadedData.nodes);
                meshBounds = loadedData.bounds;

                vec3 ext = sub(meshBounds.maxPos, meshBounds.minPos);
                float maxDim = std::max({ext.x, ext.y, ext.z});
                if (maxDim < 0.1f) maxDim = 5.0f;
                
                uiMgr.settings.camDistance = maxDim; 
                if (!uiMgr.settings.manualCamera) uiMgr.settings.camElevation = .5f; 
                
                std::cout << "[Loader] GPU Upload complete. Resume rendering.\n";
            }
        }

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

    vkDeviceWaitIdle(windowMgr.get_context()->device); 

    return 0;
}