#define TEST_BUILD
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

// Mock FURI_LOG_D
#define FURI_LOG_D(tag, format, ...) printf("[DEBUG] %s: " format "\n", tag, ##__VA_ARGS__)

// Mock furi_get_tick
uint32_t furi_get_tick(void) {
    static uint32_t tick = 1000;
    return tick++;
}

// Include all the actual game structures and functions
#include "terrain.c"       // Full terrain implementation
#include "chunk_manager.c" // Full chunk manager
#include "raycaster.c"     // Full raycaster
#include "sonar_chart.c"   // Full sonar chart

// Collision callback (same as game.c)
static bool collision_check_callback(int16_t x, int16_t y, void* context) {
    ChunkManager* chunk_manager = (ChunkManager*)context;
    return chunk_manager_check_collision(chunk_manager, x, y);
}

// Coordinate transformation (same as game.c)
typedef struct {
    int screen_x;
    int screen_y;
} ScreenPoint;

static ScreenPoint world_to_screen(float world_x, float world_y, float sub_x, float sub_y) {
    ScreenPoint point;
    point.screen_x = 64 + (int)(world_x - sub_x);
    point.screen_y = 32 + (int)(world_y - sub_y);
    return point;
}

void print_discovered_map(SonarChart* sonar_chart, float sub_x, float sub_y, int radius) {
    printf("\n=== What Would Be Rendered (radius %d) ===\n", radius);
    
    // Create ASCII map
    char map[65][129]; // Screen is 128x64
    memset(map, ' ', sizeof(map));
    
    // Mark submarine position
    map[32][64] = 'S';
    
    // Query sonar chart for visible area (same as game.c lines 266-271)
    int sample_radius = 80;
    SonarBounds query_bounds = sonar_bounds_create(
        (int16_t)sub_x - sample_radius,
        (int16_t)sub_y - sample_radius,
        (int16_t)sub_x + sample_radius,
        (int16_t)sub_y + sample_radius
    );
    
    SonarPoint* visible_points[512];
    uint16_t point_count = sonar_chart_query_area(sonar_chart, query_bounds, visible_points, 512);
    
    printf("Sonar chart query returned %d points\n", point_count);
    
    int terrain_count = 0;
    int water_count = 0;
    int on_screen = 0;
    
    // Process each discovered point (same as game.c lines 278-307)
    for(uint16_t i = 0; i < point_count; i++) {
        SonarPoint* point = visible_points[i];
        
        // Transform to screen coordinates
        ScreenPoint screen = world_to_screen(point->world_x, point->world_y, sub_x, sub_y);
        
        // Count point types
        if(point->is_terrain) {
            terrain_count++;
        } else {
            water_count++;
        }
        
        // Mark on ASCII map if on screen
        if(screen.screen_x >= 0 && screen.screen_x < 128 &&
           screen.screen_y >= 0 && screen.screen_y < 64) {
            on_screen++;
            if(point->is_terrain) {
                map[screen.screen_y][screen.screen_x] = '#';
            } else if(map[screen.screen_y][screen.screen_x] == ' ') {
                map[screen.screen_y][screen.screen_x] = '.';
            }
        }
    }
    
    printf("  Terrain points: %d\n", terrain_count);
    printf("  Water points: %d\n", water_count);
    printf("  On screen: %d\n", on_screen);
    
    // Print center portion of map
    printf("\nScreen view (center 40x20):\n");
    for(int y = 22; y < 42; y++) {
        printf("    ");
        for(int x = 44; x < 84; x++) {
            printf("%c", map[y][x]);
        }
        printf("\n");
    }
}

