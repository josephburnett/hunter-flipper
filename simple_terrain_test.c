#define TEST_BUILD
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

// Simple mock for FURI_LOG_D
#define FURI_LOG_D(tag, format, ...) printf("[DEBUG] %s: " format "\n", tag, ##__VA_ARGS__)

// Mock furi_get_tick
uint32_t furi_get_tick(void) {
    return 12345; // Fixed time for testing
}

// Define key constants from headers
#define CHUNK_SIZE 33
#define CHUNK_TERRAIN_SIZE CHUNK_SIZE
#define MAX_ACTIVE_CHUNKS 4

// Simple terrain manager structure
typedef struct {
    uint8_t* height_map;        
    bool* collision_map; 
    uint16_t width;
    uint16_t height;
    uint8_t elevation_threshold;
    uint32_t seed;
} TerrainManager;

// Chunk coordinate structure
typedef struct {
    int chunk_x;
    int chunk_y;
} ChunkCoord;

// Inline terrain functions from terrain.c (simplified)
static uint32_t terrain_seed = 12345;

static void terrain_srand(uint32_t seed) {
    terrain_seed = seed;
}

static uint8_t terrain_rand(void) {
    terrain_seed = terrain_seed * 1103515245 + 12345;
    return (terrain_seed >> 16) & 0xFF;
}

static int16_t terrain_rand_range(int16_t range) {
    return (int16_t)terrain_rand() * range / 255 - (range / 2);
}

TerrainManager* terrain_manager_alloc(uint32_t seed, uint8_t elevation) {
    TerrainManager* terrain = malloc(sizeof(TerrainManager));
    if(!terrain) return NULL;
    
    terrain->width = CHUNK_TERRAIN_SIZE;
    terrain->height = CHUNK_TERRAIN_SIZE;
    terrain->elevation_threshold = elevation;
    terrain->seed = seed;
    
    size_t map_size = terrain->width * terrain->height;
    terrain->height_map = malloc(map_size * sizeof(uint8_t));
    terrain->collision_map = malloc(map_size * sizeof(bool));
    
    if(!terrain->height_map || !terrain->collision_map) {
        if(terrain->height_map) free(terrain->height_map);
        if(terrain->collision_map) free(terrain->collision_map);
        free(terrain);
        return NULL;
    }
    
    // Initialize corners with biased heights (150-200 range)
    terrain_srand(seed);
    int step = terrain->width - 1;
    
    // Set corner values with balanced distribution (70-180 range)
    terrain->height_map[0] = 70 + (terrain_rand() % 110);  // Top-left
    terrain->height_map[step] = 70 + (terrain_rand() % 110);  // Top-right
    terrain->height_map[step * terrain->width] = 70 + (terrain_rand() % 110);  // Bottom-left
    terrain->height_map[step * terrain->width + step] = 70 + (terrain_rand() % 110);  // Bottom-right
    
    // Fill in rest with simple pattern for testing
    for(int y = 0; y < terrain->height; y++) {
        for(int x = 0; x < terrain->width; x++) {
            if(x == 0 || x == step || y == 0 || y == step) {
                // Edges already set or use average of corners
                if(terrain->height_map[y * terrain->width + x] == 0) {
                    terrain->height_map[y * terrain->width + x] = 125; // Middle value
                }
            } else {
                // Interior points - add some variation around middle value
                terrain->height_map[y * terrain->width + x] = 125 + terrain_rand_range(50);
            }
        }
    }
    
    // Apply elevation threshold
    uint16_t land_count = 0;
    uint8_t min_height = 255, max_height = 0;
    
    for(int y = 0; y < terrain->height; y++) {
        for(int x = 0; x < terrain->width; x++) {
            int idx = y * terrain->width + x;
            uint8_t height = terrain->height_map[idx];
            
            if(height < min_height) min_height = height;
            if(height > max_height) max_height = height;
            
            terrain->collision_map[idx] = (height > terrain->elevation_threshold);
            if(terrain->collision_map[idx]) land_count++;
        }
    }
    
    uint16_t total_pixels = terrain->width * terrain->height;
    uint16_t land_percentage = (land_count * 100) / total_pixels;
    FURI_LOG_D("Terrain", "Chunk stats: %d%% land (%d/%d), heights: %d-%d, threshold: %d", 
               land_percentage, land_count, total_pixels, min_height, max_height, terrain->elevation_threshold);
    
    return terrain;
}

void terrain_manager_free(TerrainManager* terrain) {
    if(!terrain) return;
    if(terrain->height_map) free(terrain->height_map);
    if(terrain->collision_map) free(terrain->collision_map);
    free(terrain);
}

