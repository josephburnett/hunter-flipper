// Test to reproduce the terrain classification bug from fresh logs
// The issue: multiple terrain points added but only 1 classified as terrain

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "test_common.h"
#include "sonar_chart.h"

int main() {
    printf("=== Terrain Classification Bug Reproduction ===\n");
    printf("Testing the exact scenario from fresh logs\n\n");
    
    SonarChart* chart = sonar_chart_alloc();
    if(!chart) {
        printf("FAIL: Could not create sonar chart\n");
        return 1;
    }
    
    // Step 1: Simulate initial water points (like in the game)
    printf("Step 1: Adding initial water points to reach EXACTLY 32 total...\n");
    
    // Add exactly 32 water points to trigger subdivision on the 33rd point
    int water_added = 0;
    for(int x = 30; x <= 90; x += 2) {
        for(int y = 20; y <= 80; y += 2) {
            if(water_added >= 32) break;
            if(sonar_chart_add_point(chart, x, y, false)) { // Add as water
                water_added++;
                if(water_added <= 5 || water_added >= 30) {
                    printf("  Added water point %d at (%d,%d)\n", water_added, x, y);
                }
                
                // Check subdivision status after each addition
                if(water_added == 32) {
                    printf("  Root after 32 points: is_leaf=%s, point_count=%d\n", 
                           chart->root->is_leaf ? "true" : "false", 
                           chart->root->point_count);
                }
            }
        }
        if(water_added >= 32) break;
    }
    
    printf("Added %d water points\n", water_added);
    
    // Query to verify initial state
    SonarBounds query = sonar_bounds_create(-20, -29, 140, 131);
    SonarPoint* points[100];
    uint16_t count = sonar_chart_query_area(chart, query, points, 100);
    
    int terrain_count = 0;
    for(int i = 0; i < count; i++) {
        if(points[i]->is_terrain) terrain_count++;
    }
    
    printf("Initial query: %d total (%d terrain) - should be (32, 0)\n", count, terrain_count);
    
    // Step 2: Add terrain points exactly from the logs
    printf("\nStep 2: Adding terrain points from logs...\n");
    
    struct { int x, y; const char* source; } terrain_points[] = {
        {66, 51, "Ray 0/31"},
        {66, 52, "Ray 1"},  
        {66, 48, "Ray 29"},
        {66, 50, "Ray 30"},
        {66, 53, "Ray 2"},
        {66, 47, "Ray 28"},
        {66, 49, "Ray 29"}, 
        {61, 61, "Additional"},
        {66, 45, "Additional"},
        {70, 57, "Additional"},
        {63, 61, "Additional"},
        {48, 55, "Additional"},
        {57, 38, "Additional"},
        {60, 37, "Additional"}
    };
    
    int num_terrain = sizeof(terrain_points) / sizeof(terrain_points[0]);
    
    for(int i = 0; i < num_terrain; i++) {
        printf("  Adding terrain at (%d,%d) from %s\n", 
               terrain_points[i].x, terrain_points[i].y, terrain_points[i].source);
        
        // Check memory pool status before adding
        printf("    Memory pool status: %d/%d points used\n", 
               chart->point_pool.active_count, chart->point_pool.pool_size);
        
        bool added = sonar_chart_add_point(chart, terrain_points[i].x, terrain_points[i].y, true);
        printf("    Result: %s\n", added ? "SUCCESS" : "FAILED");
        
        // Check memory pool status after adding
        printf("    Memory pool after: %d/%d points used\n", 
               chart->point_pool.active_count, chart->point_pool.pool_size);
        
        // Check if root node subdivided
        printf("    Root node: is_leaf=%s, point_count=%d\n", 
               chart->root->is_leaf ? "true" : "false", 
               chart->root->point_count);
        
        // Query after each terrain addition
        count = sonar_chart_query_area(chart, query, points, 100);
        terrain_count = 0;
        for(int j = 0; j < count; j++) {
            if(points[j]->is_terrain) terrain_count++;
        }
        
        printf("    Query: %d total (%d terrain)\n", count, terrain_count);
        
        // BUG CHECK: Should have more than 1 terrain point after adding several
        if(i >= 3 && terrain_count <= 1) {
            printf("    🐛 BUG DETECTED: Added %d terrain points but only %d classified as terrain!\n", 
                   i+1, terrain_count);
        }
    }
    
    // Step 3: Debug point classification
    printf("\nStep 3: Debugging point classification...\n");
    
    // Check specific terrain points that should exist
    struct { int x, y; } check_points[] = {
        {66, 51}, {66, 52}, {66, 48}, {66, 50}, {66, 53}
    };
    
    for(int i = 0; i < 5; i++) {
        SonarPoint* existing;
        bool found = sonar_chart_query_point(chart, check_points[i].x, check_points[i].y, &existing);
        
        if(found) {
            printf("  Point (%d,%d): EXISTS, is_terrain=%s\n", 
                   check_points[i].x, check_points[i].y,
                   existing->is_terrain ? "TRUE" : "FALSE");
        } else {
            printf("  Point (%d,%d): NOT FOUND\n", check_points[i].x, check_points[i].y);
        }
    }
    
    // Final summary
    printf("\n=== FINAL RESULTS ===\n");
    count = sonar_chart_query_area(chart, query, points, 100);
    terrain_count = 0;
    int water_count = 0;
    
    for(int i = 0; i < count; i++) {
        if(points[i]->is_terrain) {
            terrain_count++;
        } else {
            water_count++;
        }
    }
    
    printf("Total points: %d\n", count);
    printf("Terrain points: %d\n", terrain_count);
    printf("Water points: %d\n", water_count);
    
    if(terrain_count <= 1 && num_terrain > 5) {
        printf("\n❌ BUG CONFIRMED: Added %d terrain points but only %d are classified as terrain!\n", 
               num_terrain, terrain_count);
        printf("This exactly matches the single pixel land bug.\n");
        return 1;
    } else {
        printf("\n✓ Multiple terrain points correctly classified\n");
        return 0;
    }
}