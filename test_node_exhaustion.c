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

// Exact sonar chart definitions from the real code
#define SONAR_QUADTREE_MAX_DEPTH 6
#define SONAR_QUADTREE_MAX_POINTS 32
#define SONAR_FADE_STAGES 4
#define SONAR_FADE_DURATION_MS 15000
#define SONAR_MAX_POINTS 512

// Test with the FIXED larger node pool size
#define TEST_NODE_POOL_SIZE 128  // Same as fixed implementation

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

// Memory pool management (exact copy)
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
    printf("ERROR: Node pool exhausted! Only %d nodes available.\n", pool->pool_size);
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
        return NULL;
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

// Forward declarations
SonarQuadNode* sonar_quad_create(SonarChart* chart, SonarBounds bounds, uint8_t depth);
void sonar_quad_free(SonarChart* chart, SonarQuadNode* node);
bool sonar_quad_insert(SonarChart* chart, SonarQuadNode* node, SonarPoint* point);

// Count used nodes for debugging
int count_used_nodes(SonarNodePool* pool) {
    int count = 0;
    for(uint16_t i = 0; i < pool->pool_size; i++) {
        if(pool->node_in_use[i]) count++;
    }
    return count;
}

// EXACT subdivision logic with debugging
static void sonar_quad_subdivide(SonarChart* chart, SonarQuadNode* node) {
    if(!node->is_leaf || node->depth >= SONAR_QUADTREE_MAX_DEPTH) return;
    
    int16_t mid_x = (node->bounds.min_x + node->bounds.max_x) / 2;
    int16_t mid_y = (node->bounds.min_y + node->bounds.max_y) / 2;
    
    printf("SUBDIVIDING: depth=%d, used_nodes=%d/%d\n", 
           node->depth, count_used_nodes(&chart->node_pool), chart->node_pool.pool_size);
    
    // Create four child nodes
    node->children[0] = sonar_quad_create(chart, 
        sonar_bounds_create(node->bounds.min_x, node->bounds.min_y, mid_x, mid_y), 
        node->depth + 1);
    node->children[1] = sonar_quad_create(chart, 
        sonar_bounds_create(mid_x + 1, node->bounds.min_y, node->bounds.max_x, mid_y), 
        node->depth + 1);
    node->children[2] = sonar_quad_create(chart, 
        sonar_bounds_create(node->bounds.min_x, mid_y + 1, mid_x, node->bounds.max_y), 
        node->depth + 1);
    node->children[3] = sonar_quad_create(chart, 
        sonar_bounds_create(mid_x + 1, mid_y + 1, node->bounds.max_x, node->bounds.max_y), 
        node->depth + 1);
    
    // Check if any children failed to allocate - THIS IS THE CRITICAL BUG!
    for(int i = 0; i < 4; i++) {
        if(!node->children[i]) {
            printf("CRITICAL ERROR: Node allocation failed for child %d! Subdivision aborted!\n", i);
            printf("This means the node stays as leaf with >32 points, causing point insertion failures!\n");
            // Clean up and abort subdivision
            for(int j = 0; j < i; j++) {
                sonar_quad_free(chart, node->children[j]);
                node->children[j] = NULL;
            }
            return; // ← BUG: Node stays as leaf with >32 points!
        }
    }
    
    node->is_leaf = false;
    
    // Redistribute points to children
    for(uint16_t i = 0; i < node->point_count; i++) {
        SonarPoint* point = node->points[i];
        for(int j = 0; j < 4; j++) {
            if(sonar_bounds_contains_point(node->children[j]->bounds, point->world_x, point->world_y)) {
                sonar_quad_insert(chart, node->children[j], point);
                break;
            }
        }
    }
    
    node->point_count = 0;
    printf("Subdivision successful: depth=%d, used_nodes=%d/%d\n", 
           node->depth, count_used_nodes(&chart->node_pool), chart->node_pool.pool_size);
}

SonarQuadNode* sonar_quad_create(SonarChart* chart, SonarBounds bounds, uint8_t depth) {
    SonarQuadNode* node = sonar_node_pool_alloc(&chart->node_pool);
    if(!node) return NULL;
    
    node->bounds = bounds;
    node->depth = depth;
    node->is_leaf = true;
    node->point_count = 0;
    
    return node;
}

void sonar_quad_free(SonarChart* chart, SonarQuadNode* node) {
    if(!node) return;
    
    if(!node->is_leaf) {
        for(int i = 0; i < 4; i++) {
            sonar_quad_free(chart, node->children[i]);
        }
    }
    
    sonar_node_pool_free(&chart->node_pool, node);
}

// EXACT insert logic with debugging
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
            // Need to subdivide
            printf("Node full (%d points), attempting subdivision...\n", node->point_count);
            sonar_quad_subdivide(chart, node);
            if(node->is_leaf) {
                // Subdivision failed, try to force insert anyway
                printf("SUBDIVISION FAILED! Node remains leaf with %d points. Attempting force insert...\n", node->point_count);
                if(node->point_count < SONAR_QUADTREE_MAX_POINTS) {
                    node->points[node->point_count] = point;
                    node->point_count++;
                    printf("Force insert succeeded\n");
                    return true;
                } else {
                    printf("Force insert FAILED! Point lost!\n");
                    return false; // ← BUG: Point is lost!
                }
            }
        }
    }
    
    // Insert into appropriate child
    for(int i = 0; i < 4; i++) {
        if(sonar_quad_insert(chart, node->children[i], point)) {
            return true;
        }
    }
    
    printf("ERROR: Could not insert point (%d,%d) into any child!\n", point->world_x, point->world_y);
    return false;
}

