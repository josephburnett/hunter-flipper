#define _GNU_SOURCE
#define TEST_BUILD
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Mock types and functions
typedef void Canvas;
uint32_t furi_get_tick(void) { 
    static uint32_t tick = 1000;
    return tick += 50; // Simulate 50ms increments
}
void furi_log_print_format(int level, const char* tag, const char* format, ...) {
    (void)level; (void)tag; (void)format;
}
void canvas_draw_dot(Canvas* canvas, int x, int y) { (void)canvas; (void)x; (void)y; }
void canvas_draw_circle(Canvas* canvas, int x, int y, int radius) { (void)canvas; (void)x; (void)y; (void)radius; }
void canvas_draw_line(Canvas* canvas, float x1, float y1, float x2, float y2) { (void)canvas; (void)x1; (void)y1; (void)x2; (void)y2; }
void canvas_draw_disc(Canvas* canvas, int x, int y, int radius) { (void)canvas; (void)x; (void)y; (void)radius; }
void canvas_printf(Canvas* canvas, int x, int y, const char* format, ...) { (void)canvas; (void)x; (void)y; (void)format; }

// Include the fixed implementations from our working test
#define TERRAIN_SIZE 33
#define CHUNK_SIZE 33
#define MAX_ACTIVE_CHUNKS 4
#define RAY_CACHE_SIZE 64

// Copy all the working implementations from test_standalone.c
typedef struct {
    uint8_t* height_map;
    bool* collision_map;
    uint16_t width, height;
    uint8_t elevation_threshold;
    uint32_t seed;
} TerrainManager;

typedef struct {
    int chunk_x, chunk_y;
} ChunkCoord;

typedef struct {
    ChunkCoord coord;
    TerrainManager* terrain;
    bool is_loaded;
    uint32_t generation_seed;
} TerrainChunk;

typedef struct {
    TerrainChunk* active_chunks[MAX_ACTIVE_CHUNKS];
    int active_count;
    float player_world_x, player_world_y;
} ChunkManager;

typedef struct {
    int16_t dx, dy;
    uint8_t angle_id;
} RayDirection;

typedef struct {
    RayDirection* directions;
    uint16_t direction_count;
    uint16_t max_radius;
} RayPattern;

typedef struct {
    bool ray_complete;
    bool hit_terrain;
    int16_t hit_x, hit_y;
    int16_t distance;
} RayResult;

typedef struct {
    uint16_t rays_cast_this_frame;
    uint8_t current_quality_level;
    RayPattern sonar_pattern_full;
    int16_t bres_x, bres_y, bres_dx, bres_dy, bres_sx, bres_sy, bres_err;
    bool bres_active;
} Raycaster;

typedef struct {
    int16_t world_x, world_y;
    bool is_terrain;
    uint8_t fade_state;
} SonarPoint;

typedef struct {
    uint16_t points_added_this_frame;
    uint32_t total_points;
} SonarChart;

// Terrain implementation
TerrainManager* terrain_manager_alloc(uint32_t seed, uint8_t elevation) {
    TerrainManager* terrain = malloc(sizeof(TerrainManager));
    if (!terrain) return NULL;
    
    terrain->width = TERRAIN_SIZE;
    terrain->height = TERRAIN_SIZE;
    terrain->seed = seed;
    terrain->elevation_threshold = elevation;
    
    size_t map_size = TERRAIN_SIZE * TERRAIN_SIZE;
    terrain->height_map = malloc(map_size);
    terrain->collision_map = malloc(map_size);
    
    if (!terrain->height_map || !terrain->collision_map) {
        free(terrain->height_map);
        free(terrain->collision_map);
        free(terrain);
        return NULL;
    }
    
    srand(seed);
    for (size_t i = 0; i < map_size; i++) {
        terrain->height_map[i] = 100 + (rand() % 100);
        terrain->collision_map[i] = terrain->height_map[i] > elevation;
    }
    
    return terrain;
}

void terrain_manager_free(TerrainManager* terrain) {
    if (terrain) {
        free(terrain->height_map);
        free(terrain->collision_map);
        free(terrain);
    }
}

bool terrain_check_collision(TerrainManager* terrain, int x, int y) {
    if (x < 0 || y < 0 || x >= terrain->width || y >= terrain->height) {
        return false;
    }
    return terrain->collision_map[y * terrain->width + x];
}

// Chunk manager - 2x2 grid loading
ChunkManager* chunk_manager_alloc(void) {
    return calloc(1, sizeof(ChunkManager));
}

