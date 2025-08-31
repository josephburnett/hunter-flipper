#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>

// Mock FURI functions and types for testing
typedef uint32_t FuriTickType;
FuriTickType furi_get_tick() { return 1000; }

// Sonar chart constants and types (extracted from headers)
#define SONAR_QUADTREE_MAX_POINTS 32
#define SONAR_POINT_POOL_SIZE 512

typedef enum {
    SONAR_FADE_FULL = 0,
    SONAR_FADE_HIGH = 1,
    SONAR_FADE_MED = 2,  
    SONAR_FADE_LOW = 3,
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
    int16_t min_x;
    int16_t min_y;
    int16_t max_x;
    int16_t max_y;
} SonarBounds;

typedef struct SonarQuadNode {
    SonarBounds bounds;
    bool is_leaf;
    uint16_t point_count;
    SonarPoint* points[SONAR_QUADTREE_MAX_POINTS];
    struct SonarQuadNode* children[4];
} SonarQuadNode;

typedef struct {
    SonarPoint points[SONAR_POINT_POOL_SIZE];
    uint16_t next_free;
} SonarPointPool;

typedef struct {
    SonarQuadNode* root;
    SonarPointPool point_pool;
    
    // Performance stats
    uint16_t points_added_this_frame;
    uint16_t points_removed_this_frame;
    uint16_t query_count_this_frame;
    uint16_t points_faded_this_frame;
    uint32_t last_fade_update;
} SonarChart;

// Helper functions
static SonarBounds sonar_bounds_create(int16_t min_x, int16_t min_y, int16_t max_x, int16_t max_y) {
    SonarBounds bounds = {min_x, min_y, max_x, max_y};
    return bounds;
}

static bool sonar_bounds_intersect(SonarBounds a, SonarBounds b) {
    return !(a.max_x < b.min_x || a.min_x > b.max_x || 
             a.max_y < b.min_y || a.min_y > b.max_y);
}

static bool sonar_bounds_contains_point(SonarBounds bounds, int16_t x, int16_t y) {
    return x >= bounds.min_x && x <= bounds.max_x && 
           y >= bounds.min_y && y <= bounds.max_y;
}

// Point pool functions
static void sonar_point_pool_init(SonarPointPool* pool) {
    pool->next_free = 0;
}

static SonarPoint* sonar_point_pool_alloc(SonarPointPool* pool) {
    if (pool->next_free >= SONAR_POINT_POOL_SIZE) return NULL;
    return &pool->points[pool->next_free++];
}

static void sonar_point_pool_free(SonarPointPool* pool, SonarPoint* point) {
    // Simple implementation - just mark as unused
    (void)pool; (void)point;
}

// Quadtree node functions
static SonarQuadNode* sonar_quad_create(SonarBounds bounds) {
    SonarQuadNode* node = malloc(sizeof(SonarQuadNode));
    if (!node) return NULL;
    
    node->bounds = bounds;
    node->is_leaf = true;
    node->point_count = 0;
    for (int i = 0; i < 4; i++) {
        node->children[i] = NULL;
    }
    return node;
}

static void sonar_quad_free(SonarQuadNode* node) {
    if (!node) return;
    
    if (!node->is_leaf) {
        for (int i = 0; i < 4; i++) {
            sonar_quad_free(node->children[i]);
        }
    }
    free(node);
}