bool terrain_check_collision(TerrainManager* terrain, int x, int y) {
    if(!terrain || x < 0 || x >= terrain->width || y < 0 || y >= terrain->height) {
        return false;
    }
    return terrain->collision_map[y * terrain->width + x];
}

// Chunk coordinate functions
ChunkCoord world_to_chunk_coord(float world_x, float world_y) {
    ChunkCoord coord;
    coord.chunk_x = (int)floorf(world_x / (CHUNK_SIZE - 1));
    coord.chunk_y = (int)floorf(world_y / (CHUNK_SIZE - 1));
    return coord;
}

uint32_t chunk_coord_hash(ChunkCoord coord) {
    return (uint32_t)(coord.chunk_x * 73856093) ^ (uint32_t)(coord.chunk_y * 19349663);
}

void print_terrain_sample(TerrainManager* terrain, int size) {
    printf("\nTerrain sample (%dx%d):\n", size, size);
    for(int y = 0; y < size && y < terrain->height; y++) {
        printf("    ");
        for(int x = 0; x < size && x < terrain->width; x++) {
            bool is_land = terrain_check_collision(terrain, x, y);
            printf("%c", is_land ? '#' : '.');
        }
        printf("\n");
    }
}

int main(void) {
    printf("=== Simple Terrain Generation Test ===\n");
    
    // Test starting position like the game
    float world_x = 64.0f;
    float world_y = 32.0f;
    
    printf("Testing terrain around submarine start position: (%.1f, %.1f)\n", world_x, world_y);
    
    // Calculate which chunks would be loaded
    ChunkCoord center_coord = world_to_chunk_coord(world_x, world_y);
    printf("Center chunk coordinate: (%d, %d)\n", center_coord.chunk_x, center_coord.chunk_y);
    
    // Test 2x2 chunk grid like the game uses
    printf("\nGenerating 2x2 chunk grid:\n");
    TerrainManager* chunks[4] = {NULL};
    
    int chunk_idx = 0;
    for(int dy = 0; dy <= 1; dy++) {
        for(int dx = 0; dx <= 1; dx++) {
            ChunkCoord coord = {center_coord.chunk_x + dx, center_coord.chunk_y + dy};
            uint32_t seed = chunk_coord_hash(coord);
            
            printf("  Chunk [%d]: (%d,%d) seed=0x%08X\n", chunk_idx, coord.chunk_x, coord.chunk_y, seed);
            
            chunks[chunk_idx] = terrain_manager_alloc(seed, 100); // elevation threshold 100
            if(chunks[chunk_idx]) {
                printf("    Generated successfully\n");
            } else {
                printf("    FAILED to generate!\n");
            }
            chunk_idx++;
        }
    }
    
    // Find which chunk contains the submarine
    int sub_chunk_x = (int)world_x - (center_coord.chunk_x * (CHUNK_SIZE - 1));
    int sub_chunk_y = (int)world_y - (center_coord.chunk_y * (CHUNK_SIZE - 1)); 
    int sub_chunk_idx = 0; // Should be chunk [0,0] for this position
    
    printf("\nSubmarine local position in chunk [%d]: (%d, %d)\n", sub_chunk_idx, sub_chunk_x, sub_chunk_y);
    
    if(chunks[sub_chunk_idx]) {
        printf("\nTerrain around submarine position:\n");
        print_terrain_sample(chunks[sub_chunk_idx], 20);
        
        // Count terrain in area around submarine
        int terrain_count = 0;
        int total_count = 0;
        int radius = 15;
        
        for(int y = sub_chunk_y - radius; y <= sub_chunk_y + radius; y++) {
            for(int x = sub_chunk_x - radius; x <= sub_chunk_x + radius; x++) {
                total_count++;
                if(terrain_check_collision(chunks[sub_chunk_idx], x, y)) {
                    terrain_count++;
                }
            }
        }
        
        float land_percentage = total_count > 0 ? (float)terrain_count * 100.0f / total_count : 0;
        printf("Terrain around submarine (%dx%d area): %d/%d (%.1f%%)\n", 
               radius*2+1, radius*2+1, terrain_count, total_count, land_percentage);
        
        if(terrain_count == 0) {
            printf("PROBLEM: No terrain around submarine - sonar will find nothing!\n");
        } else {
            printf("SUCCESS: Terrain exists - sonar should discover %d land pixels\n", terrain_count);
        }
    }
    
    // Cleanup
    for(int i = 0; i < 4; i++) {
        if(chunks[i]) {
            terrain_manager_free(chunks[i]);
        }
    }
    
    return 0;
}