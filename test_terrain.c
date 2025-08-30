#include "terrain.h"
#include "chunk_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <furi.h>

// Simple test framework
#define ASSERT(condition, message) do { \
    if (!(condition)) { \
        printf("FAIL: %s\n", message); \
        return false; \
    } \
} while(0)

#define TEST(name) bool test_##name(void)
#define RUN_TEST(name) do { \
    printf("Running test_%s... ", #name); \
    if (test_##name()) { \
        printf("PASS\n"); \
        passed++; \
    } else { \
        printf("FAIL\n"); \
        failed++; \
    } \
    total++; \
} while(0)

// Test terrain generation produces reasonable land/water ratio
TEST(terrain_generation_ratio) {
    TerrainManager* terrain = terrain_manager_alloc(12345, 100);
    ASSERT(terrain != NULL, "Failed to allocate terrain");
    
    // Count land pixels
    int land_count = 0;
    int total_pixels = terrain->width * terrain->height;
    
    for(int y = 0; y < terrain->height; y++) {
        for(int x = 0; x < terrain->width; x++) {
            if(terrain_check_collision(terrain, x, y)) {
                land_count++;
            }
        }
    }
    
    int land_percentage = (land_count * 100) / total_pixels;
    printf("\n    Land ratio: %d%% (%d/%d pixels)", land_percentage, land_count, total_pixels);
    
    // With biased initialization (150-200) and threshold 100, we should have significant land
    ASSERT(land_percentage > 30, "Too little land generated");
    ASSERT(land_percentage < 90, "Too much land generated");
    
    terrain_manager_free(terrain);
    return true;
}

// Test terrain height values are in valid range
TEST(terrain_height_range) {
    TerrainManager* terrain = terrain_manager_alloc(54321, 100);
    ASSERT(terrain != NULL, "Failed to allocate terrain");
    
    uint8_t min_height = 255, max_height = 0;
    
    for(int y = 0; y < terrain->height; y++) {
        for(int x = 0; x < terrain->width; x++) {
            uint8_t height = terrain->height_map[y * terrain->width + x];
            if(height < min_height) min_height = height;
            if(height > max_height) max_height = height;
        }
    }
    
    printf("\n    Height range: %d-%d", min_height, max_height);
    
    ASSERT(min_height >= 0, "Height below minimum");
    ASSERT(max_height <= 255, "Height above maximum");
    ASSERT(max_height > min_height, "No height variation");
    
    terrain_manager_free(terrain);
    return true;
}

// Test deterministic generation with same seed
TEST(terrain_deterministic) {
    TerrainManager* terrain1 = terrain_manager_alloc(9999, 100);
    TerrainManager* terrain2 = terrain_manager_alloc(9999, 100);
    
    ASSERT(terrain1 != NULL && terrain2 != NULL, "Failed to allocate terrain");
    ASSERT(terrain1->width == terrain2->width, "Width mismatch");
    ASSERT(terrain1->height == terrain2->height, "Height mismatch");
    
    // Compare height maps
    int map_size = terrain1->width * terrain1->height;
    bool identical = (memcmp(terrain1->height_map, terrain2->height_map, map_size) == 0);
    ASSERT(identical, "Terrain generation not deterministic");
    
    // Compare collision maps
    bool collision_identical = (memcmp(terrain1->collision_map, terrain2->collision_map, map_size) == 0);
    ASSERT(collision_identical, "Collision maps not deterministic");
    
    terrain_manager_free(terrain1);
    terrain_manager_free(terrain2);
    return true;
}

// Test chunk coordinate conversion
TEST(chunk_coordinates) {
    // Test basic coordinate conversion
    ChunkCoord coord = world_to_chunk_coord(0, 0);
    ASSERT(coord.chunk_x == 0 && coord.chunk_y == 0, "Origin coordinate wrong");
    
    coord = world_to_chunk_coord(32, 32);
    ASSERT(coord.chunk_x == 1 && coord.chunk_y == 1, "Positive coordinate wrong");
    
    coord = world_to_chunk_coord(-1, -1);
    ASSERT(coord.chunk_x == -1 && coord.chunk_y == -1, "Negative coordinate wrong");
    
    // Test chunk hash uniqueness
    uint32_t hash1 = chunk_coord_hash((ChunkCoord){0, 0});
    uint32_t hash2 = chunk_coord_hash((ChunkCoord){1, 0});
    uint32_t hash3 = chunk_coord_hash((ChunkCoord){0, 1});
    
    ASSERT(hash1 != hash2, "Hash collision between adjacent chunks");
    ASSERT(hash1 != hash3, "Hash collision between adjacent chunks");
    ASSERT(hash2 != hash3, "Hash collision between adjacent chunks");
    
    return true;
}

// Test chunk manager basic functionality
TEST(chunk_manager_basic) {
    ChunkManager* manager = chunk_manager_alloc();
    ASSERT(manager != NULL, "Failed to allocate chunk manager");
    
    // Test initial state
    ASSERT(manager->center_chunk.chunk_x == 0, "Initial center chunk X wrong");
    ASSERT(manager->center_chunk.chunk_y == 0, "Initial center chunk Y wrong");
    
    // Test chunk loading
    chunk_manager_update(manager, 0, 0);
    
    // Should have 4 active chunks in 2x2 grid
    int active_count = 0;
    for(int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
        if(manager->active_chunks[i] != NULL) {
            active_count++;
        }
    }
    ASSERT(active_count == 4, "Wrong number of active chunks");
    
    // Test collision detection
    bool has_collision = chunk_manager_check_collision(manager, 10, 10);
    printf("\n    Collision at (10,10): %s", has_collision ? "yes" : "no");
    
    chunk_manager_free(manager);
    return true;
}

// Print terrain ASCII visualization for debugging
void print_terrain_sample(TerrainManager* terrain, int size) {
    printf("\n    Terrain sample (%dx%d):\n", size, size);
    for(int y = 0; y < size && y < terrain->height; y++) {
        printf("    ");
        for(int x = 0; x < size && x < terrain->width; x++) {
            bool is_land = terrain_check_collision(terrain, x, y);
            printf("%c", is_land ? '#' : '.');
        }
        printf("\n");
    }
}

// Test visual output for debugging
TEST(terrain_visualization) {
    TerrainManager* terrain = terrain_manager_alloc(42, 100);
    ASSERT(terrain != NULL, "Failed to allocate terrain");
    
    print_terrain_sample(terrain, 16);
    
    terrain_manager_free(terrain);
    return true;
}

int main(void) {
    printf("=== Terrain Unit Tests ===\n");
    
    int total = 0, passed = 0, failed = 0;
    
    RUN_TEST(terrain_generation_ratio);
    RUN_TEST(terrain_height_range);
    RUN_TEST(terrain_deterministic);
    RUN_TEST(chunk_coordinates);
    RUN_TEST(chunk_manager_basic);
    RUN_TEST(terrain_visualization);
    
    printf("\n=== Results ===\n");
    printf("Total: %d, Passed: %d, Failed: %d\n", total, passed, failed);
    
    return failed > 0 ? 1 : 0;
}