#define TEST_BUILD
#include "mock_furi.h"
#include "terrain.h"
#include "chunk_manager.h"
#include "raycaster.h" 
#include "sonar_chart.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// Collision callback for raycasting (same as game.c)
static bool collision_check_callback(int16_t x, int16_t y, void* context) {
    ChunkManager* chunk_manager = (ChunkManager*)context;
    return chunk_manager_check_collision(chunk_manager, x, y);
}

void print_terrain_around_position(ChunkManager* chunk_manager, int center_x, int center_y, int radius) {
    printf("\n=== Terrain around (%d,%d) radius %d ===\n", center_x, center_y, radius);
    
    for(int y = center_y - radius; y <= center_y + radius; y++) {
        printf("    ");
        for(int x = center_x - radius; x <= center_x + radius; x++) {
            if(x == center_x && y == center_y) {
                printf("S"); // Submarine position
            } else {
                bool is_land = chunk_manager_check_collision(chunk_manager, x, y);
                printf("%c", is_land ? '#' : '.');
            }
        }
        printf("\n");
    }
    printf("\n");
}

void print_discovered_terrain(SonarChart* sonar_chart, int center_x, int center_y, int radius) {
    printf("\n=== Discovered terrain around (%d,%d) radius %d ===\n", center_x, center_y, radius);
    
    for(int y = center_y - radius; y <= center_y + radius; y++) {
        printf("    ");
        for(int x = center_x - radius; x <= center_x + radius; x++) {
            if(x == center_x && y == center_y) {
                printf("S"); // Submarine position
            } else {
                // Query sonar chart for this point
                SonarBounds bounds = sonar_bounds_create(x, y, x, y);
                SonarPoint* points[1];
                uint16_t count = sonar_chart_query_area(sonar_chart, bounds, points, 1);
                
                if(count > 0 && points[0]->is_terrain) {
                    printf("#");
                } else if(count > 0 && !points[0]->is_terrain) {
                    printf("~");
                } else {
                    printf(" ");
                }
            }
        }
        printf("\n");
    }
    printf("\n");
}

