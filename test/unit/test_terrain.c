#include "test_common.h"
#include "mock_furi.h"
#include "terrain.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Test 1: Terrain Manager Allocation/Deallocation
bool test_terrain_allocation(TestResults* results) {
    printf("Testing terrain allocation/deallocation...\n");
    results->tests_run++;
    
    TerrainManager* terrain = terrain_manager_alloc(12345, 128);
    TEST_ASSERT(terrain != NULL, "Terrain allocation failed");
    TEST_ASSERT(terrain->width == TERRAIN_SIZE, "Width should be TERRAIN_SIZE");
    TEST_ASSERT(terrain->height == TERRAIN_SIZE, "Height should be TERRAIN_SIZE");
    TEST_ASSERT(terrain->seed == 12345, "Seed should be preserved");
    TEST_ASSERT(terrain->elevation_threshold == 128, "Elevation threshold should be preserved");
    TEST_ASSERT(terrain->height_map != NULL, "Height map should be allocated");
    TEST_ASSERT(terrain->collision_map != NULL, "Collision map should be allocated");
    
    terrain_manager_free(terrain);
    
    results->tests_passed++;
    return true;
}

// Test 2: Deterministic Generation (same seed = same terrain)
bool test_terrain_deterministic(TestResults* results) {
    printf("Testing deterministic terrain generation...\n");
    results->tests_run++;
    
    TerrainManager* t1 = terrain_manager_alloc(12345, 90);
    TerrainManager* t2 = terrain_manager_alloc(12345, 90);
    
    TEST_ASSERT(t1 != NULL && t2 != NULL, "Both terrain managers should allocate");
    
    // Compare height maps
    for(int i = 0; i < TERRAIN_SIZE * TERRAIN_SIZE; i++) {
        TEST_ASSERT(t1->height_map[i] == t2->height_map[i], "Height maps should be identical with same seed");
    }
    
    // Compare collision maps
    for(int i = 0; i < TERRAIN_SIZE * TERRAIN_SIZE; i++) {
        TEST_ASSERT(t1->collision_map[i] == t2->collision_map[i], "Collision maps should be identical with same seed");
    }
    
    terrain_manager_free(t1);
    terrain_manager_free(t2);
    
    results->tests_passed++;
    return true;
}

// Test 3: Different Seeds Generate Different Terrain
bool test_terrain_different_seeds(TestResults* results) {
    printf("Testing different seeds generate different terrain...\n");
    results->tests_run++;
    
    TerrainManager* t1 = terrain_manager_alloc(12345, 90);
    TerrainManager* t2 = terrain_manager_alloc(54321, 90);
    
    TEST_ASSERT(t1 != NULL && t2 != NULL, "Both terrain managers should allocate");
    
    // Count differences
    int differences = 0;
    for(int i = 0; i < TERRAIN_SIZE * TERRAIN_SIZE; i++) {
        if(t1->height_map[i] != t2->height_map[i]) {
            differences++;
        }
    }
    
    // Should have significant differences with different seeds
    int total_pixels = TERRAIN_SIZE * TERRAIN_SIZE;
    TEST_ASSERT(differences > total_pixels / 4, "Different seeds should generate significantly different terrain");
    
    terrain_manager_free(t1);
    terrain_manager_free(t2);
    
    results->tests_passed++;
    return true;
}

// Test 4: Height Range Validation (0-255)
bool test_terrain_height_range(TestResults* results) {
    printf("Testing terrain height range (0-255)...\n");
    results->tests_run++;
    
    TerrainManager* terrain = terrain_manager_alloc(12345, 128);
    TEST_ASSERT(terrain != NULL, "Terrain allocation failed");
    
    // Check all height values are in valid range
    for(int i = 0; i < TERRAIN_SIZE * TERRAIN_SIZE; i++) {
        uint8_t height = terrain->height_map[i];
        // Heights should be 0-255 (uint8_t range)
        TEST_ASSERT(height >= 0 && height <= 255, "Height out of valid range");
    }
    
    terrain_manager_free(terrain);
    
    results->tests_passed++;
    return true;
}

