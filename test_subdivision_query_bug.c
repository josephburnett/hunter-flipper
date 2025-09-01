// Simple test to isolate the subdivision query bug
#include <stdio.h>
#include <stdlib.h>
#include "test_common.h"
#include "sonar_chart.h"

int main() {
    printf("=== Subdivision Query Bug Test ===\n");
    
    SonarChart* chart = sonar_chart_alloc();
    if(!chart) {
        printf("FAIL: Could not create sonar chart\n");
        return 1;
    }
    
    // Add exactly 32 points to fill root node
    printf("Step 1: Adding exactly 32 points to fill root node...\n");
    for(int i = 0; i < 32; i++) {
        int x = 50 + (i % 8);
        int y = 50 + (i / 8);
        bool added = sonar_chart_add_point(chart, x, y, false); // water
        if(!added) {
            printf("FAILED to add point %d\n", i+1);
            return 1;
        }
    }
    
    printf("Root node after 32 points: is_leaf=%s, point_count=%d\n", 
           chart->root->is_leaf ? "true" : "false", chart->root->point_count);
    
    // Query before subdivision
    SonarBounds query = sonar_bounds_create(40, 40, 70, 70);
    SonarPoint* points[100];
    uint16_t count = sonar_chart_query_area(chart, query, points, 100);
    printf("Query before subdivision: %d points\n", count);
    
    // Add 33rd point to trigger subdivision
    printf("\nStep 2: Adding 33rd point to trigger subdivision...\n");
    bool added = sonar_chart_add_point(chart, 60, 60, true); // terrain
    printf("Added 33rd point: %s\n", added ? "SUCCESS" : "FAILED");
    printf("Root node after 33 points: is_leaf=%s, point_count=%d\n", 
           chart->root->is_leaf ? "true" : "false", chart->root->point_count);
    
    // Query after subdivision - THIS IS WHERE THE BUG OCCURS
    printf("\nStep 3: Querying after subdivision...\n");
    count = sonar_chart_query_area(chart, query, points, 100);
    printf("Query after subdivision: %d points\n", count);
    
    int terrain_count = 0;
    for(int i = 0; i < count; i++) {
        if(points[i]->is_terrain) terrain_count++;
        printf("  Point %d: (%d,%d) terrain=%s\n", i+1, 
               points[i]->world_x, points[i]->world_y,
               points[i]->is_terrain ? "YES" : "NO");
    }
    
    printf("\nRESULT:\n");
    printf("- Memory pool: %d/512 points used\n", chart->point_pool.active_count);
    printf("- Query returned: %d points (%d terrain)\n", count, terrain_count);
    
    if(count == 32 && chart->point_pool.active_count == 33) {
        printf("❌ BUG CONFIRMED: 33 points stored, but only 32 returned by query!\n");
        printf("The subdivision query traversal is broken.\n");
        return 1;
    } else if(count == 33 && terrain_count == 1) {
        printf("✓ Query works correctly after subdivision\n");
        return 0;
    } else {
        printf("? Unexpected result - need further investigation\n");
        return 1;
    }
}