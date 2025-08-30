#define TEST_BUILD
#include "mock_furi.h"
#include "chunk_manager.h"
#include "raycaster.h"
#include "sonar_chart.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Mock implementations
uint32_t furi_get_tick(void) {
    return 1000; // Fixed timestamp
}

void furi_log_print_format(int level, const char* tag, const char* format, ...) {
    // Silent logging for test
}

void canvas_draw_dot(Canvas* canvas, int x, int y) {}
void canvas_draw_circle(Canvas* canvas, int x, int y, int radius) {}
void canvas_draw_line(Canvas* canvas, float x1, float y1, float x2, float y2) {}
void canvas_draw_disc(Canvas* canvas, int x, int y, int radius) {}
void canvas_printf(Canvas* canvas, int x, int y, const char* format, ...) {}

// Test collision function that prints debug info
bool test_collision_callback(int16_t x, int16_t y, void* context) {
    ChunkManager* chunk_manager = (ChunkManager*)context;
    bool is_collision = chunk_manager_check_collision(chunk_manager, x, y);
    
    // Only print first few collisions to reduce noise
    static int collision_count = 0;
    if(is_collision && collision_count < 10) {
        printf("  COLLISION at (%d,%d)\n", x, y);
        collision_count++;
    }
    
    return is_collision;
}

