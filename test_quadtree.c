#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>

// Mock FURI functions for testing
void* malloc(size_t size);
void free(void* ptr);

// Include the sonar chart code
#include "sonar_chart.h"
#include "sonar_chart.c"

// Test framework
#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            printf("FAIL: %s - %s\n", __func__, message); \
            return false; \
        } \
    } while(0)

#define TEST_PASS() \
    do { \
        printf("PASS: %s\n", __func__); \
        return true; \
    } while(0)

// Test helper functions
static void print_quadtree_structure(SonarQuadNode* node, int depth) {
    if (!node) return;
    
    for (int i = 0; i < depth; i++) printf("  ");
    printf("Node bounds: (%d,%d) to (%d,%d), points: %d, leaf: %s\n",
           node->bounds.min_x, node->bounds.min_y, 
           node->bounds.max_x, node->bounds.max_y,
           node->point_count, node->is_leaf ? "true" : "false");
    
    if (node->is_leaf) {
        for (uint16_t i = 0; i < node->point_count; i++) {
            SonarPoint* p = node->points[i];
            for (int j = 0; j < depth + 1; j++) printf("  ");
            printf("Point %d: (%d,%d) terrain=%s\n", i, p->world_x, p->world_y, 
                   p->is_terrain ? "true" : "false");
        }
    } else {
        for (int i = 0; i < 4; i++) {
            if (node->children[i]) {
                print_quadtree_structure(node->children[i], depth + 1);
            }
        }
    }
}

static void print_query_results(SonarPoint** results, uint16_t count) {
    printf("Query returned %d points:\n", count);
    for (uint16_t i = 0; i < count; i++) {
        printf("  %d: (%d,%d) terrain=%s\n", i, 
               results[i]->world_x, results[i]->world_y,
               results[i]->is_terrain ? "true" : "false");
    }
}

// Test basic sonar chart creation and cleanup
bool test_sonar_chart_creation() {
    SonarChart* chart = sonar_chart_alloc();
    TEST_ASSERT(chart != NULL, "Chart allocation failed");
    TEST_ASSERT(chart->root != NULL, "Root node not created");
    TEST_ASSERT(chart->root->is_leaf == true, "Root should start as leaf");
    TEST_ASSERT(chart->root->point_count == 0, "Root should start empty");
    
    sonar_chart_free(chart);
    TEST_PASS();
}

// Test adding a single point
bool test_single_point_add() {
    SonarChart* chart = sonar_chart_alloc();
    
    // Add a terrain point
    bool result = sonar_chart_add_point(chart, 100, 200, true);
    TEST_ASSERT(result == true, "Failed to add point");
    TEST_ASSERT(chart->root->point_count == 1, "Point count should be 1");
    TEST_ASSERT(chart->root->points[0]->world_x == 100, "Wrong X coordinate");
    TEST_ASSERT(chart->root->points[0]->world_y == 200, "Wrong Y coordinate");
    TEST_ASSERT(chart->root->points[0]->is_terrain == true, "Should be terrain");
    
    sonar_chart_free(chart);
    TEST_PASS();
}

// Test adding multiple points without splitting
bool test_multiple_points_no_split() {
    SonarChart* chart = sonar_chart_alloc();
    
    // Add points within capacity (should be 32 based on SONAR_QUADTREE_MAX_POINTS)
    for (int i = 0; i < 5; i++) {
        bool result = sonar_chart_add_point(chart, 100 + i, 200 + i, true);
        TEST_ASSERT(result == true, "Failed to add point");
    }
    
    TEST_ASSERT(chart->root->is_leaf == true, "Should still be leaf");
    TEST_ASSERT(chart->root->point_count == 5, "Should have 5 points");
    
    sonar_chart_free(chart);
    TEST_PASS();
}

// Test quadtree node splitting when capacity is exceeded
bool test_quadtree_split() {
    SonarChart* chart = sonar_chart_alloc();
    
    // Add more points than SONAR_QUADTREE_MAX_POINTS to force split
    // Let's add 40 points to be sure we exceed the limit
    for (int i = 0; i < 40; i++) {
        bool result = sonar_chart_add_point(chart, 100 + i, 200 + i, true);
        TEST_ASSERT(result == true, "Failed to add point");
    }
    
    printf("After adding 40 points:\n");
    print_quadtree_structure(chart->root, 0);
    
    // Root should no longer be a leaf after splitting
    TEST_ASSERT(chart->root->is_leaf == false, "Root should have split into children");
    TEST_ASSERT(chart->root->children[0] != NULL, "Should have child nodes");
    
    sonar_chart_free(chart);
    TEST_PASS();
}

