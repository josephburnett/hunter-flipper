#include "sonar_chart.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

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

// Memory pool management
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

// Fade management
SonarFadeState sonar_chart_get_fade_state(SonarPoint* point, uint32_t current_time) {
    uint32_t age = current_time - point->discovery_time;
    uint32_t stage = age / SONAR_FADE_DURATION_MS;
    
    if(stage >= SONAR_FADE_STAGES) {
        return SONAR_FADE_GONE;
    }
    
    return (SonarFadeState)stage;
}

uint8_t sonar_fade_state_opacity(SonarFadeState state) {
    switch(state) {
        case SONAR_FADE_FULL:   return 255; // 100%
        case SONAR_FADE_BRIGHT: return 192; // 75%
        case SONAR_FADE_DIM:    return 128; // 50%
        case SONAR_FADE_FAINT:  return 64;  // 25%
        default:                return 0;   // 0%
    }
}

// Quadtree operations
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

static void sonar_quad_subdivide(SonarChart* chart, SonarQuadNode* node) {
    if(!node->is_leaf || node->depth >= SONAR_QUADTREE_MAX_DEPTH) return;
    
    int16_t mid_x = (node->bounds.min_x + node->bounds.max_x) / 2;
    int16_t mid_y = (node->bounds.min_y + node->bounds.max_y) / 2;
    
    // Create four child nodes - ensure no gaps in coverage
    // NW: top-left quadrant
    node->children[0] = sonar_quad_create(chart, 
        sonar_bounds_create(node->bounds.min_x, node->bounds.min_y, mid_x, mid_y), 
        node->depth + 1); // NW
    // NE: top-right quadrant (include mid_x boundary)
    node->children[1] = sonar_quad_create(chart, 
        sonar_bounds_create(mid_x, node->bounds.min_y, node->bounds.max_x, mid_y), 
        node->depth + 1); // NE
    // SW: bottom-left quadrant (include mid_y boundary)
    node->children[2] = sonar_quad_create(chart, 
        sonar_bounds_create(node->bounds.min_x, mid_y, mid_x, node->bounds.max_y), 
        node->depth + 1); // SW
    // SE: bottom-right quadrant (include both mid boundaries)
    node->children[3] = sonar_quad_create(chart, 
        sonar_bounds_create(mid_x, mid_y, node->bounds.max_x, node->bounds.max_y), 
        node->depth + 1); // SE
    
    // Check if any children failed to allocate
    for(int i = 0; i < 4; i++) {
        if(!node->children[i]) {
            // Clean up and abort subdivision
            for(int j = 0; j < i; j++) {
                sonar_quad_free(chart, node->children[j]);
                node->children[j] = NULL;
            }
            return;
        }
    }
    
    node->is_leaf = false;
    
    // Redistribute points to children
    for(uint16_t i = 0; i < node->point_count; i++) {
        SonarPoint* point = node->points[i];
        bool point_placed = false;
        for(int j = 0; j < 4; j++) {
            if(sonar_bounds_contains_point(node->children[j]->bounds, point->world_x, point->world_y)) {
                sonar_quad_insert(chart, node->children[j], point);
                point_placed = true;
                break;
            }
        }
        
        // DEBUG: Log if a point gets lost during subdivision
        if(!point_placed) {
            FURI_LOG_E("QUAD_BUG", "POINT LOST in subdivision! Point (%d,%d) %s doesn't fit in any child", 
                      point->world_x, point->world_y, point->is_terrain ? "TERRAIN" : "water");
            FURI_LOG_E("QUAD_BUG", "Parent bounds: (%d,%d) to (%d,%d)", 
                      node->bounds.min_x, node->bounds.min_y, node->bounds.max_x, node->bounds.max_y);
            for(int j = 0; j < 4; j++) {
                FURI_LOG_E("QUAD_BUG", "Child %d bounds: (%d,%d) to (%d,%d)", j,
                          node->children[j]->bounds.min_x, node->children[j]->bounds.min_y,
                          node->children[j]->bounds.max_x, node->children[j]->bounds.max_y);
            }
        }
    }
    
    node->point_count = 0;
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
            // Need to subdivide
            sonar_quad_subdivide(chart, node);
            if(node->is_leaf) {
                // Subdivision failed, force insert anyway
                if(node->point_count < SONAR_QUADTREE_MAX_POINTS) {
                    node->points[node->point_count] = point;
                    node->point_count++;
                }
                return false;
            }
        }
    }
    
    // Insert into appropriate child
    for(int i = 0; i < 4; i++) {
        if(sonar_quad_insert(chart, node->children[i], point)) {
            return true;
        }
    }
    
    // CRITICAL FIX: If no child can accept the point, force insert into parent
    // This prevents point loss when subdivision boundaries fail
    if(node->point_count < SONAR_QUADTREE_MAX_POINTS) {
        node->points[node->point_count] = point;
        node->point_count++;
        return true;
    }
    
    return false;
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
    } else {
        // CRITICAL FIX: Check points in the parent node first
        // This handles points that were force-inserted into parent when child insertion failed
        for(uint16_t i = 0; i < node->point_count; i++) {
            if(*count >= max_points) return false; // Buffer full
            
            SonarPoint* point = node->points[i];
            if(sonar_bounds_contains_point(bounds, point->world_x, point->world_y)) {
                out_points[*count] = point;
                (*count)++;
            }
        }
        
        // Then check child nodes
        for(int i = 0; i < 4; i++) {
            if(node->children[i] && !sonar_quad_query(chart, node->children[i], bounds, out_points, max_points, count)) {
                return false; // Buffer full
            }
        }
    }
    
    return true;
}

