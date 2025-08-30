#include "terrain.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifdef TEST_BUILD
#include "mock_furi.h"
#else
#include <furi.h>
#endif

// Diamond-square algorithm constants (adapted for uint8_t)
#define MAX_DELTA 80        // Max random variation (0-255 scale)
#define ROUGHNESS_DECAY 2   // How much roughness decreases each iteration

// Random number generation with seed
static uint32_t terrain_seed = 12345;

static void terrain_srand(uint32_t seed) {
    terrain_seed = seed;
}

static uint8_t terrain_rand(void) {
    terrain_seed = terrain_seed * 1103515245 + 12345;
    return (terrain_seed >> 16) & 0xFF; // Return 0-255
}

static int16_t terrain_rand_range(int16_t range) {
    return (int16_t)terrain_rand() * range / 255 - (range / 2);
}

TerrainManager* terrain_manager_alloc(uint32_t seed, uint8_t elevation) {
    TerrainManager* terrain = malloc(sizeof(TerrainManager));
    if(!terrain) return NULL;
    
    // Use single chunk size for new chunk-based system
    terrain->width = CHUNK_TERRAIN_SIZE;
    terrain->height = CHUNK_TERRAIN_SIZE;
    terrain->elevation_threshold = elevation;
    terrain->seed = seed;
    
    // Allocate height map (much smaller now!)
    size_t map_size = terrain->width * terrain->height;
    terrain->height_map = malloc(map_size * sizeof(uint8_t));  // 4x smaller!
    terrain->collision_map = malloc(map_size * sizeof(bool));
    
    if(!terrain->height_map || !terrain->collision_map) {
        terrain_manager_free(terrain);
        return NULL;
    }
    
    // Generate terrain
    terrain_generate_diamond_square(terrain);
    terrain_apply_elevation_threshold(terrain);
    
    return terrain;
}

void terrain_manager_free(TerrainManager* terrain) {
    if(!terrain) return;
    
    if(terrain->height_map) free(terrain->height_map);
    if(terrain->collision_map) free(terrain->collision_map);
    free(terrain);
}

static uint8_t terrain_get_height(TerrainManager* terrain, int x, int y) {
    if(x < 0 || x >= terrain->width || y < 0 || y >= terrain->height) {
        return 0;
    }
    return terrain->height_map[y * terrain->width + x];
}

static void terrain_set_height(TerrainManager* terrain, int x, int y, uint8_t height) {
    if(x < 0 || x >= terrain->width || y < 0 || y >= terrain->height) {
        return;
    }
    terrain->height_map[y * terrain->width + x] = height;
}

static void terrain_init_corners(TerrainManager* terrain) {
    int step = terrain->width - 1;
    terrain_srand(terrain->seed);
    
    // Initialize corner values with more balanced distribution (70-180 range)
    terrain_set_height(terrain, 0, 0, 70 + (terrain_rand() % 110));
    terrain_set_height(terrain, step, 0, 70 + (terrain_rand() % 110));
    terrain_set_height(terrain, 0, step, 70 + (terrain_rand() % 110));
    terrain_set_height(terrain, step, step, 70 + (terrain_rand() % 110));
}

static void terrain_diamond_step(TerrainManager* terrain, int x, int y, int size, int16_t roughness) {
    int half = size / 2;
    
    // Get corner values
    uint8_t tl = terrain_get_height(terrain, x - half, y - half);
    uint8_t tr = terrain_get_height(terrain, x + half, y - half);
    uint8_t bl = terrain_get_height(terrain, x - half, y + half);
    uint8_t br = terrain_get_height(terrain, x + half, y + half);
    
    // Calculate average and add random offset
    int16_t avg = (tl + tr + bl + br) / 4;
    int16_t offset = terrain_rand_range(roughness);
    int16_t new_height = avg + offset;
    
    // Clamp to 0-255 range
    if(new_height < 0) new_height = 0;
    if(new_height > 255) new_height = 255;
    
    terrain_set_height(terrain, x, y, (uint8_t)new_height);
}

static void terrain_square_step(TerrainManager* terrain, int x, int y, int size, int16_t roughness) {
    int half = size / 2;
    int16_t total = 0;
    int count = 0;
    
    // Sample neighboring points
    if(x - half >= 0) {
        total += terrain_get_height(terrain, x - half, y);
        count++;
    }
    if(x + half < terrain->width) {
        total += terrain_get_height(terrain, x + half, y);
        count++;
    }
    if(y - half >= 0) {
        total += terrain_get_height(terrain, x, y - half);
        count++;
    }
    if(y + half < terrain->height) {
        total += terrain_get_height(terrain, x, y + half);
        count++;
    }
    
    if(count > 0) {
        int16_t avg = total / count;
        int16_t offset = terrain_rand_range(roughness);
        int16_t new_height = avg + offset;
        
        // Clamp to 0-255 range
        if(new_height < 0) new_height = 0;
        if(new_height > 255) new_height = 255;
        
        terrain_set_height(terrain, x, y, (uint8_t)new_height);
    }
}