// Test querying points from a single node
bool test_basic_query() {
    SonarChart* chart = sonar_chart_alloc();
    
    // Add some terrain and water points
    sonar_chart_add_point(chart, 100, 200, true);   // terrain
    sonar_chart_add_point(chart, 110, 210, false);  // water
    sonar_chart_add_point(chart, 120, 220, true);   // terrain
    sonar_chart_add_point(chart, 130, 230, true);   // terrain
    
    // Query a region that should contain all points
    SonarBounds bounds = sonar_bounds_create(50, 150, 200, 300);
    SonarPoint* results[10];
    uint16_t count = sonar_chart_query_area(chart, bounds, results, 10);
    
    printf("Basic query test:\n");
    print_query_results(results, count);
    
    TEST_ASSERT(count == 4, "Should find all 4 points");
    
    // Count terrain vs water
    int terrain_count = 0, water_count = 0;
    for (int i = 0; i < count; i++) {
        if (results[i]->is_terrain) terrain_count++;
        else water_count++;
    }
    
    TEST_ASSERT(terrain_count == 3, "Should find 3 terrain points");
    TEST_ASSERT(water_count == 1, "Should find 1 water point");
    
    sonar_chart_free(chart);
    TEST_PASS();
}

// Test the specific bug scenario: multiple terrain points, only finding one
bool test_bug_reproduction() {
    SonarChart* chart = sonar_chart_alloc();
    
    printf("\n=== BUG REPRODUCTION TEST ===\n");
    
    // Simulate the exact scenario from the logs
    // Add terrain points at various coordinates
    int terrain_coords[][2] = {
        {66, 51}, {66, 52}, {66, 53}, {66, 48}, {66, 50},
        {66, 47}, {66, 49}, {61, 61}, {66, 45}, {70, 57},
        {63, 61}, {62, 62}, {60, 63}, {57, 63}, {48, 55}
    };
    
    for (int i = 0; i < 15; i++) {
        bool result = sonar_chart_add_point(chart, terrain_coords[i][0], terrain_coords[i][1], true);
        TEST_ASSERT(result == true, "Failed to add terrain point");
        printf("Added terrain point at (%d,%d)\n", terrain_coords[i][0], terrain_coords[i][1]);
    }
    
    // Add water points along ray paths (simulating the collision callback)
    for (int i = 60; i < 66; i++) {
        for (int j = 51; j <= 53; j++) {
            sonar_chart_add_point(chart, i, j, false);  // water
        }
    }
    
    printf("\nQuadtree structure after adding points:\n");
    print_quadtree_structure(chart->root, 0);
    
    // Query the same area as in the logs: (-20,-29) to (140,131)
    SonarBounds query_bounds = sonar_bounds_create(-20, -29, 140, 131);
    SonarPoint* results[50];
    uint16_t total_count = sonar_chart_query_area(chart, query_bounds, results, 50);
    
    printf("\nQuery results for bounds (-20,-29) to (140,131):\n");
    print_query_results(results, total_count);
    
    // Count terrain vs water
    int terrain_count = 0, water_count = 0;
    for (int i = 0; i < total_count; i++) {
        if (results[i]->is_terrain) terrain_count++;
        else water_count++;
    }
    
    printf("\nTotal points: %d, Terrain: %d, Water: %d\n", total_count, terrain_count, water_count);
    
    // The bug: we should find many terrain points, but we're only finding 1
    TEST_ASSERT(total_count > 20, "Should find many points total");
    TEST_ASSERT(terrain_count >= 10, "Should find many terrain points, not just 1!");
    
    sonar_chart_free(chart);
    TEST_PASS();
}

