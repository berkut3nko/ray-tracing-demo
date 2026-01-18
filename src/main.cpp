#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
#include <iostream>
#include <vector>
#include <future> // For async loading
#include <atomic>
#include <cstring> // For memcmp/memcpy
#include <algorithm> // For max(list)

import Engine;
import Types;
import Window;
import ShaderController;
import UI;

// Global flag to track if we need to reset the accumulation buffer (e.g., camera moved)
bool frameAccumulationReset = true;
// Helper to detect key toggle
bool lastSpaceState = false;

int main() {
    // ---------------------------------------------------------
    // VULKAN INIT
    // ---------------------------------------------------------
    // Exceptions are removed in favor of explicit abort() in modules
    Render::init_vulkan();
    UI::init();

    // ---------------------------------------------------------
    // DATA CONTAINERS
    // ---------------------------------------------------------
    MeshBounds meshBounds;

    // --- ASYNC LOADING STATE ---
    // Future object to hold the result of the background loading task
    std::future<bool> loadingFuture;
    
    // Temporary containers to store data while loading in background
    struct PendingModelData {
        Object obj;
        std::vector<Triangle> triangles; // Added missing field
        std::vector<RaytraceTriangle> gpu_triangles;
        std::vector<BVHNode> nodes;
        std::vector<uint> indices;
    } pendingData;
    
    std::atomic<bool> isLoading{false};

    // Helper lambda for loading logic (Now designed to run on a separate thread)
    auto load_model_task = [&](std::string path) -> bool {
        std::cout << "[Loader] Thread started for: " << path << std::endl;
        
        // 1. Load GLTF/GLB (Heavy IO)
        if (!Core::load_mesh(path, pendingData.triangles, pendingData.obj.bounds)) {
            std::cerr << "[Loader] Failed to load mesh.\n";
            return false;
        }

        // 2. Calc Bounds (Fast)
        if(pendingData.obj.bounds == MeshBounds({0,0,0},{0,0,0}))
            Core::load_bounds(pendingData.triangles, pendingData.obj.bounds);

        // 3. Cache & Quantize (CPU Heavy)
        Core::load_cache(pendingData.triangles, pendingData.obj);
        
        // 4. Build BVH (Very CPU Heavy - O(N log N))
        Core::build_bvh(pendingData.obj, pendingData.indices, pendingData.nodes);
        
        // Prepare GPU format data
        pendingData.gpu_triangles = Render::write_in_order(pendingData.obj.mesh, pendingData.indices);
        
        return true;
    };

    // Initial Load (Synchronous for the first start)
    if (load_model_task(UI::settings.modelPath)) {
         meshBounds = pendingData.obj.bounds;
         Render::reload_buffers(pendingData.gpu_triangles, pendingData.nodes);
         std::cout << "[Loader] Initial load complete.\n";

         vec3 ext = sub(meshBounds.maxPos, meshBounds.minPos);
         float maxDim = std::max({ext.x, ext.y, ext.z});
         if (maxDim < 0.1f) maxDim = 5.0f;
         UI::settings.camDistance = maxDim;
         // -------------------------
         
         // Clear RAM used for loading immediately after upload
         pendingData.triangles.clear();
         pendingData.gpu_triangles.clear();
         pendingData.nodes.clear();
         pendingData.indices.clear();
         pendingData.obj.mesh.clear();
    }

    // ---------------------------------------------------------
    // SHADER INIT
    // ---------------------------------------------------------
    Render::shader_init();

    // ---------------------------------------------------------
    // MAIN LOOP
    // ---------------------------------------------------------
    std::cout << "Starting Main Loop...\n";
    
    while (!glfwWindowShouldClose(Render::window)) {
        glfwPollEvents();
        
        // Handle Resize
        if (Render::framebufferResized) {
            Render::framebufferResized = false;
            
            // 1. Recreate Swapchain (Vulkan Core)
            Render::recreate_swapchain();
            
            // 2. Recreate Framebuffers (UI)
            UI::on_resize();
            
            // 3. Update Descriptors (Shader Resource Binding)
            Render::on_resize();
            
            std::cout << "[Window] Resized to " 
                      << Render::swapChainExtent.width << "x" 
                      << Render::swapChainExtent.height << "\n";
            
            // Skip drawing this frame to prevent validation errors during transition
            continue; 
        }

        // 1. Toggle Mode (Spacebar)
        bool currentSpaceState = glfwGetKey(Render::window, GLFW_KEY_SPACE) == GLFW_PRESS;
        if (currentSpaceState && !lastSpaceState) {
            UI::settings.manualCamera = !UI::settings.manualCamera;
            UI::settings.mouseCaptured = false;
            std::cout << "Camera Mode: " << (UI::settings.manualCamera ? "Manual (Mouse)" : "Animation") << "\n";
        }
        lastSpaceState = currentSpaceState;

        // 2. Mouse Control
        if (UI::settings.manualCamera) {
            if (glfwGetMouseButton(Render::window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
                // LOCK FIX: Only allow starting interaction if UI doesn't want mouse
                if (!UI::settings.mouseCaptured && UI::wants_capture_mouse()) {
                    // Do nothing - let ImGui handle the input
                } else {
                    double xpos, ypos;
                    glfwGetCursorPos(Render::window, &xpos, &ypos);

                    if (!UI::settings.mouseCaptured) {
                        UI::settings.lastMouseX = (float)xpos;
                        UI::settings.lastMouseY = (float)ypos;
                        UI::settings.mouseCaptured = true;
                    }

                    float xoffset = (float)xpos - UI::settings.lastMouseX;
                    float yoffset = UI::settings.lastMouseY - (float)ypos; // Invert Y

                    UI::settings.lastMouseX = (float)xpos;
                    UI::settings.lastMouseY = (float)ypos;

                    float sensitivity = 0.01f;
                    UI::settings.camAzimuth -= xoffset * sensitivity;
                    UI::settings.camElevation -= yoffset * sensitivity; // Standard orbit

                    // Clamp elevation to avoid gimbal lock or flipping
                    if (UI::settings.camElevation > 1.5f) UI::settings.camElevation = 1.5f;
                    if (UI::settings.camElevation < -1.5f) UI::settings.camElevation = -1.5f;
                }
            } else {
                UI::settings.mouseCaptured = false;
            }
        } else {
            // Animation Mode: Update azimuth based on time
            UI::settings.camAzimuth = (float)glfwGetTime() * 0.5f;
        }
        
        // 3. Check for Hot Reload Request from UI
        if (UI::settings.loadModelTriggered && !isLoading) {
            UI::settings.loadModelTriggered = false; 
            isLoading = true;
            
            // Launch loading in a separate thread
            // We pass modelPath by value to avoid race conditions if UI changes it immediately
            loadingFuture = std::async(std::launch::async, load_model_task, std::string(UI::settings.modelPath));
        }

        // 4. Check if Async Loading Finished
        if (isLoading && loadingFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            bool success = loadingFuture.get(); // Get result
            isLoading = false;

            if (success) {
                // STOP THE WORLD - SAFE ZONE FOR GPU UPLOAD
                // We still need to wait for idle to destroy old buffers safely 
                // (unless we implement sophisticated double-buffered resource management)
                vkDeviceWaitIdle(Render::device);
                
                // Upload new data to GPU
                Render::reload_buffers(pendingData.gpu_triangles, pendingData.nodes);
                meshBounds = pendingData.obj.bounds;

                vec3 ext = sub(meshBounds.maxPos, meshBounds.minPos);
                float maxDim = std::max({ext.x, ext.y, ext.z});
                if (maxDim < 0.1f) maxDim = 5.0f;
                
                // Set distance relative to object size
                UI::settings.camDistance = maxDim; 
                
                // Reset angles for a nice initial view
                if (!UI::settings.manualCamera) {
                    UI::settings.camElevation = .5f; 
                }
                
                std::cout << "[Loader] GPU Upload complete. Resume rendering.\n";
            }
            
            // Clear pending data to free RAM
            pendingData.triangles.clear();
            pendingData.gpu_triangles.clear();
            pendingData.nodes.clear();
            pendingData.indices.clear();
            pendingData.obj.mesh.clear();
        }

        // 4. Draw
        Render::draw_frame(meshBounds); 
    }

    // ---------------------------------------------------------
    // CLEANUP
    // ---------------------------------------------------------
    vkDeviceWaitIdle(Render::device); 
    
    UI::cleanup();
    Render::cleanup();

    return 0;
}