void chunk_manager_free(ChunkManager* manager) {
    if (manager) {
        for (int i = 0; i < manager->active_count; i++) {
            if (manager->active_chunks[i]) {
                terrain_manager_free(manager->active_chunks[i]->terrain);
                free(manager->active_chunks[i]);
            }
        }
        free(manager);
    }
}

void chunk_manager_update(ChunkManager* manager, float player_x, float player_y) {
    manager->player_world_x = player_x;
    manager->player_world_y = player_y;
    
    if (manager->active_count == 0) {
        int center_chunk_x = (int)floorf(player_x / CHUNK_SIZE);
        int center_chunk_y = (int)floorf(player_y / CHUNK_SIZE);
        
        int chunk_offsets[4][2] = {{0,0}, {1,0}, {0,1}, {1,1}};
        
        for (int i = 0; i < 4; i++) {
            TerrainChunk* chunk = malloc(sizeof(TerrainChunk));
            chunk->coord.chunk_x = center_chunk_x + chunk_offsets[i][0];
            chunk->coord.chunk_y = center_chunk_y + chunk_offsets[i][1];
            
            uint32_t chunk_seed = 12345 + (chunk->coord.chunk_x * 1000) + chunk->coord.chunk_y;
            chunk->terrain = terrain_manager_alloc(chunk_seed, 90);
            chunk->is_loaded = true;
            chunk->generation_seed = chunk_seed;
            
            manager->active_chunks[i] = chunk;
        }
        manager->active_count = 4;
    }
}

bool chunk_manager_check_collision(ChunkManager* manager, int world_x, int world_y) {
    if (manager->active_count == 0) return false;
    
    int target_chunk_x = (int)floorf((float)world_x / CHUNK_SIZE);
    int target_chunk_y = (int)floorf((float)world_y / CHUNK_SIZE);
    
    for (int i = 0; i < manager->active_count; i++) {
        TerrainChunk* chunk = manager->active_chunks[i];
        if (chunk->coord.chunk_x == target_chunk_x && chunk->coord.chunk_y == target_chunk_y) {
            int chunk_base_x = chunk->coord.chunk_x * CHUNK_SIZE;
            int chunk_base_y = chunk->coord.chunk_y * CHUNK_SIZE;
            
            int local_x = world_x - chunk_base_x;
            int local_y = world_y - chunk_base_y;
            
            return terrain_check_collision(chunk->terrain, local_x, local_y);
        }
    }
    return false;
}

// Simple raycaster
static RayDirection full_directions[32];
static bool patterns_initialized = false;

void init_ray_patterns() {
    if (patterns_initialized) return;
    
    for (int i = 0; i < 32; i++) {
        float angle = (float)i * 2.0f * M_PI / 32.0f;
        full_directions[i].dx = (int16_t)(cosf(angle) * 1000);
        full_directions[i].dy = (int16_t)(sinf(angle) * 1000);
        full_directions[i].angle_id = i;
    }
    patterns_initialized = true;
}

Raycaster* raycaster_alloc(void) {
    Raycaster* raycaster = calloc(1, sizeof(Raycaster));
    if (!raycaster) return NULL;
    
    init_ray_patterns();
    raycaster->sonar_pattern_full.directions = full_directions;
    raycaster->sonar_pattern_full.direction_count = 32;
    raycaster->sonar_pattern_full.max_radius = 64;
    
    return raycaster;
}

void raycaster_free(Raycaster* raycaster) {
    free(raycaster);
}

RayPattern* raycaster_get_adaptive_pattern(Raycaster* raycaster, bool forward_only) {
    (void)forward_only;
    return &raycaster->sonar_pattern_full;
}

void raycaster_bresham_init(Raycaster* raycaster, int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
    raycaster->bres_x = x0;
    raycaster->bres_y = y0;
    raycaster->bres_dx = abs(x1 - x0);
    raycaster->bres_dy = -abs(y1 - y0);
    raycaster->bres_sx = (x0 < x1) ? 1 : -1;
    raycaster->bres_sy = (y0 < y1) ? 1 : -1;
    raycaster->bres_err = raycaster->bres_dx + raycaster->bres_dy;
    raycaster->bres_active = true;
}