int main(void) {
    printf("=== Full Pipeline Test: First Ping Simulation ===\n\n");
    
    // 1. Initialize exactly like game.c
    printf("Step 1: Initialize game systems\n");
    
    ChunkManager* chunk_manager = chunk_manager_alloc();
    if(!chunk_manager) {
        printf("FAIL: Could not allocate chunk manager\n");
        return 1;
    }
    
    Raycaster* raycaster = raycaster_alloc();
    if(!raycaster) {
        printf("FAIL: Could not allocate raycaster\n");
        return 1;
    }
    
    SonarChart* sonar_chart = sonar_chart_alloc();
    if(!sonar_chart) {
        printf("FAIL: Could not allocate sonar chart\n");
        return 1;
    }
    
    // 2. Set submarine position (same as game.c)
    float world_x = 64.0f;
    float world_y = 32.0f;
    printf("Submarine at world position: (%.1f, %.1f)\n", world_x, world_y);
    
    // 3. Update chunk manager to load terrain
    printf("\nStep 2: Load terrain chunks\n");
    chunk_manager_update(chunk_manager, world_x, world_y);
    
    // Check what chunks loaded
    int chunks_loaded = 0;
    for(int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
        if(chunk_manager->active_chunks[i] && chunk_manager->active_chunks[i]->is_loaded) {
            TerrainChunk* chunk = chunk_manager->active_chunks[i];
            printf("  Chunk %d: coord(%d,%d) loaded\n", 
                   i, chunk->coord.chunk_x, chunk->coord.chunk_y);
            chunks_loaded++;
        }
    }
    printf("Total chunks loaded: %d\n", chunks_loaded);
    
    // 4. Check terrain around submarine
    printf("\nStep 3: Check terrain around submarine\n");
    int check_radius = 20;
    int terrain_exists = 0;
    
    for(int dy = -check_radius; dy <= check_radius; dy++) {
        for(int dx = -check_radius; dx <= check_radius; dx++) {
            int x = (int)world_x + dx;
            int y = (int)world_y + dy;
            if(chunk_manager_check_collision(chunk_manager, x, y)) {
                terrain_exists++;
            }
        }
    }
    
    printf("Terrain pixels in %dx%d area: %d\n", 
           check_radius*2+1, check_radius*2+1, terrain_exists);
    
    if(terrain_exists == 0) {
        printf("WARNING: No terrain exists around submarine!\n");
    }
    
    // 5. SIMULATE FIRST PING (exactly like game.c lines 169-219)
    printf("\nStep 4: Simulate first sonar ping\n");
    
    bool ping_active = true;
    float ping_x = world_x;
    float ping_y = world_y;
    int ping_radius = 0;
    uint32_t ping_timer = furi_get_tick();
    
    int total_rays_cast = 0;
    int total_hits = 0;
    int points_added = 0;
    
    // Simulate ping expansion
    while(ping_active) {
        uint32_t current_time = furi_get_tick();
        
        // Update every 50ms (same as game)
        if(current_time - ping_timer > 50) {
            ping_radius += 2;
            ping_timer = current_time;
            
            printf("  Ping radius: %d\n", ping_radius);
            
            // Get adaptive pattern (same as game)
            RayPattern* pattern = raycaster_get_adaptive_pattern(raycaster, false);
            RayResult results[RAY_CACHE_SIZE];
            
            printf("    Using pattern with %d rays, max distance %d\n", 
                   pattern->direction_count, pattern->max_radius);
            
            // Cast rays (same as game lines 181-189)
            raycaster_cast_pattern(
                raycaster,
                pattern,
                (int16_t)ping_x,
                (int16_t)ping_y,
                results,
                collision_check_callback,
                chunk_manager
            );
            
            total_rays_cast += pattern->direction_count;
            
            // Process results (same as game lines 191-214)
            int hits_this_round = 0;
            for(uint16_t i = 0; i < pattern->direction_count; i++) {
                RayResult* result = &results[i];
                
                if(result->ray_complete && result->distance <= ping_radius) {
                    // Add to sonar chart
                    sonar_chart_add_point(sonar_chart, 
                                         result->hit_x, result->hit_y, result->hit_terrain);
                    points_added++;
                    
                    if(result->hit_terrain) {
                        hits_this_round++;
                        total_hits++;
                        
                        // Add intermediate water points (same as game)
                        if(result->distance > 1) {
                            int16_t start_x = (int16_t)ping_x;
                            int16_t start_y = (int16_t)ping_y;
                            int16_t dx = result->hit_x - start_x;
                            int16_t dy = result->hit_y - start_y;
                            
                            for(uint16_t step = 0; step < result->distance; step += 3) {
                                int16_t water_x = start_x + (dx * step) / result->distance;
                                int16_t water_y = start_y + (dy * step) / result->distance;
                                sonar_chart_add_point(sonar_chart, water_x, water_y, false);
                                points_added++;
                            }
                        }
                    }
                }
            }
            
            printf("    Hits this round: %d\n", hits_this_round);
            
            // Stop at max radius (same as game)
            if(ping_radius > 64) {
                ping_active = false;
            }
        }
    }
    
    printf("\nPing complete:\n");
    printf("  Total rays cast: %d\n", total_rays_cast);
    printf("  Total terrain hits: %d\n", total_hits);
    printf("  Total points added to sonar: %d\n", points_added);
    
    // 6. Query and visualize what would be rendered
    print_discovered_map(sonar_chart, world_x, world_y, 64);
    
    // 7. Final diagnosis
    printf("\n=== DIAGNOSIS ===\n");
    if(total_hits <= 3) {
        printf("PROBLEM REPRODUCED: Only %d terrain hits during ping!\n", total_hits);
        
        if(terrain_exists > 0) {
            printf("Terrain exists but rays aren't hitting it.\n");
            printf("Possible issues:\n");
            printf("  - Raycasting math error\n");
            printf("  - Coordinate conversion error\n");
            printf("  - Terrain at wrong location\n");
        } else {
            printf("No terrain exists at submarine location.\n");
            printf("Terrain generation or chunk loading is broken.\n");
        }
    } else {
        printf("Ping found %d terrain points.\n", total_hits);
        
        // Check if it's a rendering issue
        SonarBounds test_bounds = sonar_bounds_create(
            (int16_t)world_x - 10,
            (int16_t)world_y - 10,
            (int16_t)world_x + 10,
            (int16_t)world_y + 10
        );
        SonarPoint* test_points[100];
        uint16_t test_count = sonar_chart_query_area(sonar_chart, test_bounds, test_points, 100);
        
        if(test_count <= 3) {
            printf("But sonar chart query only returns %d points!\n", test_count);
            printf("Problem is in sonar chart storage or retrieval.\n");
        } else {
            printf("Sonar chart has data. Issue may be in rendering transform.\n");
        }
    }
    
    // Cleanup
    sonar_chart_free(sonar_chart);
    raycaster_free(raycaster);
    chunk_manager_free(chunk_manager);
    
    return 0;
}