#include "chunk_manager.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifdef TEST_BUILD
#include "mock_furi.h"
#else
#include <furi.h>
#endif

// Chunk coordinate conversion
ChunkCoord world_to_chunk_coord(float world_x, float world_y) {
    ChunkCoord coord;
    coord.chunk_x = (int)floorf(world_x / CHUNK_SIZE);
    coord.chunk_y = (int)floorf(world_y / CHUNK_SIZE);
    return coord;
}

bool chunk_coord_equals(ChunkCoord a, ChunkCoord b) {
    return a.chunk_x == b.chunk_x && a.chunk_y == b.chunk_y;
}

uint32_t chunk_coord_hash(ChunkCoord coord) {
    return (uint32_t)(coord.chunk_x * 73856093) ^ (uint32_t)(coord.chunk_y * 19349663);
}

// Memory pool management
bool chunk_pool_init(ChunkPool* pool, uint32_t size) {
    pool->chunks = malloc(size * sizeof(TerrainChunk));
    pool->chunk_in_use = malloc(size * sizeof(bool));
    
    if(!pool->chunks || !pool->chunk_in_use) {
        if(pool->chunks) free(pool->chunks);
        if(pool->chunk_in_use) free(pool->chunk_in_use);
        return false;
    }
    
    pool->pool_size = size;
    pool->next_free = 0;
    
    // Initialize all chunks as free
    memset(pool->chunk_in_use, 0, size * sizeof(bool));
    for(uint32_t i = 0; i < size; i++) {
        pool->chunks[i].terrain = NULL;
        pool->chunks[i].is_loaded = false;
        pool->chunks[i].is_dirty = false;
        pool->chunks[i].last_access_time = 0;
    }
    
    return true;
}

void chunk_pool_cleanup(ChunkPool* pool) {
    if(pool->chunks) {
        // Free any allocated terrain data
        for(uint32_t i = 0; i < pool->pool_size; i++) {
            if(pool->chunks[i].terrain) {
                terrain_manager_free(pool->chunks[i].terrain);
            }
        }
        free(pool->chunks);
    }
    if(pool->chunk_in_use) free(pool->chunk_in_use);
}

TerrainChunk* chunk_pool_alloc(ChunkPool* pool) {
    // Find next free chunk
    for(uint32_t i = 0; i < pool->pool_size; i++) {
        uint32_t index = (pool->next_free + i) % pool->pool_size;
        if(!pool->chunk_in_use[index]) {
            pool->chunk_in_use[index] = true;
            pool->next_free = (index + 1) % pool->pool_size;
            return &pool->chunks[index];
        }
    }
    return NULL; // Pool exhausted
}

void chunk_pool_free(ChunkPool* pool, TerrainChunk* chunk) {
    // Find chunk in pool and mark as free
    for(uint32_t i = 0; i < pool->pool_size; i++) {
        if(&pool->chunks[i] == chunk) {
            if(chunk->terrain) {
                terrain_manager_free(chunk->terrain);
                chunk->terrain = NULL;
            }
            chunk->is_loaded = false;
            chunk->is_dirty = false;
            pool->chunk_in_use[i] = false;
            break;
        }
    }
}

// Chunk manager lifecycle
ChunkManager* chunk_manager_alloc(void) {
    ChunkManager* manager = malloc(sizeof(ChunkManager));
    if(!manager) return NULL;
    
    // Initialize active chunks array
    memset(manager->active_chunks, 0, sizeof(manager->active_chunks));
    
    // Initialize memory pool
    if(!chunk_pool_init(&manager->pool, CHUNK_CACHE_SIZE + MAX_ACTIVE_CHUNKS + 4)) {
        free(manager);
        return NULL;
    }
    
    // Initialize cache (disabled)
    manager->cache.dummy = 0;
    
    // Initialize position tracking
    manager->player_world_x = 0;
    manager->player_world_y = 0;
    manager->center_chunk = world_to_chunk_coord(0, 0);
    
    // Initialize performance counters
    chunk_manager_reset_frame_stats(manager);
    
    return manager;
}

void chunk_manager_free(ChunkManager* manager) {
    if(!manager) return;
    
    // Clean up memory pool (this frees all terrain data)
    chunk_pool_cleanup(&manager->pool);
    
    free(manager);
}

// Cache management (disabled for memory savings)
TerrainChunk* chunk_manager_get_from_cache(ChunkManager* manager, ChunkCoord coord) {
    (void)manager; (void)coord; // Suppress unused parameter warnings
    return NULL; // No caching
}

void chunk_manager_cache_chunk(ChunkManager* manager, TerrainChunk* chunk) {
    (void)manager; (void)chunk; // Suppress unused parameter warnings
    // No caching - do nothing
}

