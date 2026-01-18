module;
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYGLTF_NO_EXTERNAL_IMAGE 
#include <tiny_gltf.h>
#include <iostream>
#include <vector>

module Engine;

import Types;

namespace Core
{
    bool load_cache(const std::vector<Triangle>& triangles, Object& cache)
    {
        if (triangles.empty()) return false;

        cache.mesh.clear();
        cache.mesh.reserve(triangles.size());

        vec3 extent = sub(cache.bounds.maxPos, cache.bounds.minPos);

        if (extent.x < 1e-6f) extent.x = 1.0f;
        if (extent.y < 1e-6f) extent.y = 1.0f;
        if (extent.z < 1e-6f) extent.z = 1.0f;

        for (const auto& tri : triangles)
        {
            CachedTriangle ct;

            // 1. Normalize and quantize
            ct.v1 = quantize_position(tri.v1, cache.bounds.minPos, extent);
            ct.v2 = quantize_position(tri.v2, cache.bounds.minPos, extent);
            ct.v3 = quantize_position(tri.v3, cache.bounds.minPos, extent);

            // 2. Centroid
            vec3 center = div(add(add(tri.v1, tri.v2), tri.v3), 3.0f);
            ct.centroid = quantize_position(center, cache.bounds.minPos, extent);

            // 3. Bounds
            vec3 local_min = tri.v1;
            vec3 local_max = tri.v1;
            
            auto update_min_max = [&](const vec3& v) {
                if (v.x < local_min.x) local_min.x = v.x;
                if (v.y < local_min.y) local_min.y = v.y;
                if (v.z < local_min.z) local_min.z = v.z;
                if (v.x > local_max.x) local_max.x = v.x;
                if (v.y > local_max.y) local_max.y = v.y;
                if (v.z > local_max.z) local_max.z = v.z;
            };

            update_min_max(tri.v2);
            update_min_max(tri.v3);

            ct.min = quantize_position(local_min, cache.bounds.minPos, extent);
            ct.max = quantize_position(local_max, cache.bounds.minPos, extent);

            // 4. Normal
            vec3 edge1 = sub(tri.v2, tri.v1);
            vec3 edge2 = sub(tri.v3, tri.v1);
            vec3 normal = normalize(cross(edge1, edge2));
            ct.normal = encode_normal(normal);

            cache.mesh.push_back(ct);
        }

        std::cout << "Cached " << cache.mesh.size() << " triangles (Compressed)." << std::endl;
        return true;
    }

