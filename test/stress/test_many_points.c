// Test 4.1: Large Scale Point Storage Stress Test
// This test verifies behavior with realistic numbers of discovered points

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
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
    
    // Initialize memory pools with realistic sizes
    if(!sonar_node_pool_init(&chart->node_pool, 128)) {
        free(chart);
        return NULL;
    }
    
    if(!sonar_point_pool_init(&chart->point_pool, 512)) {
        sonar_node_pool_cleanup(&chart->node_pool);
        free(chart);
        return NULL;
    }
    
    // Create root node with large bounds (matching actual game)
    SonarBounds root_bounds = sonar_bounds_create(-32768, -32768, 32767, 32767);
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

// Test function: Large scale storage
bool test_large_scale_storage() {
    printf("=== Test 4.1: Large Scale Point Storage ===\n");
    printf("Testing with realistic number of discovered points\n\n");
    
    SonarChart* chart = create_test_chart();
    if(!chart) {
        printf("FAIL: Could not create test chart\n");
        return false;
    }
    
    printf("Step 1: Simulating full sonar sweep discovery...\n");
    
    // Simulate discovering terrain in a realistic pattern
    // This creates a dense cluster that should force multiple subdivisions
    
    int points_added = 0;
    int center_x = 60, center_y = 60;
    int max_radius = 40;
    
    // Create a realistic terrain pattern - islands and coastlines
    for(int radius = 5; radius <= max_radius; radius += 2) {
        printf("Discovery radius: %d\n", radius);
        
        // Create terrain in circular patterns with some randomness
        int circumference_points = (int)(2 * 3.14159 * radius);
        
        for(int i = 0; i < circumference_points; i += 2) {
            double angle = (2 * 3.14159 * i) / circumference_points;
            
            // Add some variation to create realistic terrain
            double actual_radius = radius + ((i % 7) - 3);  // ±3 variation
            
            int x = center_x + (int)(actual_radius * cos(angle));
            int y = center_y + (int)(actual_radius * sin(angle));
            
            // Create terrain clusters - not every point is terrain
            if((x + y + i) % 3 == 0) {  // Roughly 1/3 of points are terrain
                bool added = sonar_chart_add_point(chart, x, y, true);
                if(added) {
                    points_added++;
                    
                    if(points_added <= 10 || points_added % 50 == 0) {
                        printf("  Added terrain point %d at (%d, %d)\n", points_added, x, y);
                    }
                } else {
                    printf("  FAILED to add terrain point at (%d, %d) - memory full?\n", x, y);
                }
            }
        }
        
        // Check retrieval after each major expansion
        if(radius % 10 == 5) {  // Every 5th radius expansion
            SonarBounds query = {-200, -200, 400, 400};
            SonarPoint* points[600];
            uint16_t retrieved = sonar_chart_query_area(chart, query, points, 600);
            
            int terrain_retrieved = 0;
            for(int i = 0; i < retrieved; i++) {
                if(points[i]->is_terrain) terrain_retrieved++;
            }
            
            printf("  At radius %d: Added=%d, Retrieved=%d (%d terrain)\n", 
                   radius, points_added, retrieved, terrain_retrieved);
            
            if(terrain_retrieved < points_added) {
                printf("  ⚠️  DISCREPANCY: %d terrain points missing!\n", 
                       points_added - terrain_retrieved);
            }
        }
    }
    
    printf("\nTotal terrain points added: %d\n", points_added);
    
    printf("\nStep 2: Comprehensive retrieval test...\n");
    
    // Query entire area
    SonarBounds all = {-200, -200, 400, 400};
    SonarPoint* all_points[600];
    uint16_t total_count = sonar_chart_query_area(chart, all, all_points, 600);
    
    printf("Total points retrieved: %d\n", total_count);
    
    int terrain_count = 0;
    for(int i = 0; i < total_count; i++) {
        if(all_points[i]->is_terrain) terrain_count++;
    }
    
    printf("Terrain points retrieved: %d\n", terrain_count);
    printf("Expected terrain points: %d\n", points_added);
    printf("Missing terrain points: %d\n", points_added - terrain_count);
    
    if(terrain_count < points_added) {
        printf("❌ CRITICAL BUG: Large scale test loses %d terrain points!\n", 
               points_added - terrain_count);
        
        // Try with maximum bounds
        printf("\nTrying query with maximum bounds...\n");
        SonarBounds max_bounds = {-32768, -32768, 32767, 32767};
        SonarPoint* max_points[600];
        uint16_t max_count = sonar_chart_query_area(chart, max_bounds, max_points, 600);
        
        int max_terrain = 0;
        for(int i = 0; i < max_count; i++) {
            if(max_points[i]->is_terrain) max_terrain++;
        }
        
        printf("Maximum bounds query: %d total, %d terrain\n", max_count, max_terrain);
        
        if(max_terrain < points_added) {
            printf("❌ Confirmed: Points are lost in the quadtree structure!\n");
        }
        
        return false;
        
    } else if(terrain_count == points_added) {
        printf("✓ All terrain points successfully retrieved\n");
    } else {
        printf("❓ Found MORE terrain points than expected (%d > %d)\n", 
               terrain_count, points_added);
    }
    
    printf("\nStep 3: Quadtree structure analysis...\n");
    
    // Analyze the tree structure
    printf("Root node: is_leaf=%s, point_count=%d\n", 
           chart->root->is_leaf ? "true" : "false",
           chart->root->point_count);
    
    if(!chart->root->is_leaf) {
        int total_depth = 0;
        int leaf_nodes = 0;
        int non_leaf_nodes = 1; // Root
        
        // Simple traversal to count structure
        printf("Subdivision occurred - tree has internal structure\n");
        printf("This stress test successfully triggered subdivision\n");
    } else {
        printf("No subdivision occurred - all points in root node\n");
        if(points_added > SONAR_QUADTREE_MAX_POINTS) {
            printf("⚠️  This is unexpected - should have subdivided with %d points\n", points_added);
        }
    }
    
    // Cleanup
    sonar_chart_free(chart);
    
    printf("\n");
    if(terrain_count == points_added) {
        printf("✓ Test 4.1 PASSED: Large scale storage works correctly\n");
        return true;
    } else {
        printf("❌ Test 4.1 FAILED: Large scale storage loses points\n");
        return false;
    }
}