// Chunk loading/unloading
TerrainChunk* chunk_manager_load_chunk(ChunkManager* manager, ChunkCoord coord) {
    uint32_t start_time = furi_get_tick();
    
    // No caching - always create new chunk
    // Allocate new chunk from pool
    TerrainChunk* chunk = chunk_pool_alloc(&manager->pool);
    if(!chunk) return NULL;
    
    // Initialize chunk
    chunk->coord = coord;
    chunk->last_access_time = furi_get_tick();
    chunk->is_dirty = false;
    
    // Generate terrain seed based on chunk coordinates
    chunk->generation_seed = chunk_coord_hash(coord);
    
    // Create terrain for this chunk  
    chunk->terrain = terrain_manager_alloc(chunk->generation_seed, 90); // 90 = lower threshold for more water
    chunk->is_loaded = (chunk->terrain != NULL);
    
    if(chunk->is_loaded) {
        manager->chunks_loaded_this_frame++;
        manager->generation_time_ms += furi_get_tick() - start_time;
        FURI_LOG_D("ChunkMgr", "Loaded chunk (%d,%d) with seed 0x%08lX in %lu ms", 
                   coord.chunk_x, coord.chunk_y, chunk->generation_seed, furi_get_tick() - start_time);
    }
    
    return chunk;
}

void chunk_manager_unload_chunk(ChunkManager* manager, TerrainChunk* chunk) {
    if(!chunk) return;
    
    // No caching - immediately free the chunk to save memory
    chunk_pool_free(&manager->pool, chunk);
    manager->chunks_unloaded_this_frame++;
}

// Active chunk management
int chunk_manager_get_active_index(ChunkManager* manager, ChunkCoord coord) {
    // Convert world chunk coord to local grid index (0-3 for 2x2 grid)
    int relative_x = coord.chunk_x - manager->center_chunk.chunk_x;
    int relative_y = coord.chunk_y - manager->center_chunk.chunk_y;
    
    if(relative_x < 0 || relative_x >= 2 || relative_y < 0 || relative_y >= 2) {
        return -1; // Outside active grid
    }
    
    return relative_y * 2 + relative_x;
}

// Core chunk operations
void chunk_manager_update(ChunkManager* manager, float player_x, float player_y) {
    manager->player_world_x = player_x;
    manager->player_world_y = player_y;
    
    ChunkCoord new_center = world_to_chunk_coord(player_x, player_y);
    
    if(!chunk_coord_equals(new_center, manager->center_chunk)) {
        // Center chunk has changed - rebuild active chunk grid
        TerrainChunk* old_chunks[MAX_ACTIVE_CHUNKS];
        memcpy(old_chunks, manager->active_chunks, sizeof(old_chunks));
        memset(manager->active_chunks, 0, sizeof(manager->active_chunks));
        
        manager->center_chunk = new_center;
        
        // Load new 2x2 grid
        for(int dy = 0; dy <= 1; dy++) {
            for(int dx = 0; dx <= 1; dx++) {
                ChunkCoord coord = {new_center.chunk_x + dx, new_center.chunk_y + dy};
                int index = dy * 2 + dx;
                
                // Check if chunk was already loaded
                TerrainChunk* existing = NULL;
                for(int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
                    if(old_chunks[i] && chunk_coord_equals(old_chunks[i]->coord, coord)) {
                        existing = old_chunks[i];
                        old_chunks[i] = NULL; // Mark as reused
                        break;
                    }
                }
                
                if(existing) {
                    manager->active_chunks[index] = existing;
                } else {
                    manager->active_chunks[index] = chunk_manager_load_chunk(manager, coord);
                }
            }
        }
        
        // Unload chunks that are no longer needed
        for(int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
            if(old_chunks[i]) {
                chunk_manager_unload_chunk(manager, old_chunks[i]);
            }
        }
    }
}

TerrainChunk* chunk_manager_get_chunk_at(ChunkManager* manager, int world_x, int world_y) {
    ChunkCoord coord = world_to_chunk_coord((float)world_x, (float)world_y);
    int index = chunk_manager_get_active_index(manager, coord);
    
    if(index >= 0 && index < MAX_ACTIVE_CHUNKS) {
        return manager->active_chunks[index];
    }
    
    return NULL;
}

bool chunk_manager_check_collision(ChunkManager* manager, int world_x, int world_y) {
    TerrainChunk* chunk = chunk_manager_get_chunk_at(manager, world_x, world_y);
    if(!chunk || !chunk->terrain) return false;
    
    // Convert world coordinates to local chunk coordinates
    ChunkCoord coord = world_to_chunk_coord((float)world_x, (float)world_y);
    int local_x = world_x - (coord.chunk_x * CHUNK_SIZE);
    int local_y = world_y - (coord.chunk_y * CHUNK_SIZE);
    
    return terrain_check_collision(chunk->terrain, local_x, local_y);
}

// Performance monitoring
void chunk_manager_reset_frame_stats(ChunkManager* manager) {
    manager->chunks_loaded_this_frame = 0;
    manager->chunks_unloaded_this_frame = 0;
    manager->generation_time_ms = 0;
}

void chunk_manager_log_performance(ChunkManager* manager) {
    // This would normally log to debug output
    // For now, just reset the counters
    chunk_manager_reset_frame_stats(manager);
}