int main(void) {
    printf("=== First Ping Simulation Test ===\n");
    
    // Initialize game systems exactly like the game does
    ChunkManager* chunk_manager = chunk_manager_alloc();
    if(!chunk_manager) {
        printf("FAIL: Could not allocate chunk manager\n");
        return 1;
    }
    
    Raycaster* raycaster = raycaster_alloc();
    if(!raycaster) {
        printf("FAIL: Could not allocate raycaster\n");
        chunk_manager_free(chunk_manager);
        return 1;
    }
    
    SonarChart* sonar_chart = sonar_chart_alloc();
    if(!sonar_chart) {
        printf("FAIL: Could not allocate sonar chart\n");
        raycaster_free(raycaster);
        chunk_manager_free(chunk_manager);
        return 1;
    }
    
    // Set submarine starting position (same as game.c)
    float world_x = 64.0f;
    float world_y = 32.0f;
    
    printf("Starting position: (%.1f, %.1f)\n", world_x, world_y);
    
    // Update chunk manager to load terrain around starting position
    chunk_manager_update(chunk_manager, world_x, world_y);
    
    // Check what chunks are loaded
    printf("\nChunk loading status:\n");
    for(int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
        if(chunk_manager->active_chunks[i]) {
            TerrainChunk* chunk = chunk_manager->active_chunks[i];
            printf("  Chunk %d: (%d,%d) seed=0x%08lX loaded=%s\n", 
                   i, chunk->coord.chunk_x, chunk->coord.chunk_y, 
                   chunk->generation_seed, chunk->is_loaded ? "YES" : "NO");
        } else {
            printf("  Chunk %d: NULL\n", i);
        }
    }
    
    // Print terrain that exists around starting position
    print_terrain_around_position(chunk_manager, (int)world_x, (int)world_y, 15);
    
    // Count terrain in area around submarine
    int terrain_count = 0;
    int total_count = 0;
    for(int y = (int)world_y - 20; y <= (int)world_y + 20; y++) {
        for(int x = (int)world_x - 20; x <= (int)world_x + 20; x++) {
            total_count++;
            if(chunk_manager_check_collision(chunk_manager, x, y)) {
                terrain_count++;
            }
        }
    }
    
    printf("Terrain in 40x40 area around submarine: %d/%d (%.1f%%)\n", 
           terrain_count, total_count, (float)terrain_count * 100.0f / total_count);
    
    if(terrain_count == 0) {
        printf("WARNING: No terrain found around submarine! This explains the empty sonar.\n");
        
        // Check if terrain exists anywhere in loaded chunks
        printf("\nChecking entire loaded chunks for terrain...\n");
        int chunk_terrain_count = 0;
        int chunk_total_count = 0;
        
        for(int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
            if(chunk_manager->active_chunks[i] && chunk_manager->active_chunks[i]->terrain) {
                TerrainChunk* chunk = chunk_manager->active_chunks[i];
                TerrainManager* terrain = chunk->terrain;
                
                for(int y = 0; y < terrain->height; y++) {
                    for(int x = 0; x < terrain->width; x++) {
                        chunk_total_count++;
                        if(terrain_check_collision(terrain, x, y)) {
                            chunk_terrain_count++;
                        }
                    }
                }
                
                printf("  Chunk %d terrain: %d land pixels out of %d total\n", 
                       i, chunk_terrain_count - (chunk_total_count - (terrain->width * terrain->height)), 
                       terrain->width * terrain->height);
            }
        }
        
        printf("Total terrain in all loaded chunks: %d/%d (%.1f%%)\n",
               chunk_terrain_count, chunk_total_count, 
               chunk_total_count > 0 ? (float)chunk_terrain_count * 100.0f / chunk_total_count : 0.0f);
    }
    
    // Now simulate a sonar ping exactly like the game does
    printf("\n=== Simulating Sonar Ping ===\n");
    
    // Get adaptive pattern (same as game)
    RayPattern* pattern = raycaster_get_adaptive_pattern(raycaster, false);
    printf("Using ray pattern with %d directions, max radius %d\n", 
           pattern->direction_count, pattern->max_radius);
    
    // Cast rays from ping position
    RayResult results[RAY_CACHE_SIZE];
    uint16_t hits = raycaster_cast_pattern(
        raycaster,
        pattern,
        (int16_t)world_x,
        (int16_t)world_y,
        results,
        collision_check_callback,
        chunk_manager
    );
    
    printf("Raycasting found %d terrain hits out of %d rays\n", hits, pattern->direction_count);
    
    // Add discoveries to sonar chart (simulate ping expansion)
    int discoveries_added = 0;
    for(int ping_radius = 1; ping_radius <= 64; ping_radius += 2) {
        for(uint16_t i = 0; i < pattern->direction_count; i++) {
            RayResult* result = &results[i];
            if(result->ray_complete && result->distance <= ping_radius) {
                // Add terrain or water point to sonar chart
                sonar_chart_add_point(sonar_chart, result->hit_x, result->hit_y, result->hit_terrain);
                discoveries_added++;
                
                // Add intermediate water points like the game does
                if(result->hit_terrain && result->distance > 1) {
                    int16_t start_x = (int16_t)world_x;
                    int16_t start_y = (int16_t)world_y;
                    int16_t dx = result->hit_x - start_x;
                    int16_t dy = result->hit_y - start_y;
                    
                    for(uint16_t step = 0; step < result->distance; step += 3) {
                        int16_t water_x = start_x + (dx * step) / result->distance;
                        int16_t water_y = start_y + (dy * step) / result->distance;
                        sonar_chart_add_point(sonar_chart, water_x, water_y, false);
                        discoveries_added++;
                    }
                }
            }
        }
    }
    
    printf("Added %d discoveries to sonar chart\n", discoveries_added);
    
    // Check what's in the sonar chart after ping
    SonarBounds query_bounds = sonar_bounds_create(
        (int16_t)world_x - 40,
        (int16_t)world_y - 40, 
        (int16_t)world_x + 40,
        (int16_t)world_y + 40
    );
    
    SonarPoint* visible_points[512];
    uint16_t point_count = sonar_chart_query_area(sonar_chart, query_bounds, visible_points, 512);
    
    printf("Sonar chart contains %d points in query area\n", point_count);
    
    int terrain_points = 0;
    int water_points = 0;
    for(uint16_t i = 0; i < point_count; i++) {
        if(visible_points[i]->is_terrain) {
            terrain_points++;
        } else {
            water_points++;
        }
    }
    
    printf("  Terrain points: %d\n", terrain_points);
    printf("  Water points: %d\n", water_points);
    
    // Print what would be rendered on screen
    print_discovered_terrain(sonar_chart, (int)world_x, (int)world_y, 15);
    
    // Final assessment
    printf("\n=== Assessment ===\n");
    if(terrain_points == 0) {
        printf("PROBLEM: No terrain points discovered by sonar ping!\n");
        if(terrain_count == 0) {
            printf("ROOT CAUSE: No terrain exists around submarine starting position\n");
            printf("SOLUTION: Either fix terrain generation or change starting position\n");
        } else {
            printf("ROOT CAUSE: Raycasting or sonar chart storage is broken\n");
        }
    } else {
        printf("SUCCESS: %d terrain points should be visible after ping\n", terrain_points);
    }
    
    // Cleanup
    sonar_chart_free(sonar_chart);
    raycaster_free(raycaster);
    chunk_manager_free(chunk_manager);
    
    return 0;
}