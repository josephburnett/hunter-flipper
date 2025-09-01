// Debug version to trace exactly what's happening in subdivision
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "test_common.h"
#include "sonar_chart.h"

// Test function to debug subdivision in detail
bool debug_subdivision() {
    printf("=== DEBUGGING SUBDIVISION STEP BY STEP ===\n");
    
    SonarChart* chart = malloc(sizeof(SonarChart));
    sonar_node_pool_init(&chart->node_pool, 64);
    sonar_point_pool_init(&chart->point_pool, 128);
    
    SonarBounds root_bounds = sonar_bounds_create(-100, -100, 200, 200);
    chart->root = sonar_quad_create(chart, root_bounds, 0);
    
    chart->last_fade_update = 0;
    chart->points_faded_this_frame = 0;
    chart->cache_count = 0;
    chart->last_query_bounds = sonar_bounds_create(0, 0, 0, 0);
    chart->points_added_this_frame = 0;
    chart->points_removed_this_frame = 0;
    chart->query_count_this_frame = 0;
    
    printf("Initial state:\n");
    printf("- Node pool size: %d\n", chart->node_pool.pool_size);
    printf("- Point pool size: %d\n", chart->point_pool.pool_size);
    printf("- Root bounds: (%d,%d) to (%d,%d)\n", 
           chart->root->bounds.min_x, chart->root->bounds.min_y,
           chart->root->bounds.max_x, chart->root->bounds.max_y);
    
    // Add exactly SONAR_QUADTREE_MAX_POINTS + 1 points to trigger subdivision
    printf("\nAdding %d points (max=%d)...\n", SONAR_QUADTREE_MAX_POINTS + 1, SONAR_QUADTREE_MAX_POINTS);
    
    for(int i = 0; i <= SONAR_QUADTREE_MAX_POINTS; i++) {
        int x = 60 + (i % 8);
        int y = 50 + (i / 8);
        
        printf("\nAdding point %d at (%d,%d):\n", i+1, x, y);
        
        // Check memory pool state before addition
        printf("  Before: node_pool active=%d, point_pool active=%d\n",
               chart->node_pool.pool_size - chart->node_pool.next_free,
               chart->point_pool.active_count);
        
        bool added = sonar_chart_add_point(chart, x, y, true);
        printf("  Result: %s\n", added ? "SUCCESS" : "FAILED");
        
        printf("  After: node_pool active=%d, point_pool active=%d\n",
               chart->node_pool.pool_size - chart->node_pool.next_free,
               chart->point_pool.active_count);
        
        printf("  Root: is_leaf=%s, point_count=%d\n",
               chart->root->is_leaf ? "true" : "false", 
               chart->root->point_count);
        
        // If subdivision happened, analyze the tree
        if(!chart->root->is_leaf) {
            printf("  SUBDIVISION OCCURRED! Analyzing children:\n");
            for(int j = 0; j < 4; j++) {
                if(chart->root->children[j]) {
                    printf("    Child %d: bounds=(%d,%d)-(%d,%d), points=%d\n", j,
                           chart->root->children[j]->bounds.min_x,
                           chart->root->children[j]->bounds.min_y,
                           chart->root->children[j]->bounds.max_x,
                           chart->root->children[j]->bounds.max_y,
                           chart->root->children[j]->point_count);
                }
            }
        }
        
        // Count total retrievable points
        SonarBounds query = {-200, -200, 400, 400};
        SonarPoint* points[100];
        uint16_t count = sonar_chart_query_area(chart, query, points, 100);
        printf("  Total retrievable points: %d (expected: %d)\n", count, i+1);
        
        if(count != i+1) {
            printf("  ❌ POINT LOSS DETECTED! Missing: %d points\n", (i+1) - count);
            
            // This is where the bug happens - let's stop and analyze
            if(i == SONAR_QUADTREE_MAX_POINTS) {
                printf("\n=== BUG OCCURRED EXACTLY AT SUBDIVISION TRIGGER ===\n");
                return false;
            }
        }
    }
    
    return true;
}

int main() {
    debug_subdivision();
    return 0;
}