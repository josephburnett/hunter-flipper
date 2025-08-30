#pragma once
#include "engine/engine.h"
#include "terrain.h"
#include "chunk_manager.h"
#include "sonar_chart.h"
#include "raycaster.h"

typedef enum {
    GAME_MODE_NAV,
    GAME_MODE_TORPEDO
} GameMode;

typedef struct {
    // Submarine state (world coordinates)
    float world_x;
    float world_y;
    float velocity;
    float heading;
    GameMode mode;
    
    // Screen position (always centered)
    float screen_x;
    float screen_y;
    
    // Torpedo management
    uint8_t torpedo_count;
    uint8_t max_torpedoes;
    
    // Sonar state
    bool ping_active;
    float ping_x;
    float ping_y;
    uint8_t ping_radius;
    uint32_t ping_timer;
    
    // Input state
    uint32_t back_press_start;
    bool back_long_press;
    
    // Game settings
    float max_velocity;
    float turn_rate;
    float acceleration;
    
    // Infinite terrain system
    ChunkManager* chunk_manager;
    
    // Advanced sonar chart with fading
    SonarChart* sonar_chart;
    
    // Optimized raycasting
    Raycaster* raycaster;
} GameContext;