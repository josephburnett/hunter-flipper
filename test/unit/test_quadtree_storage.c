// Test 1.1: Quadtree Point Storage and Retrieval
// This test verifies that multiple points added to quadtree can all be retrieved

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../../test_common.h"
#include "../../sonar_chart.h"

// Mock engine.h functions for test environment
#ifndef TEST_BUILD
#include "../../engine/engine.h"
#endif

// Test helper function to create a test chart
SonarChart* create_test_chart() {
    SonarChart* chart = malloc(sizeof(SonarChart));
    if(!chart) return NULL;
    
    // Initialize memory pools
    if(!sonar_node_pool_init(&chart->node_pool, 64)) {
        free(chart);
        return NULL;
    }
    
    if(!sonar_point_pool_init(&chart->point_pool, 128)) {
        sonar_node_pool_cleanup(&chart->node_pool);
        free(chart);
        return NULL;
    }
    
    // Create root node with test bounds
    SonarBounds root_bounds = sonar_bounds_create(-100, -100, 200, 200);
    chart->root = sonar_quad_create(chart, root_bounds, 0);
    
    // Initialize other fields
    chart->last_fade_update = 0;
    chart->points_faded_this_frame = 0;
    chart->cache_count = 0;
    chart->last_query_bounds = sonar_bounds_create(0, 0, 0, 0);
    chart->points_added_this_frame = 0;
    chart->points_removed_this_frame = 0;
    chart->query_count_this_frame = 0;
    
    return chart;
}

// Test function: Multiple points storage and retrieval
bool test_quadtree_multiple_points() {
    printf("=== Test 1.1: Quadtree Multiple Points Storage/Retrieval ===\n");
    
    SonarChart* chart = create_test_chart();
    if(!chart) {
        printf("FAIL: Could not create test chart\n");
        return false;
    }
    
    printf("Step 1: Adding 10 terrain points in a cluster...\n");
    
    // Add 10 terrain points in a cluster
    int points_added = 0;
    for(int i = 0; i < 10; i++) {
        bool added = sonar_chart_add_point(chart, 60 + i, 50, true);
        if(added) {
            points_added++;
            printf("  Added point %d at (%d, %d)\n", i+1, 60+i, 50);
        } else {
            printf("  FAILED to add point %d at (%d, %d)\n", i+1, 60+i, 50);
        }
    }
    
    printf("Successfully added %d out of 10 points\n", points_added);
    
    printf("\nStep 2: Querying the area to retrieve points...\n");
    
    // Query the area
    SonarBounds bounds = {50, 40, 80, 60};
    SonarPoint* points[20];
    uint16_t count = sonar_chart_query_area(chart, bounds, points, 20);
    
    printf("Query returned %d points\n", count);
    
    // VERIFY: All 10 points are returned
    if(count != points_added) {
        printf("FAIL: Expected %d points, got %d\n", points_added, count);
        return false;
    }
    
    printf("\nStep 3: Verifying point coordinates and properties...\n");
    
    // VERIFY: Each point has correct coordinates
    int terrain_count = 0;
    for(int i = 0; i < count; i++) {
        printf("  Point %d: (%d, %d) terrain=%d\n", 
               i+1, points[i]->world_x, points[i]->world_y, points[i]->is_terrain);
        
        if(points[i]->is_terrain) terrain_count++;
        
        // Check coordinates are in expected range
        if(points[i]->world_x < 60 || points[i]->world_x > 69) {
            printf("FAIL: Point %d has wrong X coordinate: %d (expected 60-69)\n", 
                   i+1, points[i]->world_x);
            return false;
        }
        
        if(points[i]->world_y != 50) {
            printf("FAIL: Point %d has wrong Y coordinate: %d (expected 50)\n", 
                   i+1, points[i]->world_y);
            return false;
        }
    }
    
    printf("All points have correct coordinates\n");
    printf("Terrain points: %d out of %d\n", terrain_count, count);
    
    if(terrain_count != points_added) {
        printf("FAIL: Expected %d terrain points, got %d\n", points_added, terrain_count);
        return false;
    }
    
    printf("\nStep 4: Testing exact point queries...\n");
    
    // Test querying for exact points
    for(int i = 0; i < 3; i++) {
        SonarBounds exact = {60 + i, 50, 60 + i, 50};
        SonarPoint* exact_points[5];
        uint16_t exact_count = sonar_chart_query_area(chart, exact, exact_points, 5);
        
        printf("  Exact query for (%d, 50): %d points found\n", 60+i, exact_count);
        
        if(exact_count < 1) {
            printf("FAIL: No points found for exact query at (%d, 50)\n", 60+i);
            return false;
        }
    }
    
    // Cleanup
    sonar_chart_free(chart);
    
    printf("\n✓ Test 1.1 PASSED: Multiple points storage and retrieval works correctly\n");
    printf("================================================================\n\n");
    return true;
}

// Additional test: Empty chart behavior
bool test_empty_chart_query() {
    printf("=== Test 1.1b: Empty Chart Query Behavior ===\n");
    
    SonarChart* chart = create_test_chart();
    if(!chart) {
        printf("FAIL: Could not create test chart\n");
        return false;
    }
    
    // Query empty chart
    SonarBounds bounds = {0, 0, 100, 100};
    SonarPoint* points[10];
    uint16_t count = sonar_chart_query_area(chart, bounds, points, 10);
    
    printf("Empty chart query returned %d points\n", count);
    
    if(count != 0) {
        printf("FAIL: Empty chart should return 0 points, got %d\n", count);
        return false;
    }
    
    sonar_chart_free(chart);
    
    printf("✓ Test 1.1b PASSED: Empty chart returns 0 points\n");
    printf("===============================================\n\n");
    return true;
}

int main() {
    printf("Hunter-Flipper Test Suite: Phase 1 - Unit Tests\n");
    printf("Test File: test_quadtree_storage.c\n");
    printf("Purpose: Verify basic quadtree point storage and retrieval\n\n");
    
    bool all_passed = true;
    
    all_passed &= test_empty_chart_query();
    all_passed &= test_quadtree_multiple_points();
    
    if(all_passed) {
        printf("🎉 ALL STORAGE TESTS PASSED\n");
        printf("The quadtree can store and retrieve multiple points correctly.\n");
        printf("If the bug persists, it's likely in subdivision, query logic, or coordinate handling.\n");
        return 0;
    } else {
        printf("❌ STORAGE TESTS FAILED\n");
        printf("The bug is in basic quadtree storage - points are not being stored correctly.\n");
        return 1;
    }
}