static bool sonar_quad_split(SonarChart* chart, SonarQuadNode* node) {
    if (!node->is_leaf) return true; // Already split
    
    int16_t mid_x = (node->bounds.min_x + node->bounds.max_x) / 2;
    int16_t mid_y = (node->bounds.min_y + node->bounds.max_y) / 2;
    
    // Create four child quadrants
    node->children[0] = sonar_quad_create(sonar_bounds_create(node->bounds.min_x, node->bounds.min_y, mid_x, mid_y));
    node->children[1] = sonar_quad_create(sonar_bounds_create(mid_x + 1, node->bounds.min_y, node->bounds.max_x, mid_y));
    node->children[2] = sonar_quad_create(sonar_bounds_create(node->bounds.min_x, mid_y + 1, mid_x, node->bounds.max_y));
    node->children[3] = sonar_quad_create(sonar_bounds_create(mid_x + 1, mid_y + 1, node->bounds.max_x, node->bounds.max_y));
    
    // Check allocation
    for (int i = 0; i < 4; i++) {
        if (!node->children[i]) return false;
    }
    
    // Redistribute points to children
    for (uint16_t i = 0; i < node->point_count; i++) {
        SonarPoint* point = node->points[i];
        
        // Find which child should contain this point
        for (int j = 0; j < 4; j++) {
            if (sonar_bounds_contains_point(node->children[j]->bounds, point->world_x, point->world_y)) {
                if (node->children[j]->point_count < SONAR_QUADTREE_MAX_POINTS) {
                    node->children[j]->points[node->children[j]->point_count++] = point;
                }
                break;
            }
        }
    }
    
    // Clear parent points and mark as non-leaf
    node->point_count = 0;
    node->is_leaf = false;
    
    return true;
}

static bool sonar_quad_insert(SonarChart* chart, SonarQuadNode* node, SonarPoint* point) {
    if (!sonar_bounds_contains_point(node->bounds, point->world_x, point->world_y)) {
        return false; // Point outside bounds
    }
    
    if (node->is_leaf) {
        // If there's room, add the point
        if (node->point_count < SONAR_QUADTREE_MAX_POINTS) {
            node->points[node->point_count++] = point;
            return true;
        }
        
        // Need to split
        if (!sonar_quad_split(chart, node)) {
            return false;
        }
    }
    
    // Insert into appropriate child
    for (int i = 0; i < 4; i++) {
        if (sonar_bounds_contains_point(node->children[i]->bounds, point->world_x, point->world_y)) {
            return sonar_quad_insert(chart, node->children[i], point);
        }
    }
    
    return false;
}

static bool sonar_quad_query(SonarChart* chart, SonarQuadNode* node, SonarBounds bounds, 
                      SonarPoint** out_points, uint16_t max_points, uint16_t* count) {
    if (!sonar_bounds_intersect(node->bounds, bounds)) {
        return true; // Continue searching other branches
    }
    
    if (node->is_leaf) {
        for (uint16_t i = 0; i < node->point_count; i++) {
            if (*count >= max_points) return false; // Buffer full
            
            SonarPoint* point = node->points[i];
            if (sonar_bounds_contains_point(bounds, point->world_x, point->world_y)) {
                out_points[*count] = point;
                (*count)++;
            }
        }
    } else {
        for (int i = 0; i < 4; i++) {
            if (!sonar_quad_query(chart, node->children[i], bounds, out_points, max_points, count)) {
                return false; // Buffer full
            }
        }
    }
    
    return true;
}

// Main sonar chart functions
static SonarChart* sonar_chart_alloc() {
    SonarChart* chart = malloc(sizeof(SonarChart));
    if (!chart) return NULL;
    
    // Initialize with large bounds to contain all possible points
    chart->root = sonar_quad_create(sonar_bounds_create(-32768, -32768, 32767, 32767));
    if (!chart->root) {
        free(chart);
        return NULL;
    }
    
    sonar_point_pool_init(&chart->point_pool);
    
    chart->points_added_this_frame = 0;
    chart->points_removed_this_frame = 0;
    chart->query_count_this_frame = 0;
    chart->points_faded_this_frame = 0;
    chart->last_fade_update = 0;
    
    return chart;
}

static void sonar_chart_free(SonarChart* chart) {
    if (!chart) return;
    sonar_quad_free(chart->root);
    free(chart);
}

