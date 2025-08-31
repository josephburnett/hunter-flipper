#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

// Mock FURI for standalone testing
#define FURI_LOG_I(tag, fmt, ...) printf("[%s] " fmt "\n", tag, ##__VA_ARGS__)
uint32_t furi_get_tick() { return 0; }

#include "sonar_chart.h"

bool test_terrain_storage_bug() {
    printf("\n=== Testing Terrain Storage Bug ===\n");
    
    // Create sonar chart
    SonarChart* chart = sonar_chart_create();
    if (!chart) {
        printf("FAILED: Could not create chart\n");
        return false;
    }
    
    // Add the exact terrain points from the logs
    int terrain_coords[][2] = {
        {66, 51}, {66, 52}, {66, 53}, {66, 48}, {66, 50},
        {66, 47}, {66, 49}, {61, 61}, {66, 45}, {70, 57},
        {63, 61}, {62, 62}, {60, 63}, {57, 63}, {48, 55}
    };
    int num_terrain = sizeof(terrain_coords) / sizeof(terrain_coords[0]);
    
    printf("Adding %d terrain points...\n", num_terrain);
    for (int i = 0; i < num_terrain; i++) {
        bool success = sonar_chart_add_point(chart, 
            terrain_coords[i][0], terrain_coords[i][1], true);
        if (!success) {
            printf("FAILED: Could not add terrain point at (%d,%d)\n", 
                   terrain_coords[i][0], terrain_coords[i][1]);
        }
    }
    
    // Add some water points in the same area to simulate complete sonar scan
    printf("Adding water points to simulate complete scan...\n");
    for (int x = 50; x <= 75; x += 2) {
        for (int y = 40; y <= 65; y += 2) {
            // Skip if it's a terrain coordinate
            bool is_terrain_coord = false;
            for (int t = 0; t < num_terrain; t++) {
                if (terrain_coords[t][0] == x && terrain_coords[t][1] == y) {
                    is_terrain_coord = true;
                    break;
                }
            }
            if (!is_terrain_coord) {
                sonar_chart_add_point(chart, x, y, false); // water
            }
        }
    }
    
    // Now query the area like the render system does
    SonarBounds query_bounds = sonar_bounds_create(-20, -29, 140, 131); // from logs
    SonarPoint* visible_points[512];
    uint16_t point_count = sonar_chart_query_area(chart, query_bounds, visible_points, 512);
    
    // Count terrain vs water
    int terrain_count = 0;
    int water_count = 0;
    
    printf("\nQuery results:\n");
    for (uint16_t i = 0; i < point_count && i < 20; i++) { // Show first 20
        printf("  Point %d: (%d,%d) %s\n", i, 
               visible_points[i]->world_x, visible_points[i]->world_y,
               visible_points[i]->is_terrain ? "TERRAIN" : "water");
        
        if (visible_points[i]->is_terrain) terrain_count++;
        else water_count++;
    }
    
    printf("\nSUMMARY: %d total (%d terrain, %d water)\n", 
           point_count, terrain_count, water_count);
    
    // The bug: we should find 15 terrain points, not just 1
    bool success = (terrain_count >= 10);  // Should find most terrain points
    
    if (!success) {
        printf("*** BUG REPRODUCED: Only found %d terrain points instead of expected ~15 ***\n", 
               terrain_count);
    } else {
        printf("*** SUCCESS: Found %d terrain points as expected ***\n", terrain_count);
    }
    
    sonar_chart_free(chart);
    return success;
}

int main() {
    printf("Testing chart terrain storage bug...\n");
    
    if (test_terrain_storage_bug()) {
        printf("\nTEST PASSED: Chart correctly stores and retrieves terrain points\n");
        return 0;
    } else {
        printf("\nTEST FAILED: Chart bug reproduced - terrain points not retrieved correctly\n");
        return 1;
    }
}