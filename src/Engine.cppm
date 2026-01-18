module;
#include <iostream>
#include <vector>
export module Engine;

import Types;

export namespace Core
{
    bool load_mesh(const std::string& model_path, std::vector<Triangle>& out_triangles, MeshBounds& out_bounds);

    bool load_bounds(const std::vector<Triangle>& triangles, MeshBounds& bounds);

    bool load_cache(const std::vector<Triangle>& triangles, Object& cache);

    void build_bvh(const Object& obj, std::vector<uint>& out_indices, std::vector<BVHNode>& out_nodes);
}