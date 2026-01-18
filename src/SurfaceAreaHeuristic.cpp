module;
#include <limits> 
#include <cmath>
#include <vector>
#include <algorithm> 
#include <iostream>
module Engine;

import Types;

namespace Core
{
    constexpr int BVH_BINS = 16;   
    constexpr int MAX_DEPTH = 32;  
    constexpr int MIN_TRIANGLES_PER_LEAF = 2;

    struct Bin
    {
        u16vec3 min = {65535, 65535, 65535};
        u16vec3 max = {0, 0, 0};
        uint count = 0;
    };

    float get_surface_area(const u16vec3& min, const u16vec3& max)
    {
        float w = static_cast<float>(max.x - min.x);
        float h = static_cast<float>(max.y - min.y);
        float d = static_cast<float>(max.z - min.z);
        return 2.0f * (w * h + w * d + h * d);
    }

    void grow_bounds(u16vec3& min, u16vec3& max, const u16vec3& p)
    {
        if (p.x < min.x) min.x = p.x;
        if (p.y < min.y) min.y = p.y;
        if (p.z < min.z) min.z = p.z;
        if (p.x > max.x) max.x = p.x;
        if (p.y > max.y) max.y = p.y;
        if (p.z > max.z) max.z = p.z;
    }
    int max_depth = 0;

