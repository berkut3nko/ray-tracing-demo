module;
#include <cstdint>
#include <string>
#include <vector>
#include <ostream>
#include <cmath>
#include <iostream>
#include <cstdlib>
#include <source_location>
export module Types;

export using uint = uint32_t;
using ushort = unsigned short;
using std::string, std::vector;

// --- Error Handling Helper ---
export void check(bool condition, const char* errorMessage, std::source_location loc = std::source_location::current()) {
    if (!condition) {
        std::cerr << "[FATAL ERROR] " << errorMessage << "\n"
                  << "File: " << loc.file_name() << ":" << loc.line() << "\n";
        std::abort();
    }
}

export struct vec3
{
    float x,y,z;
};//12 byte

export struct vec4
{
    float x,y,z,w;
};

export struct u16vec3
{
    ushort x,y,z;
};//6 byte

export std::ostream& operator<<(std::ostream& out, const u16vec3& vec)
{
    out << "{" << vec.x << ", " << vec.y << ", " << vec.z << "}";
    return out;
}

export struct Triangle 
{
    vec3 v1,v2,v3;
};//36 byte only CPU

// Structure for BVH generation
export struct ComputeTriangle
{
    u16vec3 min, max;
    u16vec3 centroid;
    uint id;
    ushort padding;
};//22 byte + 2 padding

// GPU representation for raytracing
export struct RaytraceTriangle
{
    u16vec3 v1,v2,v3;
    u16vec3 normal;
};//24 byte

// Intermediate cached structure
export struct CachedTriangle
{
    u16vec3 v1,v2,v3;
    u16vec3 centroid;
    u16vec3 min,max;
    u16vec3 normal;
};//only CPU 18 + 6 + 12 + 6 = 38 byte

export struct MeshBounds
{
    vec3 minPos;
    vec3 maxPos;
};//24 byte

export std::ostream& operator<<(std::ostream& out, const MeshBounds& bound)
{
    out << "{" << bound.minPos.x << ", " << bound.minPos.y << ", " << bound.minPos.z << "} "
        << "{" << bound.maxPos.x << ", " << bound.maxPos.y << ", " << bound.maxPos.z << "}";
    return out;
}

export bool operator==(const MeshBounds& a, const MeshBounds& b)
{
    return (a.minPos.x == b.minPos.x && a.minPos.y == b.minPos.y && a.minPos.z == b.minPos.z &&
            a.maxPos.x == b.maxPos.x && a.maxPos.y == b.maxPos.y && a.maxPos.z == b.maxPos.z);
}

// TODO: OPTIMIZATION: Pack this structure tighter if possible, or align to 16 bytes for GPU fetch efficiency.
export struct BVHNode
{
    u16vec3 aabbMin;
    unsigned short pad1; 
    
    u16vec3 aabbMax;
    unsigned short pad2; 
    
    uint32_t leftFirst;
    uint32_t triCount;
}; // 24 bytes

export struct Object
{
    MeshBounds bounds;
    vector<CachedTriangle> mesh;
};

// --- Scene Settings UBO ---
// Aligned to std140 (vec4 = 16 bytes)
export struct SceneSettingsUBO
{
    vec4 light1Color; // RGB, w unused
    vec4 light2Color; // RGB, w unused
    vec4 light1Pos;   // XYZ relative (0-1), w unused
    vec4 light2Pos;   // XYZ relative (0-1), w unused
    int maxBounces;
    int padding[3];   // Pad to 16 bytes alignment
};


// --- Math Helpers ---

export vec3 sub(const vec3& a, const vec3& b) {
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}

export vec3 add(const vec3& a, const vec3& b) {
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}

export vec3 div(const vec3& a, float f) {
    if (f == 0.0f) return { 0, 0, 0 };
    return { a.x / f, a.y / f, a.z / f };
}

export vec3 cross(const vec3& a, const vec3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

export vec3 normalize(vec3 v) {
    float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len < 1e-6f) return { 0, 0, 0 };
    return { v.x / len, v.y / len, v.z / len };
}

// Helper to quantize a 3D float position into u16 based on bounds
export u16vec3 quantize_position(const vec3& p, const vec3& min_b, const vec3& extent)
{
    float nx = (p.x - min_b.x) / extent.x;
    float ny = (p.y - min_b.y) / extent.y;
    float nz = (p.z - min_b.z) / extent.z;

    nx = (nx < 0.0f) ? 0.0f : (nx > 1.0f) ? 1.0f : nx;
    ny = (ny < 0.0f) ? 0.0f : (ny > 1.0f) ? 1.0f : ny;
    nz = (nz < 0.0f) ? 0.0f : (nz > 1.0f) ? 1.0f : nz;

    return {
        static_cast<unsigned short>(nx * 65535.0f),
        static_cast<unsigned short>(ny * 65535.0f),
        static_cast<unsigned short>(nz * 65535.0f)
    };
}

export u16vec3 encode_normal(const vec3& n)
{
    float nx = (n.x + 1.0f) * 0.5f;
    float ny = (n.y + 1.0f) * 0.5f;
    float nz = (n.z + 1.0f) * 0.5f;

    nx = (nx < 0.0f) ? 0.0f : (nx > 1.0f) ? 1.0f : nx;
    ny = (ny < 0.0f) ? 0.0f : (ny > 1.0f) ? 1.0f : ny;
    nz = (nz < 0.0f) ? 0.0f : (nz > 1.0f) ? 1.0f : nz;

    return {
        static_cast<unsigned short>(nx * 65535.0f),
        static_cast<unsigned short>(ny * 65535.0f),
        static_cast<unsigned short>(nz * 65535.0f)
    };
}