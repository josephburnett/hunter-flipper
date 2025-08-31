#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

// Mock FURI functions for testing
#define FURI_LOG_I(tag, format, ...) printf("[%s] " format "\n", tag, ##__VA_ARGS__)
#define FURI_LOG_E(tag, format, ...) printf("[ERROR %s] " format "\n", tag, ##__VA_ARGS__)
#define furi_get_tick() 0

// Minimal sonar chart definitions (extracted from the headers)
#define SONAR_QUADTREE_MAX_DEPTH 6
#define SONAR_QUADTREE_MAX_POINTS 32
#define SONAR_FADE_STAGES 4
#define SONAR_FADE_DURATION_MS 15000
#define SONAR_MAX_POINTS 512

typedef enum {
    SONAR_FADE_FULL = 0,
    SONAR_FADE_BRIGHT = 1,
    SONAR_FADE_DIM = 2,
    SONAR_FADE_FAINT = 3,
    SONAR_FADE_GONE = 4
} SonarFadeState;

typedef struct {
    int16_t world_x;
    int16_t world_y;
    uint32_t discovery_time;
    SonarFadeState fade_state;
    bool is_terrain;
} SonarPoint;

typedef struct {
    int16_t min_x, min_y;
    int16_t max_x, max_y;
} SonarBounds;

typedef struct SonarQuadNode {
    SonarBounds bounds;
    uint8_t depth;
    bool is_leaf;
    uint16_t point_count;
    SonarPoint* points[SONAR_QUADTREE_MAX_POINTS];
    struct SonarQuadNode* children[4];
} SonarQuadNode;

typedef struct {
    SonarQuadNode* nodes;
    bool* node_in_use;
    uint16_t pool_size;
    uint16_t next_free;
} SonarNodePool;

typedef struct {
    SonarPoint* points;
    bool* point_in_use;
    uint16_t pool_size;
    uint16_t next_free;
    uint16_t active_count;
} SonarPointPool;

typedef struct {
    SonarQuadNode* root;
    SonarNodePool node_pool;
    SonarPointPool point_pool;
    uint32_t last_fade_update;
    uint16_t cache_count;
    uint16_t points_added_this_frame;
    uint16_t points_removed_this_frame;
    uint16_t query_count_this_frame;
    uint16_t points_faded_this_frame;
} SonarChart;

// Copy sonar chart functions directly (avoiding header dependencies)

// Utility functions
bool sonar_bounds_intersect(SonarBounds a, SonarBounds b) {
    return !(a.max_x < b.min_x || b.max_x < a.min_x || 
             a.max_y < b.min_y || b.max_y < a.min_y);
}

bool sonar_bounds_contains_point(SonarBounds bounds, int16_t x, int16_t y) {
    return x >= bounds.min_x && x <= bounds.max_x && 
           y >= bounds.min_y && y <= bounds.max_y;
}

SonarBounds sonar_bounds_create(int16_t min_x, int16_t min_y, int16_t max_x, int16_t max_y) {
    SonarBounds bounds = {min_x, min_y, max_x, max_y};
    return bounds;
}

// Memory pool management (simplified for testing)
bool sonar_node_pool_init(SonarNodePool* pool, uint16_t size) {
    pool->nodes = malloc(size * sizeof(SonarQuadNode));
    pool->node_in_use = malloc(size * sizeof(bool));
    
    if(!pool->nodes || !pool->node_in_use) {
        if(pool->nodes) free(pool->nodes);
        if(pool->node_in_use) free(pool->node_in_use);
        return false;
    }
    
    pool->pool_size = size;
    pool->next_free = 0;
    memset(pool->node_in_use, 0, size * sizeof(bool));
    
    return true;
}

void sonar_node_pool_cleanup(SonarNodePool* pool) {
    if(pool->nodes) free(pool->nodes);
    if(pool->node_in_use) free(pool->node_in_use);
}