static bool sonar_chart_query_point(SonarChart* chart, int16_t world_x, int16_t world_y, SonarPoint** out_point) {
    SonarBounds query_bounds = sonar_bounds_create(world_x, world_y, world_x, world_y);
    SonarPoint* nearby_points[1];
    uint16_t count = 0;
    
    sonar_quad_query(chart, chart->root, query_bounds, nearby_points, 1, &count);
    
    if (count > 0) {
        *out_point = nearby_points[0];
        return true;
    }
    
    return false;
}

static bool sonar_chart_add_point(SonarChart* chart, int16_t world_x, int16_t world_y, bool is_terrain) {
    // Check if point already exists
    SonarPoint* existing;
    if (sonar_chart_query_point(chart, world_x, world_y, &existing)) {
        // Update existing point
        existing->discovery_time = furi_get_tick();
        existing->fade_state = SONAR_FADE_FULL;
        // Preserve terrain flag: once terrain, always terrain (terrain overrides water)
        if (is_terrain) {
            existing->is_terrain = true;
        }
        // If adding water to existing water point, keep it as water (no change needed)
        return true;
    }
    
    // Allocate new point
    SonarPoint* point = sonar_point_pool_alloc(&chart->point_pool);
    if (!point) return false; // Memory exhausted
    
    point->world_x = world_x;
    point->world_y = world_y;
    point->discovery_time = furi_get_tick();
    point->fade_state = SONAR_FADE_FULL;
    point->is_terrain = is_terrain;
    
    // Insert into quadtree
    if (sonar_quad_insert(chart, chart->root, point)) {
        chart->points_added_this_frame++;
        return true;
    } else {
        // Failed to insert, free the point
        sonar_point_pool_free(&chart->point_pool, point);
        return false;
    }
}

static uint16_t sonar_chart_query_area(SonarChart* chart, SonarBounds bounds, SonarPoint** out_points, uint16_t max_points) {
    uint16_t count = 0;
    sonar_quad_query(chart, chart->root, bounds, out_points, max_points, &count);
    chart->query_count_this_frame++;
    return count;
}

// Test framework
#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            printf("FAIL: %s - %s\n", __func__, message); \
            return false; \
        } \
    } while(0)

#define TEST_PASS() \
    do { \
        printf("PASS: %s\n", __func__); \
        return true; \
    } while(0)

// Test helper functions
static void print_quadtree_structure(SonarQuadNode* node, int depth) {
    if (!node) return;
    
    for (int i = 0; i < depth; i++) printf("  ");
    printf("Node bounds: (%d,%d) to (%d,%d), points: %d, leaf: %s\n",
           node->bounds.min_x, node->bounds.min_y, 
           node->bounds.max_x, node->bounds.max_y,
           node->point_count, node->is_leaf ? "true" : "false");
    
    if (node->is_leaf) {
        for (uint16_t i = 0; i < node->point_count; i++) {
            SonarPoint* p = node->points[i];
            for (int j = 0; j < depth + 1; j++) printf("  ");
            printf("Point %d: (%d,%d) terrain=%s\n", i, p->world_x, p->world_y, 
                   p->is_terrain ? "true" : "false");
        }
    } else {
        for (int i = 0; i < 4; i++) {
            if (node->children[i]) {
                print_quadtree_structure(node->children[i], depth + 1);
            }
        }
    }
}

static void print_query_results(SonarPoint** results, uint16_t count) {
    printf("Query returned %d points:\n", count);
    for (uint16_t i = 0; i < count; i++) {
        printf("  %d: (%d,%d) terrain=%s\n", i, 
               results[i]->world_x, results[i]->world_y,
               results[i]->is_terrain ? "true" : "false");
    }
}

