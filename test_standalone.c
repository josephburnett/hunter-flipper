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
    return tick++; 
}

void furi_log_print_format(int level, const char* tag, const char* format, ...) {
    (void)level; (void)tag; (void)format;
}

void canvas_draw_dot(Canvas* canvas, int x, int y) { (void)canvas; (void)x; (void)y; }
void canvas_draw_circle(Canvas* canvas, int x, int y, int radius) { (void)canvas; (void)x; (void)y; (void)radius; }
void canvas_draw_line(Canvas* canvas, float x1, float y1, float x2, float y2) { (void)canvas; (void)x1; (void)y1; (void)x2; (void)y2; }
void canvas_draw_disc(Canvas* canvas, int x, int y, int radius) { (void)canvas; (void)x; (void)y; (void)radius; }
void canvas_printf(Canvas* canvas, int x, int y, const char* format, ...) { (void)canvas; (void)x; (void)y; (void)format; }

// Minimal terrain system (standalone version)
#define TERRAIN_SIZE 33
#define CHUNK_SIZE 33
#define MAX_ACTIVE_CHUNKS 4
#define RAY_CACHE_SIZE 64

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

// Raycaster types
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
    uint16_t early_exits_this_frame;
    uint8_t current_quality_level;
    
    // Ray patterns
    RayPattern sonar_pattern_full;
    RayPattern sonar_pattern_forward;
    RayPattern sonar_pattern_sparse;
    
    // Bresenham state
    int16_t bres_x, bres_y;
    int16_t bres_dx, bres_dy;
    int16_t bres_sx, bres_sy;
    int16_t bres_err;
    bool bres_active;
} Raycaster;

// Sonar Chart types
typedef struct {
    int16_t world_x, world_y;
    bool is_terrain;
    uint8_t fade_state;
} SonarPoint;

typedef struct {
    int16_t min_x, min_y, max_x, max_y;
} SonarBounds;

typedef struct {
    uint16_t points_added_this_frame;
    uint32_t dummy;
} SonarChart;