// Query logic (same as before)
bool sonar_quad_query(SonarChart* chart, SonarQuadNode* node, SonarBounds bounds, 
                      SonarPoint** out_points, uint16_t max_points, uint16_t* count) {
    if(!sonar_bounds_intersect(node->bounds, bounds)) {
        return true;
    }
    
    if(node->is_leaf) {
        for(uint16_t i = 0; i < node->point_count; i++) {
            if(*count >= max_points) return false;
            
            SonarPoint* point = node->points[i];
            if(sonar_bounds_contains_point(bounds, point->world_x, point->world_y)) {
                out_points[*count] = point;
                (*count)++;
            }
        }
    } else {
        for(int i = 0; i < 4; i++) {
            if(node->children[i] && !sonar_quad_query(chart, node->children[i], bounds, out_points, max_points, count)) {
                return false;
            }
        }
    }
    
    return true;
}

// Chart lifecycle
SonarChart* sonar_chart_alloc(void) {
    SonarChart* chart = malloc(sizeof(SonarChart));
    if(!chart) return NULL;
    
    // Initialize memory pools with SMALL node pool to reproduce the bug!
    if(!sonar_node_pool_init(&chart->node_pool, TEST_NODE_POOL_SIZE) ||
       !sonar_point_pool_init(&chart->point_pool, SONAR_MAX_POINTS)) {
        free(chart);
        return NULL;
    }
    
    // Create root quadtree node
    SonarBounds root_bounds = sonar_bounds_create(-32768, -32768, 32767, 32767);
    chart->root = sonar_quad_create(chart, root_bounds, 0);
    if(!chart->root) {
        free(chart);
        return NULL;
    }
    
    chart->points_added_this_frame = 0;
    
    return chart;
}

void sonar_chart_free(SonarChart* chart) {
    if(!chart) return;
    
    sonar_quad_free(chart, chart->root);
    sonar_node_pool_cleanup(&chart->node_pool);
    sonar_point_pool_cleanup(&chart->point_pool);
    
    free(chart);
}

bool sonar_chart_add_point(SonarChart* chart, int16_t world_x, int16_t world_y, bool is_terrain) {
    SonarPoint* point = sonar_point_pool_alloc(&chart->point_pool);
    if(!point) return false;
    
    point->world_x = world_x;
    point->world_y = world_y;
    point->discovery_time = furi_get_tick();
    point->is_terrain = is_terrain;
    
    if(sonar_quad_insert(chart, chart->root, point)) {
        chart->points_added_this_frame++;
        return true;
    } else {
        printf("WARNING: Failed to insert point (%d,%d) terrain=%s\n", 
               world_x, world_y, is_terrain ? "TRUE" : "FALSE");
        sonar_point_pool_free(&chart->point_pool, point);
        return false;
    }
}

uint16_t sonar_chart_query_area(SonarChart* chart, SonarBounds bounds, SonarPoint** out_points, uint16_t max_points) {
    uint16_t count = 0;
    sonar_quad_query(chart, chart->root, bounds, out_points, max_points, &count);
    return count;
}

int main() {
    printf("Testing node pool exhaustion bug\n");
    printf("=================================\n");
    printf("Node pool size: %d (same as real implementation)\n\n", TEST_NODE_POOL_SIZE);
    
    SonarChart* chart = sonar_chart_alloc();
    if(!chart) {
        printf("FAIL: Chart allocation failed\n");
        return 1;
    }
    
    // Add many points in a dense area to trigger deep subdivision and exhaust node pool
    printf("Adding dense cluster of terrain points...\n");
    int points_added = 0, points_failed = 0;
    
    // Add the terrain coordinates from the logs
    int terrain_coords[][2] = {
        {66, 51}, {66, 52}, {66, 53}, {66, 48}, {66, 50},
        {66, 47}, {66, 49}, {61, 61}, {66, 45}, {70, 57},
        {63, 61}, {62, 62}, {60, 63}, {57, 63}, {48, 55}
    };
    
    for (int i = 0; i < 15; i++) {
        if(sonar_chart_add_point(chart, terrain_coords[i][0], terrain_coords[i][1], true)) {
            points_added++;
        } else {
            points_failed++;
        }
    }
    
    // Add many water points to exhaust the node pool
    printf("Adding water points to exhaust node pool...\n");
    for (int i = 40; i < 80; i++) {
        for (int j = 40; j < 80; j++) {
            if(sonar_chart_add_point(chart, i, j, false)) {
                points_added++;
            } else {
                points_failed++;
            }
        }
    }
    
    printf("\nResults: %d points added successfully, %d points failed\n", points_added, points_failed);
    printf("Used nodes: %d/%d\n", count_used_nodes(&chart->node_pool), chart->node_pool.pool_size);
    
    // Query the area
    printf("\nQuerying area (-20,-29) to (140,131)...\n");
    SonarBounds query_bounds = sonar_bounds_create(-20, -29, 140, 131);
    SonarPoint* results[100];
    uint16_t total_count = sonar_chart_query_area(chart, query_bounds, results, 100);
    
    int terrain_count = 0, water_count = 0;
    for (int i = 0; i < total_count; i++) {
        if (results[i]->is_terrain) terrain_count++;
        else water_count++;
    }
    
    printf("Query returned: Total=%d, Terrain=%d, Water=%d\n", total_count, terrain_count, water_count);
    
    if(points_failed > 0) {
        printf("\nBUG REPRODUCED: %d points failed to insert due to node pool exhaustion!\n", points_failed);
        printf("This explains why only 1 terrain point is found in the real game.\n");
    } else {
        printf("\nNo insertion failures - bug not reproduced with this data set.\n");
    }
    
    sonar_chart_free(chart);
    return 0;
}