    bool load_bounds(const std::vector<Triangle>& triangles, MeshBounds& bounds)
    {
        if (triangles.empty()) return false;

        bounds.minPos = { std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
        bounds.maxPos = { std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };

        for (const auto& tri : triangles)
        {
            const vec3* verts[] = { &tri.v1, &tri.v2, &tri.v3 };
            for (const auto* v : verts)
            {
                if (v->x < bounds.minPos.x) bounds.minPos.x = v->x;
                if (v->y < bounds.minPos.y) bounds.minPos.y = v->y;
                if (v->z < bounds.minPos.z) bounds.minPos.z = v->z;

                if (v->x > bounds.maxPos.x) bounds.maxPos.x = v->x;
                if (v->y > bounds.maxPos.y) bounds.maxPos.y = v->y;
                if (v->z > bounds.maxPos.z) bounds.maxPos.z = v->z;
            }
        }
        return true;
    }

    bool load_mesh(const std::string& model_path, std::vector<Triangle>& out_triangles, MeshBounds& out_bounds)
    {
        // TODO: OPTIMIZATION: Implement multi-threaded loading for large meshes
        tinygltf::Model model;
        tinygltf::TinyGLTF loader;
        std::string err;
        std::string warn;

        bool ret = false;
        if (model_path.find(".glb") != std::string::npos) {
            ret = loader.LoadBinaryFromFile(&model, &err, &warn, model_path);
        } else {
            ret = loader.LoadASCIIFromFile(&model, &err, &warn, model_path);
        }

        if (!warn.empty()) std::cout << "TinyGLTF Warning: " << warn << std::endl;
        if (!err.empty()) std::cerr << "TinyGLTF Error: " << err << std::endl;
        if (!ret) {
            std::cerr << "Failed to parse glTF: " << model_path << std::endl;
            return false;
        }

        out_bounds.minPos = { std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
        out_bounds.maxPos = { std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };

        bool found_any_bounds = false;

        for (const auto& mesh : model.meshes)
        {
            for (const auto& primitive : mesh.primitives)
            {
                if (primitive.mode != TINYGLTF_MODE_TRIANGLES) continue;

                const auto& pos_accessor_it = primitive.attributes.find("POSITION");
                if (pos_accessor_it == primitive.attributes.end()) continue;

                const tinygltf::Accessor& pos_accessor = model.accessors[pos_accessor_it->second];
                
                // --- FAST BOUNDS EXTRACTION ---
                if (pos_accessor.minValues.size() == 3 && pos_accessor.maxValues.size() == 3)
                {
                    found_any_bounds = true;
                    if (pos_accessor.minValues[0] < out_bounds.minPos.x) out_bounds.minPos.x = (float)pos_accessor.minValues[0];
                    if (pos_accessor.minValues[1] < out_bounds.minPos.y) out_bounds.minPos.y = (float)pos_accessor.minValues[1];
                    if (pos_accessor.minValues[2] < out_bounds.minPos.z) out_bounds.minPos.z = (float)pos_accessor.minValues[2];

                    if (pos_accessor.maxValues[0] > out_bounds.maxPos.x) out_bounds.maxPos.x = (float)pos_accessor.maxValues[0];
                    if (pos_accessor.maxValues[1] > out_bounds.maxPos.y) out_bounds.maxPos.y = (float)pos_accessor.maxValues[1];
                    if (pos_accessor.maxValues[2] > out_bounds.maxPos.z) out_bounds.maxPos.z = (float)pos_accessor.maxValues[2];
                }

                const tinygltf::BufferView& pos_view = model.bufferViews[pos_accessor.bufferView];
                const tinygltf::Buffer& pos_buffer = model.buffers[pos_view.buffer];
                const float* positions = reinterpret_cast<const float*>(&pos_buffer.data[pos_view.byteOffset + pos_accessor.byteOffset]);

                if (primitive.indices >= 0)
                {
                    const tinygltf::Accessor& idx_accessor = model.accessors[primitive.indices];
                    const tinygltf::BufferView& idx_view = model.bufferViews[idx_accessor.bufferView];
                    const tinygltf::Buffer& idx_buffer = model.buffers[idx_view.buffer];
                    const unsigned char* idx_data = &idx_buffer.data[idx_view.byteOffset + idx_accessor.byteOffset];

                    for (size_t i = 0; i < idx_accessor.count; i += 3)
                    {
                        int i0, i1, i2;
                        if (idx_accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                            const uint16_t* indices = reinterpret_cast<const uint16_t*>(idx_data);
                            i0 = indices[i]; i1 = indices[i+1]; i2 = indices[i+2];
                        } else {
                            const uint* indices = reinterpret_cast<const uint*>(idx_data);
                            i0 = indices[i]; i1 = indices[i+1]; i2 = indices[i+2];
                        }
                        
                        vec3 v1 = { positions[i0 * 3], positions[i0 * 3 + 1], positions[i0 * 3 + 2] };
                        vec3 v2 = { positions[i1 * 3], positions[i1 * 3 + 1], positions[i1 * 3 + 2] };
                        vec3 v3 = { positions[i2 * 3], positions[i2 * 3 + 1], positions[i2 * 3 + 2] };

                        out_triangles.push_back(Triangle{ v1, v2, v3 });
                    }
                }
            }
        }

        if (!found_any_bounds) {
            out_bounds.minPos = {0,0,0};
            out_bounds.maxPos = {0,0,0};
        }
        return true;
    }
}