bool raycaster_bresham_step(Raycaster* raycaster, int16_t* x, int16_t* y) {
    if (!raycaster->bres_active) return false;
    
    *x = raycaster->bres_x;
    *y = raycaster->bres_y;
    
    {
        int16_t e2 = 2 * raycaster->bres_err;
        if (e2 >= raycaster->bres_dy) {
            raycaster->bres_err += raycaster->bres_dy;
            raycaster->bres_x += raycaster->bres_sx;
        }
        if (e2 <= raycaster->bres_dx) {
            raycaster->bres_err += raycaster->bres_dx;
            raycaster->bres_y += raycaster->bres_sy;
        }
    }
    
    int16_t dx = abs(raycaster->bres_x);
    int16_t dy = abs(raycaster->bres_y);
    if (dx * dx + dy * dy > 64 * 64) {
        raycaster->bres_active = false;
        return false;
    }
    
    return true;
}

typedef bool (*collision_callback_t)(int16_t x, int16_t y, void* context);

uint16_t raycaster_cast_pattern(Raycaster* raycaster, RayPattern* pattern, 
                               int16_t start_x, int16_t start_y, RayResult* results,
                               collision_callback_t collision_check, void* collision_context) {
    uint16_t hits = 0;
    raycaster->rays_cast_this_frame = pattern->direction_count;
    
    for (uint16_t i = 0; i < pattern->direction_count; i++) {
        RayDirection* dir = &pattern->directions[i];
        RayResult* result = &results[i];
        
        int16_t end_x = start_x + (dir->dx * 64 / 1000);
        int16_t end_y = start_y + (dir->dy * 64 / 1000);
        
        raycaster_bresham_init(raycaster, start_x, start_y, end_x, end_y);
        
        int16_t x, y;
        int16_t steps = 0;
        bool found_collision = false;
        
        while (raycaster_bresham_step(raycaster, &x, &y) && steps < 64) {
            if (x != start_x || y != start_y) {
                if (collision_check(x, y, collision_context)) {
                    result->hit_terrain = true;
                    result->hit_x = x;
                    result->hit_y = y;
                    result->distance = steps;
                    result->ray_complete = true;
                    found_collision = true;
                    hits++;
                    break;
                }
            }
            steps++;
        }
        
        if (!found_collision) {
            result->hit_terrain = false;
            result->hit_x = end_x;
            result->hit_y = end_y;
            result->distance = 64;
            result->ray_complete = true;
        }
    }
    
    return hits;
}

// Simple sonar chart
SonarChart* sonar_chart_alloc(void) {
    return calloc(1, sizeof(SonarChart));
}

void sonar_chart_free(SonarChart* chart) {
    free(chart);
}

bool sonar_chart_add_point(SonarChart* chart, int16_t x, int16_t y, bool is_terrain) {
    (void)x; (void)y;
    if (is_terrain) {
        chart->points_added_this_frame++;
        chart->total_points++;
    }
    return true;
}

bool test_collision_callback(int16_t x, int16_t y, void* context) {
    ChunkManager* chunk_manager = (ChunkManager*)context;
    return chunk_manager_check_collision(chunk_manager, x, y);
}