// Simple terrain implementation
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
    
    // Generate simple terrain - mostly land
    srand(seed);
    for (size_t i = 0; i < map_size; i++) {
        terrain->height_map[i] = 100 + (rand() % 100); // 100-199
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

// Simple chunk manager
ChunkManager* chunk_manager_alloc(void) {
    ChunkManager* manager = calloc(1, sizeof(ChunkManager));
    return manager;
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
    
    // Load 2x2 grid of chunks around player
    if (manager->active_count == 0) {
        int center_chunk_x = (int)floorf(player_x / CHUNK_SIZE);
        int center_chunk_y = (int)floorf(player_y / CHUNK_SIZE);
        
        printf("Player at (%.1f,%.1f) -> center chunk (%d,%d)\n",
               player_x, player_y, center_chunk_x, center_chunk_y);
        printf("Loading 2x2 chunk grid:\n");
        
        // Load 2x2 grid: center and 3 neighbors
        int chunk_offsets[4][2] = {{0,0}, {1,0}, {0,1}, {1,1}};
        
        for (int i = 0; i < 4; i++) {
            TerrainChunk* chunk = malloc(sizeof(TerrainChunk));
            chunk->coord.chunk_x = center_chunk_x + chunk_offsets[i][0];
            chunk->coord.chunk_y = center_chunk_y + chunk_offsets[i][1];
            
            // Use different seed for each chunk to get different terrain
            uint32_t chunk_seed = 12345 + (chunk->coord.chunk_x * 1000) + chunk->coord.chunk_y;
            chunk->terrain = terrain_manager_alloc(chunk_seed, 90);
            chunk->is_loaded = true;
            chunk->generation_seed = chunk_seed;
            
            printf("  Chunk %d: (%d,%d) seed=%u\n", i, 
                   chunk->coord.chunk_x, chunk->coord.chunk_y, chunk_seed);
            
            manager->active_chunks[i] = chunk;
        }
        manager->active_count = 4;
    }
}

bool chunk_manager_check_collision(ChunkManager* manager, int world_x, int world_y) {
    if (manager->active_count == 0) return false;
    
    // Find which chunk contains this world coordinate
    int target_chunk_x = (int)floorf((float)world_x / CHUNK_SIZE);
    int target_chunk_y = (int)floorf((float)world_y / CHUNK_SIZE);
    
    // Look for the chunk in active chunks
    for (int i = 0; i < manager->active_count; i++) {
        TerrainChunk* chunk = manager->active_chunks[i];
        if (chunk->coord.chunk_x == target_chunk_x && chunk->coord.chunk_y == target_chunk_y) {
            int chunk_base_x = chunk->coord.chunk_x * CHUNK_SIZE;
            int chunk_base_y = chunk->coord.chunk_y * CHUNK_SIZE;
            
            int local_x = world_x - chunk_base_x;
            int local_y = world_y - chunk_base_y;
            
            static int debug_count = 0;
            if (debug_count < 15) {
                printf("      World(%d,%d) -> chunk(%d,%d) -> local(%d,%d)\n", 
                       world_x, world_y, target_chunk_x, target_chunk_y, local_x, local_y);
                debug_count++;
            }
            
            return terrain_check_collision(chunk->terrain, local_x, local_y);
        }
    }
    
    // Chunk not loaded - return false (water)
    static int miss_count = 0;
    if (miss_count < 5) {
        printf("      World(%d,%d) -> chunk(%d,%d) NOT LOADED\n", 
               world_x, world_y, target_chunk_x, target_chunk_y);
        miss_count++;
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
    static int debug_ray = -1;
    static int step_count = 0;
    
    if (!raycaster->bres_active) {
        return false;
    }
    
    *x = raycaster->bres_x;
    *y = raycaster->bres_y;
    
    if (debug_ray == -1 || debug_ray == 0) { // Debug first ray
        if (step_count < 10) {
            printf("    Bresham step %d: pos(%d,%d) err=%d dx=%d dy=%d sx=%d sy=%d\n", 
                   step_count, *x, *y, raycaster->bres_err, 
                   raycaster->bres_dx, raycaster->bres_dy, 
                   raycaster->bres_sx, raycaster->bres_sy);
        }
        step_count++;
        if (step_count == 1) debug_ray = 0; // Lock to ray 0
    }
    
    // Continue stepping until we reach the end point
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
    
    // Check if we should continue - distance from start
    int16_t start_x = raycaster->bres_x - raycaster->bres_sx * step_count;
    int16_t start_y = raycaster->bres_y - raycaster->bres_sy * step_count;
    int16_t dx = raycaster->bres_x - start_x;
    int16_t dy = raycaster->bres_y - start_y;
    
    if (dx * dx + dy * dy > 64 * 64) {
        raycaster->bres_active = false;
        if (debug_ray == 0 && step_count < 15) {
            printf("    Bresham stopping: distance too far\n");
        }
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
    
    printf("Casting %d rays from (%d,%d)\n", pattern->direction_count, start_x, start_y);
    
    for (uint16_t i = 0; i < pattern->direction_count; i++) {
        RayDirection* dir = &pattern->directions[i];
        RayResult* result = &results[i];
        
        // Cast ray in this direction
        int16_t end_x = start_x + (dir->dx * 64 / 1000);
        int16_t end_y = start_y + (dir->dy * 64 / 1000);
        
        if (i < 4) { // Debug first 4 rays
            printf("Ray %d: dir(%d,%d) -> end_point(%d,%d)\n", i, dir->dx, dir->dy, end_x, end_y);
        }
        
        raycaster_bresham_init(raycaster, start_x, start_y, end_x, end_y);
        
        int16_t x, y;
        int16_t steps = 0;
        bool found_collision = false;
        
        while (raycaster_bresham_step(raycaster, &x, &y) && steps < 64) {
            if (i < 4 && steps < 5) { // Debug first few steps of first few rays
                printf("  Step %d: (%d,%d)\n", steps, x, y);
            }
            
            if (x != start_x || y != start_y) { // Don't check starting position
                bool collision = collision_check(x, y, collision_context);
                
                if (i < 4 && steps < 5) {
                    printf("    Collision check at (%d,%d): %s\n", x, y, collision ? "HIT" : "miss");
                }
                
                if (collision) {
                    result->hit_terrain = true;
                    result->hit_x = x;
                    result->hit_y = y;
                    result->distance = steps;
                    result->ray_complete = true;
                    found_collision = true;
                    hits++;
                    if (i < 4) {
                        printf("  Ray %d: HIT TERRAIN at (%d,%d) after %d steps\n", i, x, y, steps);
                    }
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
            if (i < 4) {
                printf("  Ray %d: No collision, reached end at (%d,%d)\n", i, end_x, end_y);
            }
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
    (void)x; (void)y; (void)is_terrain;
    chart->points_added_this_frame++;
    return true;
}

SonarBounds sonar_bounds_create(int16_t min_x, int16_t min_y, int16_t max_x, int16_t max_y) {
    SonarBounds bounds = {min_x, min_y, max_x, max_y};
    return bounds;
}

uint16_t sonar_chart_query_area(SonarChart* chart, SonarBounds bounds, SonarPoint** results, uint16_t max_results) {
    (void)chart; (void)bounds; (void)results; (void)max_results;
    return 0; // Simplified - no actual storage
}

// Test collision callback
bool test_collision_callback(int16_t x, int16_t y, void* context) {
    ChunkManager* chunk_manager = (ChunkManager*)context;
    bool collision = chunk_manager_check_collision(chunk_manager, x, y);
    return collision;
}

int main(void) {
    printf("=== Standalone Full Pipeline Test ===\n\n");
    
    // Initialize systems
    ChunkManager* chunk_manager = chunk_manager_alloc();
    Raycaster* raycaster = raycaster_alloc();
    SonarChart* sonar_chart = sonar_chart_alloc();
    
    if (!chunk_manager || !raycaster || !sonar_chart) {
        printf("ERROR: Failed to allocate components\n");
        return 1;
    }
    
    // Submarine position
    float world_x = 64.0f;
    float world_y = 32.0f;
    printf("Submarine at: (%.1f, %.1f)\n", world_x, world_y);
    
    // Load chunks
    chunk_manager_update(chunk_manager, world_x, world_y);
    printf("Loaded %d chunks\n", chunk_manager->active_count);
    
    // Check terrain around submarine
    printf("\nTerrain around submarine:\n");
    int terrain_count = 0;
    for (int dy = -3; dy <= 3; dy++) {
        printf("Row %2d: ", dy);
        for (int dx = -3; dx <= 3; dx++) {
            bool collision = chunk_manager_check_collision(chunk_manager, 
                                                          (int)world_x + dx, (int)world_y + dy);
            printf("%c", collision ? '#' : '.');
            if (collision) terrain_count++;
        }
        printf("\n");
    }
    printf("Terrain pixels in 7x7 area: %d\n\n", terrain_count);
    
    // Test raycasting
    printf("=== Raycasting Test ===\n");
    RayPattern* pattern = raycaster_get_adaptive_pattern(raycaster, false);
    printf("Using pattern with %d rays\n", pattern->direction_count);
    
    RayResult results[RAY_CACHE_SIZE];
    memset(results, 0, sizeof(results));
    
    uint16_t hits = raycaster_cast_pattern(
        raycaster, pattern,
        (int16_t)world_x, (int16_t)world_y,
        results, test_collision_callback, chunk_manager
    );
    
    printf("Raycasting result: %d terrain hits out of %d rays\n", hits, pattern->direction_count);
    
    // Show first few results
    uint16_t terrain_hits = 0;
    for (uint16_t i = 0; i < pattern->direction_count && i < 8; i++) {
        RayResult* result = &results[i];
        if (result->ray_complete && result->hit_terrain) {
            terrain_hits++;
            printf("  Ray %d: TERRAIN at (%d,%d) distance=%d\n", 
                   i, result->hit_x, result->hit_y, result->distance);
        }
    }
    
    printf("\nTotal terrain hits: %d\n", hits);
    
    if (hits <= 3) {
        printf("\n*** BUG REPRODUCED: Only %d terrain hits! ***\n", hits);
        printf("Expected: Many hits since terrain exists around submarine\n");
        
        if (terrain_count > 0) {
            printf("Terrain exists but rays aren't finding it - raycasting bug!\n");
        } else {
            printf("No terrain around submarine - terrain generation bug!\n");
        }
    } else {
        printf("\n*** Test passed: Found %d terrain hits ***\n", hits);
    }
    
    // Cleanup
    sonar_chart_free(sonar_chart);
    raycaster_free(raycaster);
    chunk_manager_free(chunk_manager);
    
    return 0;
}