void sonar_quad_cleanup_faded(SonarChart* chart, SonarQuadNode* node, uint32_t current_time) {
    if(node->is_leaf) {
        // Remove faded points from leaf nodes
        uint16_t write_index = 0;
        for(uint16_t read_index = 0; read_index < node->point_count; read_index++) {
            SonarPoint* point = node->points[read_index];
            SonarFadeState state = sonar_chart_get_fade_state(point, current_time);
            
            if(state >= SONAR_FADE_GONE) {
                // Free the point
                sonar_point_pool_free(&chart->point_pool, point);
                chart->points_removed_this_frame++;
            } else {
                // Update fade state and keep point
                point->fade_state = state;
                node->points[write_index] = point;
                write_index++;
            }
        }
        node->point_count = write_index;
    } else {
        // Recursively clean children
        for(int i = 0; i < 4; i++) {
            sonar_quad_cleanup_faded(chart, node->children[i], current_time);
        }
    }
}

// Sonar chart lifecycle
SonarChart* sonar_chart_alloc(void) {
    SonarChart* chart = malloc(sizeof(SonarChart));
    if(!chart) return NULL;
    
    // Initialize memory pools - increased node pool size to prevent subdivision failures
    if(!sonar_node_pool_init(&chart->node_pool, 128) ||
       !sonar_point_pool_init(&chart->point_pool, SONAR_MAX_POINTS)) {
        sonar_chart_free(chart);
        return NULL;
    }
    
    // Create root quadtree node with large bounds
    SonarBounds root_bounds = sonar_bounds_create(-32768, -32768, 32767, 32767);
    chart->root = sonar_quad_create(chart, root_bounds, 0);
    if(!chart->root) {
        sonar_chart_free(chart);
        return NULL;
    }
    
    // Initialize other fields
    chart->last_fade_update = 0;
    chart->cache_count = 0;
    sonar_chart_reset_frame_stats(chart);
    
    return chart;
}

void sonar_chart_free(SonarChart* chart) {
    if(!chart) return;
    
    if(chart->root) {
        sonar_quad_free(chart, chart->root);
    }
    
    sonar_node_pool_cleanup(&chart->node_pool);
    sonar_point_pool_cleanup(&chart->point_pool);
    
    free(chart);
}

// Core sonar operations
bool sonar_chart_add_point(SonarChart* chart, int16_t world_x, int16_t world_y, bool is_terrain) {
    // Debug: Log all terrain point additions
    static int add_debug_count = 0;
    if(is_terrain && add_debug_count < 20) {
        FURI_LOG_I("CHART_ADD", "Adding terrain point at (%d,%d)", world_x, world_y);
        add_debug_count++;
    }
    
    // Check if point already exists (within 1 unit tolerance)
    SonarPoint* existing;
    if(sonar_chart_query_point(chart, world_x, world_y, &existing)) {
        // Debug: Log when we find existing point
        if(is_terrain && add_debug_count < 20) {
            FURI_LOG_I("CHART_ADD", "Found existing point at (%d,%d) for new point (%d,%d)", 
                      existing->world_x, existing->world_y, world_x, world_y);
        }
        
        // Update existing point
        existing->discovery_time = furi_get_tick();
        existing->fade_state = SONAR_FADE_FULL;
        // Preserve terrain flag: once terrain, always terrain (terrain overrides water)
        if(is_terrain) {
            existing->is_terrain = true;
        }
        // If adding water to existing water point, keep it as water (no change needed)
        return true;
    }
    
    // Allocate new point
    SonarPoint* point = sonar_point_pool_alloc(&chart->point_pool);
    if(!point) return false; // Memory exhausted
    
    point->world_x = world_x;
    point->world_y = world_y;
    point->discovery_time = furi_get_tick();
    point->fade_state = SONAR_FADE_FULL;
    point->is_terrain = is_terrain;
    
    // Debug: Log successful new point creation
    if(is_terrain && add_debug_count <= 20) {
        FURI_LOG_I("CHART_ADD", "Created NEW terrain point at (%d,%d)", world_x, world_y);
    }
    
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

bool sonar_chart_query_point(SonarChart* chart, int16_t world_x, int16_t world_y, SonarPoint** out_point) {
    SonarBounds query_bounds = sonar_bounds_create(world_x, world_y, world_x, world_y);
    SonarPoint* nearby_points[9];
    uint16_t count = 0;
    
    sonar_quad_query(chart, chart->root, query_bounds, nearby_points, 9, &count);
    
    // Find closest point
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
    
    if(best_point && best_distance <= 0) {
        *out_point = best_point;
        return true;
    }
    
    return false;
}

uint16_t sonar_chart_query_area(SonarChart* chart, SonarBounds bounds, SonarPoint** out_points, uint16_t max_points) {
    uint16_t count = 0;
    sonar_quad_query(chart, chart->root, bounds, out_points, max_points, &count);
    chart->query_count_this_frame++;
    return count;
}

void sonar_chart_update_fade(SonarChart* chart, uint32_t current_time) {
    if(current_time - chart->last_fade_update < 1000) { // Update every second
        return;
    }
    
    chart->last_fade_update = current_time;
    chart->points_faded_this_frame = 0;
    
    sonar_quad_cleanup_faded(chart, chart->root, current_time);
}

// Performance monitoring
void sonar_chart_reset_frame_stats(SonarChart* chart) {
    chart->points_added_this_frame = 0;
    chart->points_removed_this_frame = 0;
    chart->query_count_this_frame = 0;
    chart->points_faded_this_frame = 0;
}

void sonar_chart_log_performance(SonarChart* chart) {
    // This would normally log to debug output
    sonar_chart_reset_frame_stats(chart);
}