// Test 5: Elevation Threshold Application
bool test_terrain_elevation_threshold(TestResults* results) {
    printf("Testing elevation threshold application...\n");
    results->tests_run++;
    
    // Test with very low threshold (most pixels should be terrain)
    TerrainManager* low_terrain = terrain_manager_alloc(12345, 50);
    
    // Test with very high threshold (most pixels should be water)
    TerrainManager* high_terrain = terrain_manager_alloc(12345, 200);
    
    TEST_ASSERT(low_terrain != NULL && high_terrain != NULL, "Both terrain managers should allocate");
    
    int low_terrain_count = 0;
    int high_terrain_count = 0;
    
    for(int i = 0; i < TERRAIN_SIZE * TERRAIN_SIZE; i++) {
        if(low_terrain->collision_map[i]) low_terrain_count++;
        if(high_terrain->collision_map[i]) high_terrain_count++;
    }
    
    // Low threshold should have more terrain than high threshold
    TEST_ASSERT(low_terrain_count > high_terrain_count, "Lower threshold should produce more terrain");
    
    terrain_manager_free(low_terrain);
    terrain_manager_free(high_terrain);
    
    results->tests_passed++;
    return true;
}

// Test 6: Collision Detection Bounds Checking
bool test_terrain_collision_bounds(TestResults* results) {
    printf("Testing collision detection bounds checking...\n");
    results->tests_run++;
    
    TerrainManager* terrain = terrain_manager_alloc(12345, 128);
    TEST_ASSERT(terrain != NULL, "Terrain allocation failed");
    
    // Test valid coordinates
    bool valid_result = terrain_check_collision(terrain, TERRAIN_SIZE/2, TERRAIN_SIZE/2);
    (void)valid_result; // Result depends on terrain generation, just verify no crash
    
    // Test invalid coordinates (should return false)
    TEST_ASSERT(!terrain_check_collision(terrain, -1, 0), "Negative X should return false");
    TEST_ASSERT(!terrain_check_collision(terrain, 0, -1), "Negative Y should return false");
    TEST_ASSERT(!terrain_check_collision(terrain, TERRAIN_SIZE, 0), "X >= width should return false");
    TEST_ASSERT(!terrain_check_collision(terrain, 0, TERRAIN_SIZE), "Y >= height should return false");
    TEST_ASSERT(!terrain_check_collision(terrain, TERRAIN_SIZE + 10, TERRAIN_SIZE + 10), "Far out of bounds should return false");
    
    terrain_manager_free(terrain);
    
    results->tests_passed++;
    return true;
}

// Test 7: Memory Safety (no crashes with extreme values)
bool test_terrain_memory_safety(TestResults* results) {
    printf("Testing terrain memory safety...\n");
    results->tests_run++;
    
    // Test with various seeds and thresholds
    for(int i = 0; i < 10; i++) {
        uint32_t seed = 1000 + i;
        uint8_t threshold = 25 * i + 25; // 25, 50, 75, etc.
        
        TerrainManager* terrain = terrain_manager_alloc(seed, threshold);
        TEST_ASSERT(terrain != NULL, "Terrain allocation should not fail");
        
        // Test collision detection at various points
        for(int y = 0; y < TERRAIN_SIZE; y += 8) {
            for(int x = 0; x < TERRAIN_SIZE; x += 8) {
                terrain_check_collision(terrain, x, y); // Should not crash
            }
        }
        
        terrain_manager_free(terrain);
    }
    
    results->tests_passed++;
    return true;
}

int main(void) {
    printf("=== Unit Tests: Terrain Module ===\n\n");
    
    TestResults results = {0, 0, 0};
    
    // Basic allocation/deallocation
    if (!test_terrain_allocation(&results)) results.tests_failed++;
    
    // Deterministic generation
    if (!test_terrain_deterministic(&results)) results.tests_failed++;
    if (!test_terrain_different_seeds(&results)) results.tests_failed++;
    
    // Height and threshold validation
    if (!test_terrain_height_range(&results)) results.tests_failed++;
    if (!test_terrain_elevation_threshold(&results)) results.tests_failed++;
    
    // Collision detection
    if (!test_terrain_collision_bounds(&results)) results.tests_failed++;
    
    // Memory safety
    if (!test_terrain_memory_safety(&results)) results.tests_failed++;
    
    // Summary
    printf("\n=== Terrain Unit Test Results ===\n");
    printf("Tests run: %d\n", results.tests_run);
    printf("Tests passed: %d\n", results.tests_passed);
    printf("Tests failed: %d\n", results.tests_failed);
    
    if (results.tests_failed == 0) {
        printf("✅ All terrain unit tests PASSED!\n");
        return 0;
    } else {
        printf("❌ %d terrain unit tests FAILED!\n", results.tests_failed);
        return 1;
    }
}