// Test function: Subdivision threshold testing
bool test_subdivision_threshold() {
    printf("=== Test 4.1b: Subdivision Threshold Analysis ===\n");
    
    SonarChart* chart = create_test_chart();
    if(!chart) {
        printf("FAIL: Could not create test chart\n");
        return false;
    }
    
    printf("SONAR_QUADTREE_MAX_POINTS = %d\n", SONAR_QUADTREE_MAX_POINTS);
    printf("Testing point addition around the subdivision threshold...\n\n");
    
    // Add points in a tight cluster to force subdivision
    int center_x = 100, center_y = 100;
    int points_added = 0;
    
    // First, add points up to the threshold
    printf("Phase 1: Adding points up to threshold...\n");
    
    for(int i = 0; i < SONAR_QUADTREE_MAX_POINTS; i++) {
        int x = center_x + (i % 8);  // 8x8 grid pattern
        int y = center_y + (i / 8);
        
        bool added = sonar_chart_add_point(chart, x, y, true);
        if(added) points_added++;
        
        if(i < 5 || i >= SONAR_QUADTREE_MAX_POINTS - 5) {
            printf("  Point %d at (%d,%d): %s\n", i+1, x, y, added ? "added" : "failed");
        } else if(i == 5) {
            printf("  ... (adding intermediate points) ...\n");
        }
    }
    
    printf("Added %d points (threshold = %d)\n", points_added, SONAR_QUADTREE_MAX_POINTS);
    printf("Root is_leaf: %s, point_count: %d\n", 
           chart->root->is_leaf ? "true" : "false", chart->root->point_count);
    
    // Query to verify all points exist
    SonarBounds query = {50, 50, 150, 150};
    SonarPoint* points[100];
    uint16_t count = sonar_chart_query_area(chart, query, points, 100);
    
    printf("Query returned %d points\n", count);
    
    if(count != points_added) {
        printf("❌ THRESHOLD BUG: Expected %d, got %d (lost %d)\n", 
               points_added, count, points_added - count);
        return false;
    }
    
    printf("\nPhase 2: Adding points beyond threshold to trigger subdivision...\n");
    
    // Now add more points to trigger subdivision
    for(int i = 0; i < 10; i++) {
        int x = center_x + 8 + (i % 3);
        int y = center_y + 8 + (i / 3);
        
        bool added = sonar_chart_add_point(chart, x, y, true);
        if(added) points_added++;
        
        printf("  Extra point %d at (%d,%d): %s\n", i+1, x, y, added ? "added" : "failed");
        
        // Check after each addition
        count = sonar_chart_query_area(chart, query, points, 100);
        printf("    After adding: Total retrievable = %d (expected %d)\n", count, points_added);
        
        if(count < points_added) {
            printf("    ❌ CRITICAL: Lost %d points after adding point %d!\n", 
                   points_added - count, i+1);
            printf("    Root is_leaf: %s, point_count: %d\n",
                   chart->root->is_leaf ? "true" : "false", chart->root->point_count);
            return false;
        }
    }
    
    printf("\nFinal state:\n");
    printf("Total points added: %d\n", points_added);
    printf("Root is_leaf: %s, point_count: %d\n", 
           chart->root->is_leaf ? "true" : "false", chart->root->point_count);
    
    // Final comprehensive query
    SonarBounds final_query = {0, 0, 200, 200};
    count = sonar_chart_query_area(chart, final_query, points, 100);
    
    printf("Final query: %d points retrieved\n", count);
    
    if(count == points_added) {
        printf("✓ Subdivision threshold test passed\n");
    } else {
        printf("❌ Subdivision threshold test failed (%d != %d)\n", count, points_added);
        return false;
    }
    
    // Cleanup
    sonar_chart_free(chart);
    
    return true;
}

int main() {
    printf("Hunter-Flipper Test Suite: Phase 4 - Stress Testing\n");
    printf("Test File: test_many_points.c\n");
    printf("Purpose: Test quadtree with realistic large numbers of points\n\n");
    
    bool all_passed = true;
    
    all_passed &= test_subdivision_threshold();
    all_passed &= test_large_scale_storage();
    
    if(all_passed) {
        printf("🎉 ALL STRESS TESTS PASSED\n");
        printf("The quadtree handles large numbers of points correctly.\n");
        printf("If the original bug persists, it may be context-dependent.\n");
        return 0;
    } else {
        printf("❌ STRESS TESTS FAILED\n");
        printf("CRITICAL BUG CONFIRMED: Quadtree loses points under stress!\n");
        printf("This is the root cause of the 'single pixel land' bug.\n");
        return 1;
    }
}