SonarQuadNode* sonar_node_pool_alloc(SonarNodePool* pool) {
    for(uint16_t i = 0; i < pool->pool_size; i++) {
        uint16_t index = (pool->next_free + i) % pool->pool_size;
        if(!pool->node_in_use[index]) {
            pool->node_in_use[index] = true;
            pool->next_free = (index + 1) % pool->pool_size;
            
            SonarQuadNode* node = &pool->nodes[index];
            memset(node, 0, sizeof(SonarQuadNode));
            node->is_leaf = true;
            return node;
        }
    }
    return NULL; // Pool exhausted
}

void sonar_node_pool_free(SonarNodePool* pool, SonarQuadNode* node) {
    for(uint16_t i = 0; i < pool->pool_size; i++) {
        if(&pool->nodes[i] == node) {
            pool->node_in_use[i] = false;
            break;
        }
    }
}

bool sonar_point_pool_init(SonarPointPool* pool, uint16_t size) {
    pool->points = malloc(size * sizeof(SonarPoint));
    pool->point_in_use = malloc(size * sizeof(bool));
    
    if(!pool->points || !pool->point_in_use) {
        if(pool->points) free(pool->points);
        if(pool->point_in_use) free(pool->point_in_use);
        return false;
    }
    
    pool->pool_size = size;
    pool->next_free = 0;
    pool->active_count = 0;
    memset(pool->point_in_use, 0, size * sizeof(bool));
    
    return true;
}

void sonar_point_pool_cleanup(SonarPointPool* pool) {
    if(pool->points) free(pool->points);
    if(pool->point_in_use) free(pool->point_in_use);
}

SonarPoint* sonar_point_pool_alloc(SonarPointPool* pool) {
    if(pool->active_count >= pool->pool_size) {
        return NULL; // Pool exhausted
    }
    
    for(uint16_t i = 0; i < pool->pool_size; i++) {
        uint16_t index = (pool->next_free + i) % pool->pool_size;
        if(!pool->point_in_use[index]) {
            pool->point_in_use[index] = true;
            pool->next_free = (index + 1) % pool->pool_size;
            pool->active_count++;
            
            SonarPoint* point = &pool->points[index];
            memset(point, 0, sizeof(SonarPoint));
            return point;
        }
    }
    return NULL;
}

void sonar_point_pool_free(SonarPointPool* pool, SonarPoint* point) {
    for(uint16_t i = 0; i < pool->pool_size; i++) {
        if(&pool->points[i] == point) {
            pool->point_in_use[i] = false;
            pool->active_count--;
            break;
        }
    }
}

// Quadtree operations (simplified for testing)
SonarQuadNode* sonar_quad_create(SonarChart* chart, SonarBounds bounds, uint8_t depth) {
    SonarQuadNode* node = sonar_node_pool_alloc(&chart->node_pool);
    if(!node) return NULL;
    
    node->bounds = bounds;
    node->depth = depth;
    node->is_leaf = true;
    node->point_count = 0;
    
    return node;
}

bool sonar_quad_insert(SonarChart* chart, SonarQuadNode* node, SonarPoint* point) {
    if(!sonar_bounds_contains_point(node->bounds, point->world_x, point->world_y)) {
        return false;
    }
    
    if(node->is_leaf) {
        if(node->point_count < SONAR_QUADTREE_MAX_POINTS) {
            node->points[node->point_count] = point;
            node->point_count++;
            return true;
        } else {
            // For testing, just force insert instead of subdividing
            printf("WARNING: Node full, would subdivide in real implementation\n");
            return false;
        }
    }
    
    return false; // Simplified - no subdivision for testing
}