// Test terrain flag preservation (the fix I made)
bool test_terrain_flag_preservation() {
    SonarChart* chart = sonar_chart_alloc();
    
    // Add a terrain point
    sonar_chart_add_point(chart, 100, 200, true);
    
    // Query to verify it's terrain
    SonarBounds bounds = sonar_bounds_create(90, 190, 110, 210);
    SonarPoint* results[5];
    uint16_t count = sonar_chart_query_area(chart, bounds, results, 5);
    
    TEST_ASSERT(count == 1, "Should find the terrain point");
    TEST_ASSERT(results[0]->is_terrain == true, "Point should be terrain");
    
    // Now add a water point at the same location
    sonar_chart_add_point(chart, 100, 200, false);
    
    // Query again - should still be terrain (terrain overrides water)
    count = sonar_chart_query_area(chart, bounds, results, 5);
    TEST_ASSERT(count == 1, "Should still find the point");
    TEST_ASSERT(results[0]->is_terrain == true, "Point should STILL be terrain after water override attempt");
    
    sonar_chart_free(chart);
    TEST_PASS();
}

// Test edge cases
bool test_edge_cases() {
    SonarChart* chart = sonar_chart_alloc();
    
    // Test empty query
    SonarBounds bounds = sonar_bounds_create(0, 0, 100, 100);
    SonarPoint* results[5];
    uint16_t count = sonar_chart_query_area(chart, bounds, results, 5);
    TEST_ASSERT(count == 0, "Empty chart should return 0 points");
    
    // Test query outside bounds
    sonar_chart_add_point(chart, 50, 50, true);
    bounds = sonar_bounds_create(200, 200, 300, 300);
    count = sonar_chart_query_area(chart, bounds, results, 5);
    TEST_ASSERT(count == 0, "Query outside bounds should return 0 points");
    
    // Test exact boundary query
    bounds = sonar_bounds_create(50, 50, 50, 50);
    count = sonar_chart_query_area(chart, bounds, results, 5);
    TEST_ASSERT(count == 1, "Exact boundary query should find the point");
    
    sonar_chart_free(chart);
    TEST_PASS();
}

// Test stress case: many points in small area
bool test_stress_dense_points() {
    SonarChart* chart = sonar_chart_alloc();
    
    printf("\n=== STRESS TEST: Dense Points ===\n");
    
    // Add 100 terrain points in a 10x10 grid
    for (int x = 0; x < 10; x++) {
        for (int y = 0; y < 10; y++) {
            bool result = sonar_chart_add_point(chart, 100 + x, 200 + y, true);
            TEST_ASSERT(result == true, "Failed to add point in stress test");
        }
    }
    
    printf("Added 100 points in 10x10 grid\n");
    print_quadtree_structure(chart->root, 0);
    
    // Query the entire area
    SonarBounds bounds = sonar_bounds_create(90, 190, 120, 220);
    SonarPoint* results[150];
    uint16_t count = sonar_chart_query_area(chart, bounds, results, 150);
    
    printf("Query found %d points (expected 100)\n", count);
    TEST_ASSERT(count == 100, "Should find all 100 points");
    
    // Verify all are terrain
    for (int i = 0; i < count; i++) {
        TEST_ASSERT(results[i]->is_terrain == true, "All points should be terrain");
    }
    
    sonar_chart_free(chart);
    TEST_PASS();
}

int main() {
    printf("Running SonarChart Quadtree Unit Tests\n");
    printf("=====================================\n\n");
    
    int passed = 0, total = 0;
    
    #define RUN_TEST(test) \
        do { \
            total++; \
            if (test()) passed++; \
            printf("\n"); \
        } while(0)
    
    RUN_TEST(test_sonar_chart_creation);
    RUN_TEST(test_single_point_add);
    RUN_TEST(test_multiple_points_no_split);
    RUN_TEST(test_quadtree_split);
    RUN_TEST(test_basic_query);
    RUN_TEST(test_terrain_flag_preservation);
    RUN_TEST(test_edge_cases);
    RUN_TEST(test_stress_dense_points);
    RUN_TEST(test_bug_reproduction);  // This should reveal the bug
    
    printf("=====================================\n");
    printf("Test Results: %d/%d passed\n", passed, total);
    
    if (passed == total) {
        printf("All tests PASSED! 🎉\n");
        return 0;
    } else {
        printf("Some tests FAILED! 💥\n");
        return 1;
    }
}