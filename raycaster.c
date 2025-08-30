#include "raycaster.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265359f
#define TWO_PI (2.0f * PI)
#define FIXED_POINT_SCALE 1000

// Utility functions
RayDirection raycaster_angle_to_direction(float angle_radians) {
    RayDirection dir;
    dir.dx = (int16_t)(cosf(angle_radians) * FIXED_POINT_SCALE);
    dir.dy = (int16_t)(sinf(angle_radians) * FIXED_POINT_SCALE);
    dir.angle_id = (uint16_t)((angle_radians / TWO_PI) * RAY_ANGLE_PRECISION) % RAY_ANGLE_PRECISION;
    return dir;
}

RayDirection raycaster_get_cached_direction(Raycaster* raycaster, uint16_t angle_id) {
    if(angle_id >= RAY_ANGLE_PRECISION) angle_id = angle_id % RAY_ANGLE_PRECISION;
    return raycaster->angle_cache[angle_id];
}

float raycaster_direction_to_angle(RayDirection direction) {
    return atan2f((float)direction.dy / FIXED_POINT_SCALE, (float)direction.dx / FIXED_POINT_SCALE);
}

// Raycaster initialization
void raycaster_init_angle_cache(Raycaster* raycaster) {
    for(uint16_t i = 0; i < RAY_ANGLE_PRECISION; i++) {
        float angle = (float)i * TWO_PI / RAY_ANGLE_PRECISION;
        raycaster->angle_cache[i] = raycaster_angle_to_direction(angle);
        raycaster->angle_cache[i].angle_id = i;
    }
}

RayPattern raycaster_create_pattern(Raycaster* raycaster, float start_angle, float end_angle, 
                                    uint16_t ray_count, uint16_t max_radius) {
    (void)raycaster; // Suppress unused parameter warning
    RayPattern pattern;
    pattern.max_radius = max_radius;
    pattern.direction_count = 0;
    
    if(ray_count > RAY_CACHE_SIZE) ray_count = RAY_CACHE_SIZE;
    
    float angle_range = end_angle - start_angle;
    if(angle_range < 0) angle_range += TWO_PI;
    
    for(uint16_t i = 0; i < ray_count && pattern.direction_count < RAY_CACHE_SIZE; i++) {
        float angle = start_angle + (angle_range * i) / (ray_count - 1);
        if(angle >= TWO_PI) angle -= TWO_PI;
        if(angle < 0) angle += TWO_PI;
        
        pattern.directions[pattern.direction_count] = raycaster_angle_to_direction(angle);
        pattern.direction_count++;
    }
    
    return pattern;
}

void raycaster_init_sonar_patterns(Raycaster* raycaster) {
    // Full 360° pattern with 32 rays (reduced for memory)
    raycaster->sonar_pattern_full = raycaster_create_pattern(raycaster, 0, TWO_PI, 32, RAY_MAX_DISTANCE);
    
    // Forward-facing 180° arc with 16 rays
    raycaster->sonar_pattern_forward = raycaster_create_pattern(raycaster, -PI/2, PI/2, 16, RAY_MAX_DISTANCE);
    
    // Sparse pattern with 8 rays for performance
    raycaster->sonar_pattern_sparse = raycaster_create_pattern(raycaster, 0, TWO_PI, 8, RAY_MAX_DISTANCE / 2);
}

// Bresenham line algorithm
void raycaster_bresham_init(Raycaster* raycaster, int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
    raycaster->bresham_x = x0;
    raycaster->bresham_y = y0;
    
    raycaster->bresham_dx = abs(x1 - x0);
    raycaster->bresham_dy = abs(y1 - y0);
    
    raycaster->bresham_step_x = (x0 < x1) ? 1 : -1;
    raycaster->bresham_step_y = (y0 < y1) ? 1 : -1;
    
    raycaster->bresham_steep = raycaster->bresham_dy > raycaster->bresham_dx;
    
    if(raycaster->bresham_steep) {
        raycaster->bresham_err = raycaster->bresham_dx - raycaster->bresham_dy;
    } else {
        raycaster->bresham_err = raycaster->bresham_dx - raycaster->bresham_dy;
    }
}

bool raycaster_bresham_step(Raycaster* raycaster, int16_t* out_x, int16_t* out_y) {
    *out_x = raycaster->bresham_x;
    *out_y = raycaster->bresham_y;
    
    if(raycaster->bresham_dx == 0 && raycaster->bresham_dy == 0) {
        return false; // Ray complete
    }
    
    int16_t e2 = 2 * raycaster->bresham_err;
    
    if(e2 > -raycaster->bresham_dy) {
        raycaster->bresham_err -= raycaster->bresham_dy;
        raycaster->bresham_x += raycaster->bresham_step_x;
        raycaster->bresham_dx--;
    }
    
    if(e2 < raycaster->bresham_dx) {
        raycaster->bresham_err += raycaster->bresham_dx;
        raycaster->bresham_y += raycaster->bresham_step_y;
        raycaster->bresham_dy--;
    }
    
    return true;
}

