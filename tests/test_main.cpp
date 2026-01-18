#include <gtest/gtest.h>
#include <vector>
#include <cmath>

// Import your modules
import Types;
import Engine;

// --- Test Math Helpers (Types.cppm) ---

TEST(MathTests, VectorAddition) {
    vec3 a = {1.0f, 2.0f, 3.0f};
    vec3 b = {4.0f, 5.0f, 6.0f};
    vec3 result = add(a, b);

    EXPECT_FLOAT_EQ(result.x, 5.0f);
    EXPECT_FLOAT_EQ(result.y, 7.0f);
    EXPECT_FLOAT_EQ(result.z, 9.0f);
}

TEST(MathTests, VectorNormalization) {
    vec3 v = {10.0f, 0.0f, 0.0f};
    vec3 norm = normalize(v);

    EXPECT_FLOAT_EQ(norm.x, 1.0f);
    EXPECT_FLOAT_EQ(norm.y, 0.0f);
    EXPECT_FLOAT_EQ(norm.z, 0.0f);
}

// --- Test Quantization Logic (Types.cppm) ---

TEST(CompressionTests, QuantizePosition) {
    // Define a bounds box 0 to 10
    vec3 minB = {0.0f, 0.0f, 0.0f};
    vec3 extent = {10.0f, 10.0f, 10.0f};
    
    // Point right in the middle (5.0)
    vec3 p = {5.0f, 5.0f, 5.0f};

    u16vec3 q = quantize_position(p, minB, extent);

    // Max u16 is 65535. Middle should be approx 32767
    EXPECT_NEAR(q.x, 32767, 5); // Allow small rounding error
    EXPECT_NEAR(q.y, 32767, 5);
    EXPECT_NEAR(q.z, 32767, 5);
}

// --- Test Engine Logic (Engine.cppm / LoadModel.cpp) ---

TEST(EngineTests, BoundsCalculation) {
    std::vector<Triangle> triangles;
    
    // Triangle spanning -1 to 1 on X axis
    triangles.push_back({
        {-1.0f, 0.0f, 0.0f},
        { 1.0f, 0.0f, 0.0f},
        { 0.0f, 1.0f, 0.0f}
    });

    MeshBounds bounds;
    // Core is the namespace exported by Engine.cppm
    bool success = Core::load_bounds(triangles, bounds);

    ASSERT_TRUE(success);
    EXPECT_FLOAT_EQ(bounds.minPos.x, -1.0f);
    EXPECT_FLOAT_EQ(bounds.maxPos.x,  1.0f);
    EXPECT_FLOAT_EQ(bounds.maxPos.y,  1.0f);
}

TEST(EngineTests, CacheGenerationEmpty) {
    std::vector<Triangle> empty_tris;
    Object obj;
    // Should return false or handle empty gracefully
    bool result = Core::load_cache(empty_tris, obj);
    EXPECT_FALSE(result);
}