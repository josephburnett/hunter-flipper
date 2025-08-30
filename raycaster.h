#pragma once
#include "engine/engine.h"

// Raycasting configuration - optimized for Flipper Zero memory but good quality
#define RAY_CACHE_SIZE 64           // Reduced from 256 
#define RAY_ANGLE_PRECISION 256     // Reduced from 1024 but still good (2π / 256 ≈ 0.024 radians)
#define RAY_MAX_DISTANCE 48         // Reduced from 64
#define RAY_BATCH_SIZE 4            // Reduced from 8

// Precomputed ray direction
typedef struct {
    int16_t dx;         // X direction (fixed point, scaled by 1000)
    int16_t dy;         // Y direction (fixed point, scaled by 1000)
    uint16_t angle_id;  // Index into angle cache
} RayDirection;

// Ray result for collision detection
typedef struct {
    int16_t hit_x;
    int16_t hit_y;
    uint16_t distance;
    bool hit_terrain;
    bool ray_complete;
} RayResult;

// Cached ray pattern for common sonar operations
typedef struct {
    RayDirection directions[RAY_CACHE_SIZE];
    uint16_t direction_count;
    uint16_t max_radius;
} RayPattern;

// Main raycaster with performance optimizations
typedef struct {
    // Precomputed direction table
    RayDirection angle_cache[RAY_ANGLE_PRECISION];
    
    // Common ray patterns
    RayPattern sonar_pattern_full;      // 360° sonar sweep
    RayPattern sonar_pattern_forward;   // Forward-facing 180° arc
    RayPattern sonar_pattern_sparse;    // Reduced ray count for performance
    
    // Bresenham line algorithm state
    int16_t bresham_x, bresham_y;
    int16_t bresham_dx, bresham_dy;
    int16_t bresham_err;
    int16_t bresham_step_x, bresham_step_y;
    int16_t bresham_x1, bresham_y1;  // Target position for termination check
    bool bresham_steep;
    
    // Performance monitoring
    uint32_t rays_cast_this_frame;
    uint32_t cache_hits_this_frame;
    uint32_t early_exits_this_frame;
    
    // Adaptive quality
    uint8_t current_quality_level;  // 0 = highest, 3 = lowest
    uint32_t last_performance_check;
    uint32_t frame_time_budget_ms;  // Target frame time budget for raycasting
} Raycaster;

// Raycaster lifecycle
Raycaster* raycaster_alloc(void);
void raycaster_free(Raycaster* raycaster);

// Ray pattern initialization
void raycaster_init_angle_cache(Raycaster* raycaster);
void raycaster_init_sonar_patterns(Raycaster* raycaster);
RayPattern raycaster_create_pattern(Raycaster* raycaster, float start_angle, float end_angle, 
                                    uint16_t ray_count, uint16_t max_radius);

// Core raycasting operations
bool raycaster_cast_ray(Raycaster* raycaster, int16_t start_x, int16_t start_y, 
                        RayDirection direction, uint16_t max_distance, RayResult* result,
                        bool (*collision_func)(int16_t x, int16_t y, void* context), void* context);

uint16_t raycaster_cast_pattern(Raycaster* raycaster, RayPattern* pattern, 
                                int16_t start_x, int16_t start_y, RayResult* results,
                                bool (*collision_func)(int16_t x, int16_t y, void* context), void* context);

// Bresenham line algorithm implementation
void raycaster_bresham_init(Raycaster* raycaster, int16_t x0, int16_t y0, int16_t x1, int16_t y1);
bool raycaster_bresham_step(Raycaster* raycaster, int16_t* out_x, int16_t* out_y);

// Performance optimization
void raycaster_set_quality_level(Raycaster* raycaster, uint8_t level);
RayPattern* raycaster_get_adaptive_pattern(Raycaster* raycaster, bool prefer_performance);
void raycaster_update_performance_stats(Raycaster* raycaster, uint32_t frame_start_time);

// Utility functions
RayDirection raycaster_angle_to_direction(float angle_radians);
RayDirection raycaster_get_cached_direction(Raycaster* raycaster, uint16_t angle_id);
float raycaster_direction_to_angle(RayDirection direction);

// Performance monitoring
void raycaster_reset_frame_stats(Raycaster* raycaster);
void raycaster_log_performance(Raycaster* raycaster);