int main(void) {
    printf("=== Progressive Ping Test (As per Test Plan) ===\n\n");
    printf("This test simulates the exact first ping behavior from game.c\n");
    printf("Starting at radius 0 and growing by 2 every 50ms to catch the '3 dots' bug.\n\n");
    
    // Initialize systems
    ChunkManager* chunk_manager = chunk_manager_alloc();
    Raycaster* raycaster = raycaster_alloc();
    SonarChart* sonar_chart = sonar_chart_alloc();
    
    if (!chunk_manager || !raycaster || !sonar_chart) {
        printf("ERROR: Failed to allocate components\n");
        return 1;
    }
    
    // Submarine position (same as test plan)
    float world_x = 64.0f;
    float world_y = 32.0f;
    printf("Submarine at world position: (%.1f, %.1f)\n", world_x, world_y);
    
    // Load chunks
    chunk_manager_update(chunk_manager, world_x, world_y);
    printf("Loaded %d chunks for 2x2 grid coverage\n", chunk_manager->active_count);
    
    // Quick terrain check around submarine
    int terrain_count = 0;
    printf("Terrain around submarine:\n");
    for (int dy = -2; dy <= 2; dy++) {
        printf("Row %2d: ", dy);
        for (int dx = -2; dx <= 2; dx++) {
            bool collision = chunk_manager_check_collision(chunk_manager, 
                                                          (int)world_x + dx, (int)world_y + dy);
            printf("%c", collision ? '#' : '.');
            if (collision) terrain_count++;
        }
        printf("\n");
    }
    printf("Terrain pixels in 5x5 area: %d\n\n", terrain_count);
    
    // Progressive ping simulation - EXACT implementation from test plan
    printf("=== Progressive Ping Simulation ===\n");
    bool ping_active = true;
    float ping_x = world_x;
    float ping_y = world_y;
    int ping_radius = 0;  // CRITICAL: Start at 0!
    uint32_t ping_timer = furi_get_tick();
    
    int frame = 0;
    int discoveries[35] = {0}; // Track discoveries per frame
    
    printf("Frame | Radius | New Terrain Points | Total Points | Status\n");
    printf("------|--------|-------------------|--------------|--------\n");
    
    while (ping_active && frame < 35) {
        uint32_t current_time = furi_get_tick();
        
        // Update every 50ms (same as game)
        if (current_time - ping_timer >= 50) {
            ping_radius += 2;
            ping_timer = current_time;
            
            // Get ray pattern and cast rays
            RayPattern* pattern = raycaster_get_adaptive_pattern(raycaster, false);
            RayResult results[RAY_CACHE_SIZE];
            memset(results, 0, sizeof(results));
            
            uint16_t total_hits = raycaster_cast_pattern(
                raycaster, pattern,
                (int16_t)ping_x, (int16_t)ping_y,
                results, test_collision_callback, chunk_manager
            );
            
            // Process results - only add points within current ping radius
            int new_terrain_points = 0;
            int total_terrain_hits = 0;
            int hits_within_radius = 0;
            
            for (uint16_t i = 0; i < pattern->direction_count; i++) {
                RayResult* result = &results[i];
                
                if (result->ray_complete && result->hit_terrain) {
                    total_terrain_hits++;
                    if (result->distance <= ping_radius) {
                        sonar_chart_add_point(sonar_chart, result->hit_x, result->hit_y, true);
                        new_terrain_points++;
                        hits_within_radius++;
                        
                        // Debug first few hits at early radius
                        if (ping_radius <= 6 && hits_within_radius <= 5) {
                            printf("        Ray %d: hit terrain at (%d,%d) distance=%d (radius=%d)\n",
                                   i, result->hit_x, result->hit_y, result->distance, ping_radius);
                        }
                    }
                }
            }
            
            // Add debug info for first few frames
            if (frame < 5) {
                printf("        Debug: Found %d total terrain hits, %d within radius %d\n",
                       total_terrain_hits, hits_within_radius, ping_radius);
            }
            
            discoveries[frame] = new_terrain_points;
            
            // Check for the critical bug condition
            const char* status = "";
            if (ping_radius <= 4 && sonar_chart->total_points <= 3) {
                status = "*** BUG DETECTED ***";
            } else if (ping_radius <= 6 && sonar_chart->total_points >= 10) {
                status = "Good coverage";
            }
            
            printf("%5d | %6d | %17d | %12d | %s\n", 
                   frame, ping_radius, new_terrain_points, (int)sonar_chart->total_points, status);
            
            frame++;
            
            // Stop at max radius
            if (ping_radius > 64) {
                ping_active = false;
            }
        }
    }
    
    printf("\n=== Final Analysis ===\n");
    printf("Total frames: %d\n", frame);
    printf("Final terrain points: %d\n", (int)sonar_chart->total_points);
    
    // Validate discovery pattern (as per test plan)
    printf("\nEarly radius validation (critical for bug detection):\n");
    int early_total = 0;
    for (int i = 0; i < 3 && i < frame; i++) {
        early_total += discoveries[i];
        printf("  Radius %d: %d new points (running total: %d)\n", 
               i*2+2, discoveries[i], early_total);
    }
    
    // Test result
    if (early_total <= 3) {
        printf("\n*** FAILED: Only %d terrain points in first 3 frames! ***\n", early_total);
        printf("This reproduces the '3 dots only' bug described in the test plan.\n");
        printf("Issue: Insufficient chunk loading or raycasting problems.\n");
        return 1;
    } else if (sonar_chart->total_points < 50) {
        printf("\n*** PARTIAL FAILURE: Only %d total terrain points ***\n", (int)sonar_chart->total_points);
        printf("Raycasting works but terrain coverage is low.\n");
        return 1;
    } else {
        printf("\n*** SUCCESS: %d terrain points discovered! ***\n", (int)sonar_chart->total_points);
        printf("Progressive ping test passes - no '3 dots' bug detected.\n");
        printf("Early frames found %d points, showing proper raycasting.\n", early_total);
    }
    
    // Cleanup
    sonar_chart_free(sonar_chart);
    raycaster_free(raycaster);
    chunk_manager_free(chunk_manager);
    
    return 0;
}