bool sonar_quad_query(SonarChart* chart, SonarQuadNode* node, SonarBounds bounds, 
                      SonarPoint** out_points, uint16_t max_points, uint16_t* count) {
    if(!sonar_bounds_intersect(node->bounds, bounds)) {
        return true; // Continue searching other branches
    }
    
    if(node->is_leaf) {
        for(uint16_t i = 0; i < node->point_count; i++) {
            if(*count >= max_points) return false; // Buffer full
            
            SonarPoint* point = node->points[i];
            if(sonar_bounds_contains_point(bounds, point->world_x, point->world_y)) {
                out_points[*count] = point;
                (*count)++;
            }
        }
    }
    
    return true;
}

// Sonar chart lifecycle
SonarChart* sonar_chart_alloc(void) {
    SonarChart* chart = malloc(sizeof(SonarChart));
    if(!chart) return NULL;
    
    // Initialize memory pools
    if(!sonar_node_pool_init(&chart->node_pool, 32) ||
       !sonar_point_pool_init(&chart->point_pool, SONAR_MAX_POINTS)) {
        free(chart);
        return NULL;
    }
    
    // Create root quadtree node with large bounds
    SonarBounds root_bounds = sonar_bounds_create(-32768, -32768, 32767, 32767);
    chart->root = sonar_quad_create(chart, root_bounds, 0);
    if(!chart->root) {
        free(chart);
        return NULL;
    }
    
    // Initialize other fields
    chart->last_fade_update = 0;
    chart->cache_count = 0;
    chart->points_added_this_frame = 0;
    chart->points_removed_this_frame = 0;
    chart->query_count_this_frame = 0;
    chart->points_faded_this_frame = 0;
    
    return chart;
}

void sonar_chart_free(SonarChart* chart) {
    if(!chart) return;
    
    sonar_node_pool_cleanup(&chart->node_pool);
    sonar_point_pool_cleanup(&chart->point_pool);
    
    free(chart);
}

// CRITICAL FUNCTION: This is where the bug is!
bool sonar_chart_query_point(SonarChart* chart, int16_t world_x, int16_t world_y, SonarPoint** out_point) {
    SonarBounds query_bounds = sonar_bounds_create(world_x, world_y, world_x, world_y);
    SonarPoint* nearby_points[9];
    uint16_t count = 0;
    
    sonar_quad_query(chart, chart->root, query_bounds, nearby_points, 9, &count);
    
    // Find closest point using Manhattan distance
    int16_t best_distance = INT16_MAX;
    SonarPoint* best_point = NULL;
    
    for(uint16_t i = 0; i < count; i++) {
        int16_t dx = nearby_points[i]->world_x - world_x;
        int16_t dy = nearby_points[i]->world_y - world_y;
        int16_t distance = abs(dx) + abs(dy); // Manhattan distance
        
        if(distance < best_distance) {
            best_distance = distance;
            best_point = nearby_points[i];
        }
    }
    
    // CRITICAL: Only exact matches (distance <= 0)
    if(best_point && best_distance <= 0) {
        *out_point = best_point;
        return true;
    }
    
    return false;
}

// CRITICAL FUNCTION: This shows the terrain override logic
bool sonar_chart_add_point(SonarChart* chart, int16_t world_x, int16_t world_y, bool is_terrain) {
    printf("Adding point at (%d,%d) terrain=%s\n", world_x, world_y, is_terrain ? "TRUE" : "FALSE");
    
    // Check if point already exists (within 1 unit tolerance) 
    SonarPoint* existing;
    if(sonar_chart_query_point(chart, world_x, world_y, &existing)) {
        printf("  Found existing point at (%d,%d) terrain=%s\n", 
               existing->world_x, existing->world_y, existing->is_terrain ? "TRUE" : "FALSE");
        
        // Update existing point
        existing->discovery_time = furi_get_tick();
        // CRITICAL: Preserve terrain flag: once terrain, always terrain (terrain overrides water)
        if(is_terrain) {
            printf("  Updating to terrain (terrain overrides water)\n");
            existing->is_terrain = true;
        } else {
            printf("  Not updating terrain flag (water does not override terrain)\n");
        }
        return true;
    }
    
    // Allocate new point
    SonarPoint* point = sonar_point_pool_alloc(&chart->point_pool);
    if(!point) return false; // Memory exhausted
    
    point->world_x = world_x;
    point->world_y = world_y;
    point->discovery_time = furi_get_tick();
    point->is_terrain = is_terrain;
    
    printf("  Created NEW point\n");
    
    // Insert into quadtree
    if(sonar_quad_insert(chart, chart->root, point)) {
        chart->points_added_this_frame++;
        return true;
    } else {
        // Failed to insert, free the point
        sonar_point_pool_free(&chart->point_pool, point);
        return false;
    }
}