// Core raycasting
bool raycaster_cast_ray(Raycaster* raycaster, int16_t start_x, int16_t start_y, 
                        RayDirection direction, uint16_t max_distance, RayResult* result,
                        bool (*collision_func)(int16_t x, int16_t y, void* context), void* context) {
    
    result->ray_complete = false;
    result->hit_terrain = false;
    result->distance = 0;
    
    // Calculate end point using fixed-point arithmetic
    int32_t end_x = start_x + ((int32_t)direction.dx * max_distance) / FIXED_POINT_SCALE;
    int32_t end_y = start_y + ((int32_t)direction.dy * max_distance) / FIXED_POINT_SCALE;
    
    // Clamp to reasonable bounds
    if(end_x < -32768) end_x = -32768;
    if(end_x > 32767) end_x = 32767;
    if(end_y < -32768) end_y = -32768;
    if(end_y > 32767) end_y = 32767;
    
    // Initialize Bresenham algorithm
    raycaster_bresham_init(raycaster, start_x, start_y, (int16_t)end_x, (int16_t)end_y);
    
    int16_t current_x, current_y;
    uint16_t step_count = 0;
    
    while(raycaster_bresham_step(raycaster, &current_x, &current_y) && step_count < max_distance) {
        step_count++;
        raycaster->rays_cast_this_frame++;
        
        // Check for collision
        if(collision_func && collision_func(current_x, current_y, context)) {
            result->hit_x = current_x;
            result->hit_y = current_y;
            result->distance = step_count;
            result->hit_terrain = true;
            result->ray_complete = true;
            raycaster->early_exits_this_frame++;
            return true;
        }
        
        // Early exit if we're way outside reasonable bounds
        if(abs(current_x) > 10000 || abs(current_y) > 10000) {
            raycaster->early_exits_this_frame++;
            break;
        }
    }
    
    // Ray completed without hitting terrain
    result->hit_x = current_x;
    result->hit_y = current_y;
    result->distance = step_count;
    result->ray_complete = true;
    
    return false; // No collision found
}

uint16_t raycaster_cast_pattern(Raycaster* raycaster, RayPattern* pattern, 
                                int16_t start_x, int16_t start_y, RayResult* results,
                                bool (*collision_func)(int16_t x, int16_t y, void* context), void* context) {
    uint16_t hits = 0;
    
    for(uint16_t i = 0; i < pattern->direction_count; i++) {
        if(raycaster_cast_ray(raycaster, start_x, start_y, pattern->directions[i], 
                              pattern->max_radius, &results[i], collision_func, context)) {
            hits++;
        }
        
        // Adaptive quality: skip rays if we're over budget
        if(raycaster->current_quality_level > 0 && (i % (raycaster->current_quality_level + 1)) != 0) {
            continue;
        }
    }
    
    return hits;
}

// Performance optimization
void raycaster_set_quality_level(Raycaster* raycaster, uint8_t level) {
    if(level > 3) level = 3;
    raycaster->current_quality_level = level;
    
    // Adjust frame time budget based on quality
    uint32_t base_budget = 5; // 5ms base budget
    raycaster->frame_time_budget_ms = base_budget >> level; // Halve budget for each level
    if(raycaster->frame_time_budget_ms < 1) raycaster->frame_time_budget_ms = 1;
}

RayPattern* raycaster_get_adaptive_pattern(Raycaster* raycaster, bool prefer_performance) {
    switch(raycaster->current_quality_level) {
        case 0: return prefer_performance ? &raycaster->sonar_pattern_forward : &raycaster->sonar_pattern_full;
        case 1: return &raycaster->sonar_pattern_forward;
        case 2: 
        case 3: return &raycaster->sonar_pattern_sparse;
        default: return &raycaster->sonar_pattern_sparse;
    }
}

void raycaster_update_performance_stats(Raycaster* raycaster, uint32_t frame_start_time) {
    uint32_t current_time = furi_get_tick();
    uint32_t frame_time = current_time - frame_start_time;
    
    if(current_time - raycaster->last_performance_check > 1000) { // Check every second
        // Adjust quality based on performance
        if(frame_time > raycaster->frame_time_budget_ms * 2 && raycaster->current_quality_level < 3) {
            raycaster->current_quality_level++;
        } else if(frame_time < raycaster->frame_time_budget_ms / 2 && raycaster->current_quality_level > 0) {
            raycaster->current_quality_level--;
        }
        
        raycaster->last_performance_check = current_time;
    }
}

// Raycaster lifecycle
Raycaster* raycaster_alloc(void) {
    Raycaster* raycaster = malloc(sizeof(Raycaster));
    if(!raycaster) return NULL;
    
    // Initialize angle cache
    raycaster_init_angle_cache(raycaster);
    
    // Initialize sonar patterns
    raycaster_init_sonar_patterns(raycaster);
    
    // Initialize performance settings
    raycaster->current_quality_level = 1; // Start at medium quality
    raycaster->frame_time_budget_ms = 3;  // 3ms budget
    raycaster->last_performance_check = 0;
    
    // Reset frame stats
    raycaster_reset_frame_stats(raycaster);
    
    return raycaster;
}

void raycaster_free(Raycaster* raycaster) {
    if(raycaster) {
        free(raycaster);
    }
}

// Performance monitoring
void raycaster_reset_frame_stats(Raycaster* raycaster) {
    raycaster->rays_cast_this_frame = 0;
    raycaster->cache_hits_this_frame = 0;
    raycaster->early_exits_this_frame = 0;
}

void raycaster_log_performance(Raycaster* raycaster) {
    // This would normally log to debug output
    raycaster_reset_frame_stats(raycaster);
}