// Test the specific bug scenario: multiple terrain points, only finding one
bool test_bug_reproduction() {
    SonarChart* chart = sonar_chart_alloc();
    
    printf("\n=== BUG REPRODUCTION TEST ===\n");
    
    // Simulate the exact scenario from the logs
    // Add terrain points at various coordinates
    int terrain_coords[][2] = {
        {66, 51}, {66, 52}, {66, 53}, {66, 48}, {66, 50},
        {66, 47}, {66, 49}, {61, 61}, {66, 45}, {70, 57},
        {63, 61}, {62, 62}, {60, 63}, {57, 63}, {48, 55}
    };
    
    for (int i = 0; i < 15; i++) {
        bool result = sonar_chart_add_point(chart, terrain_coords[i][0], terrain_coords[i][1], true);
        TEST_ASSERT(result == true, "Failed to add terrain point");
        printf("Added terrain point at (%d,%d)\n", terrain_coords[i][0], terrain_coords[i][1]);
    }
    
    // Add water points along ray paths (simulating the collision callback)
    printf("\nAdding water points...\n");
    for (int i = 60; i < 66; i++) {
        for (int j = 51; j <= 53; j++) {
            sonar_chart_add_point(chart, i, j, false);  // water
            printf("Added water point at (%d,%d)\n", i, j);
        }
    }
    
    printf("\nQuadtree structure after adding points:\n");
    print_quadtree_structure(chart->root, 0);
    
    // Query the same area as in the logs: (-20,-29) to (140,131)
    SonarBounds query_bounds = sonar_bounds_create(-20, -29, 140, 131);
    SonarPoint* results[50];
    uint16_t total_count = sonar_chart_query_area(chart, query_bounds, results, 50);
    
    printf("\nQuery results for bounds (-20,-29) to (140,131):\n");
    print_query_results(results, total_count);
    
    // Count terrain vs water
    int terrain_count = 0, water_count = 0;
    for (int i = 0; i < total_count; i++) {
        if (results[i]->is_terrain) terrain_count++;
        else water_count++;
    }
    
    printf("\nTotal points: %d, Terrain: %d, Water: %d\n", total_count, terrain_count, water_count);
    
    // The bug: we should find many terrain points, but we're only finding 1
    TEST_ASSERT(total_count > 20, "Should find many points total");
    TEST_ASSERT(terrain_count >= 10, "Should find many terrain points, not just 1!");
    
    sonar_chart_free(chart);
    TEST_PASS();
}

bool test_simple_terrain_query() {
    SonarChart* chart = sonar_chart_alloc();
    
    printf("\n=== SIMPLE TERRAIN QUERY TEST ===\n");
    
    // Add a few terrain points
    sonar_chart_add_point(chart, 100, 200, true);   // terrain
    sonar_chart_add_point(chart, 110, 210, true);   // terrain  
    sonar_chart_add_point(chart, 120, 220, true);   // terrain
    
    printf("Added 3 terrain points\n");
    print_quadtree_structure(chart->root, 0);
    
    // Query the area
    SonarBounds bounds = sonar_bounds_create(50, 150, 200, 300);
    SonarPoint* results[10];
    uint16_t count = sonar_chart_query_area(chart, bounds, results, 10);
    
    printf("\nQuery results:\n");
    print_query_results(results, count);
    
    TEST_ASSERT(count == 3, "Should find all 3 terrain points");
    
    sonar_chart_free(chart);
    TEST_PASS();
}

int main() {
    printf("Running SonarChart Quadtree Unit Tests\n");
    printf("=====================================\n\n");
    
    int passed = 0, total = 0;
    
    #define RUN_TEST(test) \
        do { \
            total++; \
            if (test()) passed++; \
            printf("\n"); \
        } while(0)
    
    RUN_TEST(test_simple_terrain_query);
    RUN_TEST(test_bug_reproduction);  // This should reveal the bug
    
    printf("=====================================\n");
    printf("Test Results: %d/%d passed\n", passed, total);
    
    if (passed == total) {
        printf("All tests PASSED! 🎉\n");
        return 0;
    } else {
        printf("Some tests FAILED! 💥\n");
        return 1;
    }
}