    void split_bvh_node(uint nodeIdx, const Object& obj, std::vector<uint>& indices, std::vector<BVHNode>& nodes, int depth)
    {
        BVHNode& node = nodes[nodeIdx];
        node.pad1 = 0; 
        node.pad2 = 0;
        
        node.aabbMin = {65535, 65535, 65535};
        node.aabbMax = {0, 0, 0};
        for (uint i = 0; i < node.triCount; ++i) {
            const auto& tri = obj.mesh[indices[node.leftFirst + i]];
            grow_bounds(node.aabbMin, node.aabbMax, tri.min);
            grow_bounds(node.aabbMin, node.aabbMax, tri.max);
        }

        if (depth >= MAX_DEPTH || node.triCount <= MIN_TRIANGLES_PER_LEAF) {
            return;
        }
        if(depth > max_depth) max_depth = depth;

        u16vec3 cMin = {65535, 65535, 65535};
        u16vec3 cMax = {0, 0, 0};
        for (uint i = 0; i < node.triCount; ++i) {
            const auto& tri = obj.mesh[indices[node.leftFirst + i]];
            grow_bounds(cMin, cMax, tri.centroid);
        }

        int axis = 0;
        int extentX = cMax.x - cMin.x;
        int extentY = cMax.y - cMin.y;
        int extentZ = cMax.z - cMin.z;
        if (extentY > extentX) axis = 1;
        if (extentZ > extentX && extentZ > extentY) axis = 2;

        float axisExtent = (axis == 0) ? extentX : (axis == 1) ? extentY : extentZ;
        if (axisExtent < 1e-4f) return;

        Bin bins[BVH_BINS];
        float scale = BVH_BINS / (axisExtent + 0.1f);

        // TODO: OPTIMIZATION: This loop can be vectorized or parallelized
        for (uint i = 0; i < node.triCount; ++i) {
            const auto& tri = obj.mesh[indices[node.leftFirst + i]];
            int val = (axis == 0) ? tri.centroid.x : (axis == 1) ? tri.centroid.y : tri.centroid.z;
            int binIdx = std::min(BVH_BINS - 1, static_cast<int>((val - ((axis==0)?cMin.x:(axis==1)?cMin.y:cMin.z)) * scale));
            
            bins[binIdx].count++;
            grow_bounds(bins[binIdx].min, bins[binIdx].max, tri.min);
            grow_bounds(bins[binIdx].max, bins[binIdx].max, tri.max);
        }

        float leftArea[BVH_BINS - 1], rightArea[BVH_BINS - 1];
        int leftCount[BVH_BINS - 1], rightCount[BVH_BINS - 1];
        
        u16vec3 currentMin = {65535, 65535, 65535}, currentMax = {0, 0, 0};
        int currentCount = 0;
        for (int i = 0; i < BVH_BINS - 1; ++i) {
            currentCount += bins[i].count;
            if (bins[i].count > 0) {
                grow_bounds(currentMin, currentMax, bins[i].min);
                grow_bounds(currentMin, currentMax, bins[i].max);
            }
            leftArea[i] = get_surface_area(currentMin, currentMax);
            leftCount[i] = currentCount;
        }

        currentMin = {65535, 65535, 65535}; currentMax = {0, 0, 0};
        currentCount = 0;
        for (int i = BVH_BINS - 2; i >= 0; --i) {
            currentCount += bins[i+1].count;
             if (bins[i+1].count > 0) {
                grow_bounds(currentMin, currentMax, bins[i+1].min);
                grow_bounds(currentMin, currentMax, bins[i+1].max);
            }
            rightArea[i] = get_surface_area(currentMin, currentMax);
            rightCount[i] = currentCount;
        }

        float minCost = std::numeric_limits<float>::max();
        int splitIdx = -1;
        float parentArea = get_surface_area(node.aabbMin, node.aabbMax); 

        for (int i = 0; i < BVH_BINS - 1; ++i) {
            float cost = leftCount[i] * leftArea[i] + rightCount[i] * rightArea[i];
            if (cost < minCost) {
                minCost = cost;
                splitIdx = i;
            }
        }

        float leafCost = node.triCount * parentArea; 

        if (minCost >= leafCost) return; 

        auto it = std::partition(indices.begin() + node.leftFirst, indices.begin() + node.leftFirst + node.triCount,
            [&](uint idx) {
                const auto& tri = obj.mesh[idx];
                int val = (axis == 0) ? tri.centroid.x : (axis == 1) ? tri.centroid.y : tri.centroid.z;
                int binIdx = static_cast<int>((val - ((axis==0)?cMin.x:(axis==1)?cMin.y:cMin.z)) * scale);
                binIdx = std::min(BVH_BINS - 1, std::max(0, binIdx));
                return binIdx <= splitIdx;
            });

        uint leftCountFinal = std::distance(indices.begin() + node.leftFirst, it);
        
        if (leftCountFinal == 0 || leftCountFinal == node.triCount) return;

        uint currentLeftFirst = node.leftFirst;
        uint currentTriCount = node.triCount;

        // Creating children. Note: vector reallocation invalidates 'node' reference!
        uint leftChildIdx = static_cast<uint>(nodes.size());
        nodes.emplace_back();
        uint rightChildIdx = static_cast<uint>(nodes.size());
        nodes.emplace_back();

        nodes[nodeIdx].leftFirst = leftChildIdx; 
        nodes[nodeIdx].triCount = 0; 

        nodes[leftChildIdx].leftFirst = currentLeftFirst;
        nodes[leftChildIdx].triCount = leftCountFinal;
        
        nodes[rightChildIdx].leftFirst = currentLeftFirst + leftCountFinal;
        nodes[rightChildIdx].triCount = currentTriCount - leftCountFinal;

        split_bvh_node(leftChildIdx, obj, indices, nodes, depth + 1);
        split_bvh_node(rightChildIdx, obj, indices, nodes, depth + 1);
    }

    void build_bvh(const Object& obj, std::vector<uint>& out_indices, std::vector<BVHNode>& out_nodes)
    {
        max_depth = 0;
        if (obj.mesh.empty()) return;

        out_indices.resize(obj.mesh.size());
        for (size_t i = 0; i < obj.mesh.size(); ++i) {
            out_indices[i] = static_cast<uint>(i);
        }

        out_nodes.clear();
        out_nodes.reserve(obj.mesh.size() * 2); 
        out_nodes.emplace_back();
        
        BVHNode& root = out_nodes[0];
        root.leftFirst = 0;
        root.triCount = static_cast<uint>(obj.mesh.size());

        split_bvh_node(0, obj, out_indices, out_nodes, 0);

        std::cout << "BVH Generated: " << out_nodes.size() << " nodes, with " << max_depth << " depth." << std::endl;
    }
}