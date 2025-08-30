#pragma once
#include "engine/engine.h"

// Sonar chart configuration - optimized for Flipper Zero memory
#define SONAR_QUADTREE_MAX_DEPTH 6      // Reduced from 8
#define SONAR_QUADTREE_MAX_POINTS 8     // Reduced from 16
#define SONAR_FADE_STAGES 4
#define SONAR_FADE_DURATION_MS 15000    // 15 seconds per stage
#define SONAR_MAX_POINTS 512            // Reduced from 2048

// Sonar point fade states
typedef enum {
    SONAR_FADE_FULL = 0,    // 100% opacity
    SONAR_FADE_BRIGHT = 1,  // 75% opacity
    SONAR_FADE_DIM = 2,     // 50% opacity
    SONAR_FADE_FAINT = 3,   // 25% opacity
    SONAR_FADE_GONE = 4     // Removed from memory
} SonarFadeState;

// Individual sonar discovery point
typedef struct {
    int16_t world_x;
    int16_t world_y;
    uint32_t discovery_time;
    SonarFadeState fade_state;
    bool is_terrain;        // true for terrain, false for water
} SonarPoint;

// Quadtree bounds
typedef struct {
    int16_t min_x, min_y;
    int16_t max_x, max_y;
} SonarBounds;

// Forward declaration
struct SonarQuadNode;

// Quadtree node
typedef struct SonarQuadNode {
    SonarBounds bounds;
    SonarPoint* points[SONAR_QUADTREE_MAX_POINTS];
    uint16_t point_count;
    
    struct SonarQuadNode* children[4]; // NW, NE, SW, SE
    bool is_leaf;
    uint8_t depth;
} SonarQuadNode;

// Memory pool for quadtree nodes
typedef struct {
    SonarQuadNode* nodes;
    bool* node_in_use;
    uint16_t pool_size;
    uint16_t next_free;
} SonarNodePool;

// Memory pool for sonar points
typedef struct {
    SonarPoint* points;
    bool* point_in_use;
    uint16_t pool_size;
    uint16_t next_free;
    uint16_t active_count;
} SonarPointPool;

// Main sonar chart manager
typedef struct {
    SonarQuadNode* root;
    SonarNodePool node_pool;
    SonarPointPool point_pool;
    
    // Fade management
    uint32_t last_fade_update;
    uint32_t points_faded_this_frame;
    
    // Spatial query cache
    SonarPoint* query_cache[256];
    uint16_t cache_count;
    SonarBounds last_query_bounds;
    
    // Performance monitoring
    uint16_t points_added_this_frame;
    uint16_t points_removed_this_frame;
    uint16_t query_count_this_frame;
} SonarChart;

// Sonar chart lifecycle
SonarChart* sonar_chart_alloc(void);
void sonar_chart_free(SonarChart* chart);

// Core sonar operations
bool sonar_chart_add_point(SonarChart* chart, int16_t world_x, int16_t world_y, bool is_terrain);
bool sonar_chart_query_point(SonarChart* chart, int16_t world_x, int16_t world_y, SonarPoint** out_point);
uint16_t sonar_chart_query_area(SonarChart* chart, SonarBounds bounds, SonarPoint** out_points, uint16_t max_points);

// Fade management
void sonar_chart_update_fade(SonarChart* chart, uint32_t current_time);
SonarFadeState sonar_chart_get_fade_state(SonarPoint* point, uint32_t current_time);
uint8_t sonar_fade_state_opacity(SonarFadeState state);

// Quadtree operations
SonarQuadNode* sonar_quad_create(SonarChart* chart, SonarBounds bounds, uint8_t depth);
void sonar_quad_free(SonarChart* chart, SonarQuadNode* node);
bool sonar_quad_insert(SonarChart* chart, SonarQuadNode* node, SonarPoint* point);
bool sonar_quad_query(SonarChart* chart, SonarQuadNode* node, SonarBounds bounds, 
                      SonarPoint** out_points, uint16_t max_points, uint16_t* count);
void sonar_quad_cleanup_faded(SonarChart* chart, SonarQuadNode* node, uint32_t current_time);

// Memory pool management
bool sonar_node_pool_init(SonarNodePool* pool, uint16_t size);
void sonar_node_pool_cleanup(SonarNodePool* pool);
SonarQuadNode* sonar_node_pool_alloc(SonarNodePool* pool);
void sonar_node_pool_free(SonarNodePool* pool, SonarQuadNode* node);

bool sonar_point_pool_init(SonarPointPool* pool, uint16_t size);
void sonar_point_pool_cleanup(SonarPointPool* pool);
SonarPoint* sonar_point_pool_alloc(SonarPointPool* pool);
void sonar_point_pool_free(SonarPointPool* pool, SonarPoint* point);

// Utility functions
bool sonar_bounds_intersect(SonarBounds a, SonarBounds b);
bool sonar_bounds_contains_point(SonarBounds bounds, int16_t x, int16_t y);
SonarBounds sonar_bounds_create(int16_t min_x, int16_t min_y, int16_t max_x, int16_t max_y);

// Performance monitoring
void sonar_chart_reset_frame_stats(SonarChart* chart);
void sonar_chart_log_performance(SonarChart* chart);