uint16_t sonar_chart_query_area(SonarChart* chart, SonarBounds bounds, SonarPoint** out_points, uint16_t max_points) {
    uint16_t count = 0;
    sonar_quad_query(chart, chart->root, bounds, out_points, max_points, &count);
    chart->query_count_this_frame++;
    return count;
}

int main() {
    printf("Testing terrain overwrite bug reproduction\n");
    printf("==========================================\n\n");
    
    SonarChart* chart = sonar_chart_alloc();
    if(!chart) {
        printf("FAIL: Chart allocation failed\n");
        return 1;
    }
    
    // Reproduce the exact scenario from the logs
    printf("Step 1: Adding terrain point at (66,52)\n");
    bool result = sonar_chart_add_point(chart, 66, 52, true);
    if(!result) {
        printf("FAIL: Could not add terrain point\n");
        return 1;
    }
    
    // Verify terrain point exists
    SonarBounds query_bounds = sonar_bounds_create(60, 50, 70, 55);
    SonarPoint* results[10];
    uint16_t count = sonar_chart_query_area(chart, query_bounds, results, 10);
    
    printf("Query after terrain addition: found %d points\n", count);
    int terrain_count = 0;
    for(int i = 0; i < count; i++) {
        printf("  Point %d: (%d,%d) terrain=%s\n", i, 
               results[i]->world_x, results[i]->world_y,
               results[i]->is_terrain ? "TRUE" : "FALSE");
        if(results[i]->is_terrain) terrain_count++;
    }
    printf("Terrain points found: %d\n\n", terrain_count);
    
    // Now simulate the water point addition that happens in the collision callback
    printf("Step 2: Adding water points along ray path (simulating collision callback)\n");
    
    // This is what happens in game.c lines 222-226 - water points added every 3 steps
    for(int step = 3; step < 30; step += 3) {
        // Simulate water points around the terrain point
        int water_x = 64 + step/3; // This could hit (66,52) area
        int water_y = 50 + step/3;
        printf("  Adding water point at (%d,%d)\n", water_x, water_y);
        sonar_chart_add_point(chart, water_x, water_y, false);
    }
    
    // Add water point at the exact same location as terrain (this is the bug!)
    printf("  Adding water point at EXACT terrain location (66,52) - THIS IS THE BUG!\n");
    sonar_chart_add_point(chart, 66, 52, false);
    
    // Query again
    count = sonar_chart_query_area(chart, query_bounds, results, 10);
    printf("\nQuery after water additions: found %d points\n", count);
    terrain_count = 0;
    for(int i = 0; i < count; i++) {
        printf("  Point %d: (%d,%d) terrain=%s\n", i, 
               results[i]->world_x, results[i]->world_y,
               results[i]->is_terrain ? "TRUE" : "FALSE");
        if(results[i]->is_terrain) terrain_count++;
    }
    printf("Terrain points found: %d\n", terrain_count);
    
    if(terrain_count == 1) {
        printf("\nSUCCESS: Terrain flag preserved (terrain overrides water) ✓\n");
    } else {
        printf("\nFAILURE: Terrain flag was lost! ✗\n");
        return 1;
    }
    
    sonar_chart_free(chart);
    return 0;
}