int main(void) {
    printf("=== First Ping Simulation Test ===\n\n");
    
    // Submarine starting position
    float sub_world_x = 64.0f;
    float sub_world_y = 32.0f;
    int16_t ping_x = (int16_t)sub_world_x;
    int16_t ping_y = (int16_t)sub_world_y;
    
    printf("Submarine at: (%.1f, %.1f)\n", sub_world_x, sub_world_y);
    printf("Ping from: (%d, %d)\n\n", ping_x, ping_y);
    
    // Initialize chunk manager
    ChunkManager* chunk_manager = chunk_manager_alloc();
    if(!chunk_manager) {
        printf("ERROR: Failed to allocate chunk manager\n");
        return 1;
    }
    
    printf("Chunk manager allocated successfully\n");
    
    // Initialize raycaster
    Raycaster* raycaster = raycaster_alloc();
    if(!raycaster) {
        printf("ERROR: Failed to allocate raycaster\n");
        chunk_manager_free(chunk_manager);
        return 1;
    }
    
    printf("Raycaster allocated successfully\n");
    printf("Quality level: %d\n", raycaster->current_quality_level);
    
    // Initialize sonar chart
    SonarChart* sonar_chart = sonar_chart_alloc();
    if(!sonar_chart) {
        printf("ERROR: Failed to allocate sonar chart\n");
        raycaster_free(raycaster);
        chunk_manager_free(chunk_manager);
        return 1;
    }
    
    printf("Sonar chart allocated successfully\n\n");
    
    // Load initial chunks around submarine
    printf("=== Loading initial chunks ===\n");
    chunk_manager_update(chunk_manager, sub_world_x, sub_world_y);
    
    // Print chunk information
    for(int i = 0; i < chunk_manager->active_count; i++) {
        Chunk* chunk = &chunk_manager->active_chunks[i];
        printf("Chunk %d: (%d,%d) seed=%u\n", i, chunk->chunk_x, chunk->chunk_y, chunk->generation_seed);
        
        // Sample a few terrain points around the submarine's local position
        int local_x = (int)sub_world_x % CHUNK_SIZE;
        int local_y = (int)sub_world_y % CHUNK_SIZE;
        printf("  Local submarine position: (%d,%d)\n", local_x, local_y);
        
        // Check terrain around submarine position
        for(int dy = -2; dy <= 2; dy++) {
            printf("  Row %2d: ", local_y + dy);
            for(int dx = -2; dx <= 2; dx++) {
                int check_x = local_x + dx;
                int check_y = local_y + dy;
                bool collision = chunk_manager_check_collision(chunk_manager, 
                                                              ping_x + dx, ping_y + dy);
                printf("%c", collision ? '#' : '.');
            }
            printf("\n");
        }
        printf("\n");
    }
    
    // Get raycasting pattern
    printf("=== Setting up raycasting pattern ===\n");
    RayPattern* pattern = raycaster_get_adaptive_pattern(raycaster, false);
    printf("Pattern: %d rays, max radius: %d\n", pattern->direction_count, pattern->max_radius);
    
    // Print ray directions for first few rays
    printf("First 8 ray directions:\n");
    for(int i = 0; i < 8 && i < pattern->direction_count; i++) {
        RayDirection dir = pattern->directions[i];
        float angle = raycaster_direction_to_angle(dir);
        printf("  Ray %d: angle %.2f rad, dir (%d,%d), angle_id=%d\n", 
               i, angle, dir.dx, dir.dy, dir.angle_id);
    }
    printf("\n");
    
    // Cast the rays
    printf("=== Casting rays for first ping ===\n");
    RayResult results[RAY_CACHE_SIZE];
    memset(results, 0, sizeof(results));
    
    printf("Casting %d rays from (%d,%d)...\n", pattern->direction_count, ping_x, ping_y);
    
    uint16_t hits = raycaster_cast_pattern(
        raycaster,
        pattern,
        ping_x,
        ping_y,
        results,
        test_collision_callback,
        chunk_manager
    );
    
    printf("Raycasting complete: %d hits out of %d rays\n", hits, pattern->direction_count);
    printf("Rays cast this frame: %d\n", raycaster->rays_cast_this_frame);
    printf("Early exits this frame: %d\n", raycaster->early_exits_this_frame);
    printf("\n");
    
    // Analyze results
    printf("=== Ray Results Analysis ===\n");
    uint16_t terrain_hits = 0;
    uint16_t water_hits = 0;
    int16_t min_distance = INT16_MAX;
    int16_t max_distance = 0;
    
    for(uint16_t i = 0; i < pattern->direction_count; i++) {
        RayResult* result = &results[i];
        
        if(result->ray_complete) {
            if(result->hit_terrain) {
                terrain_hits++;
                printf("Ray %2d: HIT TERRAIN at (%d,%d) distance=%d\n", 
                       i, result->hit_x, result->hit_y, result->distance);
            } else {
                water_hits++;
                if(i < 8) { // Only show first few water hits to reduce noise
                    printf("Ray %2d: water at (%d,%d) distance=%d\n", 
                           i, result->hit_x, result->hit_y, result->distance);
                }
            }
            
            if(result->distance < min_distance) min_distance = result->distance;
            if(result->distance > max_distance) max_distance = result->distance;
        } else {
            printf("Ray %2d: INCOMPLETE\n", i);
        }
    }
    
    printf("\nSummary:\n");
    printf("  Terrain hits: %d\n", terrain_hits);
    printf("  Water hits: %d\n", water_hits);
    printf("  Distance range: %d to %d\n", min_distance, max_distance);
    printf("\n");
    
    // Add discoveries to sonar chart (simulate game logic)
    printf("=== Adding discoveries to sonar chart ===\n");
    uint16_t points_added = 0;
    
    for(uint16_t i = 0; i < pattern->direction_count; i++) {
        RayResult* result = &results[i];
        if(result->ray_complete && result->distance <= 64) { // Simulate ping radius
            // Add terrain or water point
            bool added = sonar_chart_add_point(sonar_chart, 
                                              result->hit_x, result->hit_y, result->hit_terrain);
            if(added) {
                points_added++;
                if(result->hit_terrain) {
                    printf("Added TERRAIN point at (%d,%d)\n", result->hit_x, result->hit_y);
                }
            }
            
            // Add intermediate water points for terrain hits (simulate game logic)
            if(result->hit_terrain && result->distance > 1) {
                int16_t dx = result->hit_x - ping_x;
                int16_t dy = result->hit_y - ping_y;
                
                for(uint16_t step = 0; step < result->distance; step += 3) {
                    int16_t water_x = ping_x + (dx * step) / result->distance;
                    int16_t water_y = ping_y + (dy * step) / result->distance;
                    bool added_water = sonar_chart_add_point(sonar_chart, water_x, water_y, false);
                    if(added_water) {
                        points_added++;
                    }
                }
            }
        }
    }
    
    printf("Added %d points to sonar chart\n", points_added);
    printf("Points added this frame: %d\n", sonar_chart->points_added_this_frame);
    printf("\n");
    
    // Query sonar chart (simulate rendering)
    printf("=== Querying sonar chart for rendering ===\n");
    int sample_radius = 80;
    SonarBounds query_bounds = sonar_bounds_create(
        ping_x - sample_radius,
        ping_y - sample_radius,
        ping_x + sample_radius,
        ping_y + sample_radius
    );
    
    SonarPoint* visible_points[512];
    uint16_t point_count = sonar_chart_query_area(sonar_chart, query_bounds, visible_points, 512);
    
    printf("Query returned %d visible points\n", point_count);
    
    uint16_t terrain_points = 0;
    uint16_t water_points = 0;
    
    for(uint16_t i = 0; i < point_count; i++) {
        SonarPoint* point = visible_points[i];
        if(point->is_terrain) {
            terrain_points++;
            printf("Terrain point at (%d,%d) fade_state=%d\n", 
                   point->world_x, point->world_y, point->fade_state);
        } else {
            water_points++;
        }
    }
    
    printf("Visible terrain points: %d\n", terrain_points);
    printf("Visible water points: %d\n", water_points);
    printf("\n");
    
    // Simulate screen rendering
    printf("=== Simulating screen rendering ===\n");
    printf("Screen coordinates (assuming submarine at center 64,32):\n");
    uint16_t rendered_dots = 0;
    
    for(uint16_t i = 0; i < point_count; i++) {
        SonarPoint* point = visible_points[i];
        
        if(point->is_terrain) {
            // Transform to screen coordinates (simple version)
            int16_t screen_x = 64 + (point->world_x - ping_x);
            int16_t screen_y = 32 + (point->world_y - ping_y);
            
            // Check if on screen
            if(screen_x >= 0 && screen_x < 128 && screen_y >= 0 && screen_y < 64) {
                uint8_t opacity = sonar_fade_state_opacity(point->fade_state);
                if(opacity > 128) {
                    rendered_dots++;
                    printf("Render DOT at screen (%d,%d) from world (%d,%d)\n",
                           screen_x, screen_y, point->world_x, point->world_y);
                }
            }
        }
    }
    
    printf("\nFinal result: %d dots would be rendered on screen\n", rendered_dots);
    
    if(rendered_dots <= 3) {
        printf("\n*** REPRODUCED THE BUG: Only %d dots! ***\n", rendered_dots);
        printf("Expected: Many dots around submarine position\n");
        printf("This matches what you're seeing on the Flipper Zero.\n");
    } else {
        printf("\n*** Issue not reproduced: %d dots found ***\n", rendered_dots);
        printf("This suggests the bug is elsewhere or the fix worked.\n");
    }
    
    // Cleanup
    sonar_chart_free(sonar_chart);
    raycaster_free(raycaster);
    chunk_manager_free(chunk_manager);
    
    printf("\nTest complete.\n");
    return 0;
}