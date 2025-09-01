// Test 1.2: Quadtree Subdivision Behavior
// This test verifies that subdivision doesn't lose points

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

// Test function: Subdivision preserves points
bool test_quadtree_subdivision_preserves_points() {
    printf("=== Test 1.2: Quadtree Subdivision Point Preservation ===\n");
    
    SonarChart* chart = create_test_chart();
    if(!chart) {
        printf("FAIL: Could not create test chart\n");
        return false;
    }
    
    printf("SONAR_QUADTREE_MAX_POINTS = %d\n", SONAR_QUADTREE_MAX_POINTS);
    
    printf("Step 1: Adding points to trigger subdivision...\n");
    
    // Add enough points to trigger subdivision (>SONAR_QUADTREE_MAX_POINTS)
    int target_points = SONAR_QUADTREE_MAX_POINTS + 5;
    int points_added = 0;
    
    for(int i = 0; i < target_points; i++) {
        // Use unique coordinates to avoid duplicates
        int x = 60 + (i % 8);  // X varies 60-67
        int y = 50 + (i / 8);  // Y varies 50+ (no duplicates)
        
        bool added = sonar_chart_add_point(chart, x, y, true);
        if(added) {
            points_added++;
            if(i < 10) {
                printf("  Added point %d at (%d, %d)\n", i+1, x, y);
            } else if(i == 10) {
                printf("  ... (adding %d more points)\n", target_points - 10);
            }
        } else {
            printf("  FAILED to add point %d at (%d, %d)\n", i+1, x, y);
        }
    }
    
    printf("Successfully added %d out of %d target points\n", points_added, target_points);
    
    printf("\nStep 2: Checking if subdivision occurred...\n");
    printf("Root node is_leaf: %s\n", chart->root->is_leaf ? "true" : "false");
    printf("Root node point_count: %d\n", chart->root->point_count);
    
    if(!chart->root->is_leaf) {
        printf("✓ Subdivision occurred as expected\n");
        
        // Check children
        for(int i = 0; i < 4; i++) {
            if(chart->root->children[i]) {
                printf("  Child %d: is_leaf=%s, points=%d, bounds=(%d,%d)-(%d,%d)\n", 
                       i, 
                       chart->root->children[i]->is_leaf ? "true" : "false",
                       chart->root->children[i]->point_count,
                       chart->root->children[i]->bounds.min_x,
                       chart->root->children[i]->bounds.min_y,
                       chart->root->children[i]->bounds.max_x,
                       chart->root->children[i]->bounds.max_y);
            } else {
                printf("  Child %d: NULL\n", i);
            }
        }
    } else if(chart->root->point_count > SONAR_QUADTREE_MAX_POINTS) {
        printf("⚠️  Subdivision should have occurred but didn't\n");
        printf("Root has %d points (max allowed: %d)\n", chart->root->point_count, SONAR_QUADTREE_MAX_POINTS);
    }
    
    printf("\nStep 3: Querying entire area to verify all points exist...\n");
    
    // Query and verify all points exist
    SonarBounds bounds = {-200, -200, 400, 400};  // Very wide area
    SonarPoint* points[100];
    uint16_t count = sonar_chart_query_area(chart, bounds, points, 100);
    
    printf("Query returned %d points\n", count);
    
    // CRITICAL: Verify no points were lost during subdivision
    if(count != points_added) {
        printf("❌ CRITICAL BUG FOUND: Points lost during subdivision!\n");
        printf("Expected: %d points, Found: %d points\n", points_added, count);
        printf("Lost: %d points\n", points_added - count);
        return false;
    }
    
    printf("\nStep 4: Verifying point properties...\n");
    
    int terrain_count = 0;
    for(int i = 0; i < count; i++) {
        if(points[i]->is_terrain) terrain_count++;
    }
    
    printf("Terrain points: %d out of %d total\n", terrain_count, count);
    
    if(terrain_count != points_added) {
        printf("FAIL: Expected %d terrain points, got %d\n", points_added, terrain_count);
        return false;
    }
    
    // Cleanup
    sonar_chart_free(chart);
    
    printf("\n✓ Test 1.2 PASSED: Subdivision preserves all points correctly\n");
    printf("==============================================================\n\n");
    return true;
}

// Test function: Progressive subdivision
bool test_progressive_subdivision() {
    printf("=== Test 1.2b: Progressive Subdivision Stress Test ===\n");
    
    SonarChart* chart = create_test_chart();
    if(!chart) {
        printf("FAIL: Could not create test chart\n");
        return false;
    }
    
    printf("Step 1: Adding points progressively and checking count at each stage...\n");
    
    int max_test_points = SONAR_QUADTREE_MAX_POINTS * 2;  // Double the limit
    
    for(int stage = 1; stage <= max_test_points; stage++) {
        // Add one unique point per stage
        int x = 60 + ((stage - 1) % 10);
        int y = 50 + ((stage - 1) / 10);
        
        bool added = sonar_chart_add_point(chart, x, y, true);
        if(!added) {
            printf("Failed to add point %d at (%d,%d)\n", stage, x, y);
            continue;
        }
        
        // Query to count points
        SonarBounds bounds = {-200, -200, 400, 400};
        SonarPoint* points[100];
        uint16_t count = sonar_chart_query_area(chart, bounds, points, 100);
        
        // Report at key milestones
        if(stage <= 5 || stage == SONAR_QUADTREE_MAX_POINTS || 
           stage == SONAR_QUADTREE_MAX_POINTS + 1 ||
           stage % 10 == 0 || stage == max_test_points) {
            printf("  Stage %d: Added=1, Total expected=%d, Found=%d, Root leaf=%s\n", 
                   stage, stage, count, chart->root->is_leaf ? "yes" : "no");
        }
        
        // Critical check: count should equal stage number
        if(count != stage) {
            printf("❌ CRITICAL BUG: Point count mismatch at stage %d!\n", stage);
            printf("Expected: %d, Found: %d, Lost: %d\n", stage, count, stage - count);
            return false;
        }
    }
    
    printf("✓ Progressive subdivision maintained correct point count throughout\n");
    
    // Cleanup
    sonar_chart_free(chart);
    
    printf("\n✓ Test 1.2b PASSED: Progressive subdivision works correctly\n");
    printf("=========================================================\n\n");
    return true;
}

int main() {
    printf("Hunter-Flipper Test Suite: Phase 1 - Unit Tests\n");
    printf("Test File: test_quadtree_subdivision.c\n");
    printf("Purpose: Verify quadtree subdivision doesn't lose points\n\n");
    
    bool all_passed = true;
    
    all_passed &= test_quadtree_subdivision_preserves_points();
    all_passed &= test_progressive_subdivision();
    
    if(all_passed) {
        printf("🎉 ALL SUBDIVISION TESTS PASSED\n");
        printf("The quadtree subdivision mechanism works correctly.\n");
        printf("Points are preserved during subdivision operations.\n");
        return 0;
    } else {
        printf("❌ SUBDIVISION TESTS FAILED\n");
        printf("CRITICAL BUG FOUND: Points are lost during quadtree subdivision!\n");
        printf("This is likely the root cause of the 'single pixel land' bug.\n");
        return 1;
    }
}