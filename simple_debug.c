// Simple debug to isolate the exact point of failure
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_common.h"
#include "sonar_chart.h"

int main() {
    printf("=== SIMPLE SUBDIVISION DEBUG ===\n");
    
    SonarChart* chart = malloc(sizeof(SonarChart));
    sonar_node_pool_init(&chart->node_pool, 10);  // Small pool to trigger failures faster
    sonar_point_pool_init(&chart->point_pool, 50);
    
    SonarBounds root_bounds = sonar_bounds_create(-100, -100, 200, 200);
    chart->root = sonar_quad_create(chart, root_bounds, 0);
    
    chart->last_fade_update = 0;
    chart->points_faded_this_frame = 0;
    chart->cache_count = 0;
    chart->last_query_bounds = sonar_bounds_create(0, 0, 0, 0);
    chart->points_added_this_frame = 0;
    chart->points_removed_this_frame = 0;
    chart->query_count_this_frame = 0;
    
    printf("Pool sizes: nodes=%d, points=%d\n", 
           chart->node_pool.pool_size, chart->point_pool.pool_size);
    
    // Add points one by one and check after each
    for(int i = 0; i < 40; i++) {
        int x = 60 + (i % 10);
        int y = 50 + (i / 10);
        
        printf("\nAdding point %d at (%d,%d)\n", i+1, x, y);
        
        // Check pool usage before
        int nodes_used = 0;
        for(int j = 0; j < chart->node_pool.pool_size; j++) {
            if(chart->node_pool.node_in_use[j]) nodes_used++;
        }
        printf("Before: %d/%d nodes used, %d/%d points used\n", 
               nodes_used, chart->node_pool.pool_size,
               chart->point_pool.active_count, chart->point_pool.pool_size);
        
        bool added = sonar_chart_add_point(chart, x, y, true);
        printf("Result: %s\n", added ? "SUCCESS" : "FAILED");
        
        if(!added) {
            printf("❌ ADDITION FAILED - Memory exhausted?\n");
            break;
        }
        
        // Check pool usage after
        nodes_used = 0;
        for(int j = 0; j < chart->node_pool.pool_size; j++) {
            if(chart->node_pool.node_in_use[j]) nodes_used++;
        }
        printf("After: %d/%d nodes used, %d/%d points used\n", 
               nodes_used, chart->node_pool.pool_size,
               chart->point_pool.active_count, chart->point_pool.pool_size);
        
        // Query to see how many we can retrieve
        SonarBounds query = {-200, -200, 400, 400};
        SonarPoint* points[100];
        uint16_t count = sonar_chart_query_area(chart, query, points, 100);
        printf("Retrievable: %d (expected: %d)\n", count, i+1);
        
        if(count != i+1) {
            printf("❌ POINT LOSS: Missing %d points\n", (i+1) - count);
            break;
        }
    }
    
    return 0;
}