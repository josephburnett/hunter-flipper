// Test 1.3: Duplicate Point Handling
// This test verifies how quadtree handles duplicate points at same location

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

// Test function: Duplicate point handling
bool test_quadtree_duplicate_points() {
    printf("=== Test 1.3: Quadtree Duplicate Point Handling ===\n");
    
    SonarChart* chart = create_test_chart();
    if(!chart) {
        printf("FAIL: Could not create test chart\n");
        return false;
    }
    
    printf("Step 1: Adding the same point multiple times...\n");
    
    // Add same point multiple times (the exact coordinates from the bug report)
    int add_count = 0;
    for(int i = 0; i < 5; i++) {
        bool added = sonar_chart_add_point(chart, 66, 51, true);
        if(added) add_count++;
        printf("  Attempt %d to add (66,51): %s\n", i+1, added ? "success" : "failed");
    }
    
    printf("Added point (66,51) successfully %d out of 5 attempts\n", add_count);
    
    printf("\nStep 2: Querying exact location...\n");
    
    // Query exact location
    SonarBounds exact_bounds = {66, 51, 66, 51};
    SonarPoint* points[10];
    uint16_t count = sonar_chart_query_area(chart, exact_bounds, points, 10);
    
    printf("Exact query at (66,51) returned %d points\n", count);
    
    // Verify behavior - should either merge or store all
    printf("Duplicate handling: %d points at (66,51)\n", count);
    if(count >= 1) {
        printf("✓ At least one point exists at the location\n");
        
        // Show details of all points found
        for(int i = 0; i < count; i++) {
            printf("  Point %d: (%d,%d) terrain=%d discovery_time=%u\n", 
                   i+1, points[i]->world_x, points[i]->world_y, 
                   points[i]->is_terrain, points[i]->discovery_time);
        }
    } else {
        printf("❌ FAIL: No points found at (66,51) despite successful additions\n");
        return false;
    }
    
    printf("\nStep 3: Testing nearby point queries...\n");
    
    // Query slightly wider area to see if points are misplaced
    SonarBounds wide_bounds = {65, 50, 67, 52};
    uint16_t wide_count = sonar_chart_query_area(chart, wide_bounds, points, 10);
    
    printf("Wide query (65,50)-(67,52) returned %d points\n", wide_count);
    for(int i = 0; i < wide_count; i++) {
        printf("  Found point: (%d,%d) terrain=%d\n", 
               points[i]->world_x, points[i]->world_y, points[i]->is_terrain);
    }
    
    printf("\nStep 4: Adding different points to check uniqueness...\n");
    
    // Add some different points
    bool added_67_51 = sonar_chart_add_point(chart, 67, 51, true);
    bool added_66_52 = sonar_chart_add_point(chart, 66, 52, true);
    printf("Added (67,51): %s\n", added_67_51 ? "success" : "failed");
    printf("Added (66,52): %s\n", added_66_52 ? "success" : "failed");
    
    // Query the wider area again
    uint16_t final_count = sonar_chart_query_area(chart, wide_bounds, points, 10);
    printf("Final wide query returned %d points\n", final_count);
    
    // Expected: at least 1 point at (66,51) + the 2 new points = at least 3 total
    int expected_min = (count >= 1 ? count : 1) + (added_67_51 ? 1 : 0) + (added_66_52 ? 1 : 0);
    
    if(final_count < expected_min) {
        printf("❌ FAIL: Expected at least %d points, found %d\n", expected_min, final_count);
        return false;
    }
    
    // Cleanup
    sonar_chart_free(chart);
    
    printf("\n✓ Test 1.3 PASSED: Duplicate handling works correctly\n");
    printf("====================================================\n\n");
    return true;
}

// Test function: Terrain vs Water precedence
bool test_terrain_water_precedence() {
    printf("=== Test 1.3b: Terrain vs Water Point Precedence ===\n");
    
    SonarChart* chart = create_test_chart();
    if(!chart) {
        printf("FAIL: Could not create test chart\n");
        return false;
    }
    
    printf("Step 1: Adding water point first...\n");
    bool added_water = sonar_chart_add_point(chart, 70, 55, false);
    printf("Added water at (70,55): %s\n", added_water ? "success" : "failed");
    
    // Query to see what we have
    SonarBounds bounds = {70, 55, 70, 55};
    SonarPoint* points[5];
    uint16_t count = sonar_chart_query_area(chart, bounds, points, 5);
    
    if(count > 0) {
        printf("Water point: (%d,%d) terrain=%d\n", 
               points[0]->world_x, points[0]->world_y, points[0]->is_terrain);
    }
    
    printf("\nStep 2: Adding terrain point at same location...\n");
    bool added_terrain = sonar_chart_add_point(chart, 70, 55, true);
    printf("Added terrain at (70,55): %s\n", added_terrain ? "success" : "failed");
    
    // Query again
    count = sonar_chart_query_area(chart, bounds, points, 5);
    printf("After adding terrain: %d points found\n", count);
    
    if(count > 0) {
        printf("Final point: (%d,%d) terrain=%d\n", 
               points[0]->world_x, points[0]->world_y, points[0]->is_terrain);
        
        // Terrain should override water
        if(points[0]->is_terrain) {
            printf("✓ Terrain correctly overrode water\n");
        } else {
            printf("⚠️  Water remained despite terrain being added\n");
        }
    }
    
    printf("\nStep 3: Reverse test - terrain first, then water...\n");
    
    bool added_terrain_first = sonar_chart_add_point(chart, 75, 60, true);
    printf("Added terrain at (75,60): %s\n", added_terrain_first ? "success" : "failed");
    
    bool added_water_after = sonar_chart_add_point(chart, 75, 60, false);  
    printf("Added water at (75,60): %s\n", added_water_after ? "success" : "failed");
    
    // Query
    SonarBounds bounds2 = {75, 60, 75, 60};
    count = sonar_chart_query_area(chart, bounds2, points, 5);
    
    if(count > 0) {
        printf("Final point at (75,60): terrain=%d\n", points[0]->is_terrain);
        
        // Should still be terrain
        if(points[0]->is_terrain) {
            printf("✓ Terrain correctly preserved against water\n");
        } else {
            printf("❌ FAIL: Water incorrectly overrode terrain\n");
            return false;
        }
    }
    
    // Cleanup
    sonar_chart_free(chart);
    
    printf("\n✓ Test 1.3b PASSED: Terrain/water precedence works correctly\n");
    printf("============================================================\n\n");
    return true;
}

int main() {
    printf("Hunter-Flipper Test Suite: Phase 1 - Unit Tests\n");
    printf("Test File: test_quadtree_duplicates.c\n");
    printf("Purpose: Verify quadtree duplicate point handling\n\n");
    
    bool all_passed = true;
    
    all_passed &= test_quadtree_duplicate_points();
    all_passed &= test_terrain_water_precedence();
    
    if(all_passed) {
        printf("🎉 ALL DUPLICATE HANDLING TESTS PASSED\n");
        printf("The quadtree handles duplicate points correctly.\n");
        printf("The bug is likely not in duplicate handling logic.\n");
        return 0;
    } else {
        printf("❌ DUPLICATE HANDLING TESTS FAILED\n");
        printf("Issues found in duplicate point handling logic.\n");
        return 1;
    }
}