void terrain_generate_diamond_square(TerrainManager* terrain) {
    // Initialize corners
    terrain_init_corners(terrain);
    
    int size = terrain->width;
    int16_t roughness = MAX_DELTA;
    
    while(size >= 3) {
        int half = size / 2;
        
        // Diamond step
        for(int y = half; y < terrain->height; y += size - 1) {
            for(int x = half; x < terrain->width; x += size - 1) {
                terrain_diamond_step(terrain, x, y, size, roughness);
            }
        }
        
        // Square step
        for(int y = 0; y < terrain->height; y += half) {
            for(int x = (y / half) % 2 == 0 ? half : 0; x < terrain->width; x += size - 1) {
                terrain_square_step(terrain, x, y, size, roughness);
            }
        }
        
        size = half + 1;
        roughness = roughness / ROUGHNESS_DECAY;
    }
}

void terrain_apply_elevation_threshold(TerrainManager* terrain) {
    uint16_t land_count = 0;
    uint8_t min_height = 255, max_height = 0;
    
    for(int y = 0; y < terrain->height; y++) {
        for(int x = 0; x < terrain->width; x++) {
            int idx = y * terrain->width + x;
            uint8_t height = terrain->height_map[idx];
            
            // Track statistics
            if(height < min_height) min_height = height;
            if(height > max_height) max_height = height;
            
            terrain->collision_map[idx] = (height > terrain->elevation_threshold);
            if(terrain->collision_map[idx]) land_count++;
        }
    }
    
    // Log terrain statistics for debugging
    uint16_t total_pixels = terrain->width * terrain->height;
    uint16_t land_percentage = (land_count * 100) / total_pixels;
    FURI_LOG_D("Terrain", "Chunk stats: %d%% land (%d/%d), heights: %d-%d, threshold: %d", 
               land_percentage, land_count, total_pixels, min_height, max_height, terrain->elevation_threshold);
    
    // Apply despeckle filter - remove isolated land pixels
    bool* temp_map = malloc(terrain->width * terrain->height * sizeof(bool));
    if(!temp_map) return;
    
    memcpy(temp_map, terrain->collision_map, terrain->width * terrain->height * sizeof(bool));
    
    for(int y = 0; y < terrain->height; y++) {
        for(int x = 0; x < terrain->width; x++) {
            int idx = y * terrain->width + x;
            if(temp_map[idx]) { // If this is land
                bool has_neighbor = false;
                
                // Check 8-connected neighbors (less aggressive than 4-connected)
                for(int dy = -1; dy <= 1; dy++) {
                    for(int dx = -1; dx <= 1; dx++) {
                        if(dx == 0 && dy == 0) continue;
                        
                        int nx = x + dx;
                        int ny = y + dy;
                        
                        if(nx >= 0 && nx < terrain->width && ny >= 0 && ny < terrain->height) {
                            if(temp_map[ny * terrain->width + nx]) {
                                has_neighbor = true;
                                break;
                            }
                        }
                    }
                    if(has_neighbor) break;
                }
                
                if(!has_neighbor) {
                    terrain->collision_map[idx] = false; // Remove isolated pixel
                }
            }
        }
    }
    
    free(temp_map);
}

bool terrain_check_collision(TerrainManager* terrain, int x, int y) {
    if(!terrain || x < 0 || x >= terrain->width || y < 0 || y >= terrain->height) {
        return false;
    }
    return terrain->collision_map[y * terrain->width + x];
}

void terrain_render_area(TerrainManager* terrain, Canvas* canvas, int start_x, int start_y, int end_x, int end_y) {
    if(!terrain) return;
    
    // Clamp to screen and terrain bounds
    start_x = MAX(0, MIN(start_x, terrain->width - 1));
    start_y = MAX(0, MIN(start_y, terrain->height - 1));
    end_x = MAX(0, MIN(end_x, terrain->width - 1));
    end_y = MAX(0, MIN(end_y, terrain->height - 1));
    
    // Render terrain pixels
    for(int y = start_y; y <= end_y; y++) {
        for(int x = start_x; x <= end_x; x++) {
            if(terrain_check_collision(terrain, x, y)) {
                canvas_draw_dot(canvas, x, y);
            }
        }
    }
}