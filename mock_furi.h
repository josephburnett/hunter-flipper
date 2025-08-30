#pragma once
#include <stdint.h>

// Mock Flipper Zero APIs for testing

#ifdef TEST_BUILD

// Furi logging mock
#define FURI_LOG_NONE  0
#define FURI_LOG_E     1
#define FURI_LOG_W     2
#define FURI_LOG_I     3
#define FURI_LOG_T     5

#define FURI_LOG_D(tag, format, ...) furi_log_print_format(4, tag, format, ##__VA_ARGS__)

uint32_t furi_get_tick(void);
void furi_log_print_format(int level, const char* tag, const char* format, ...);

// Canvas mock
typedef void Canvas;

void canvas_draw_dot(Canvas* canvas, int x, int y);
void canvas_draw_circle(Canvas* canvas, int x, int y, int radius);
void canvas_draw_line(Canvas* canvas, float x1, float y1, float x2, float y2);
void canvas_draw_disc(Canvas* canvas, int x, int y, int radius);
void canvas_printf(Canvas* canvas, int x, int y, const char* format, ...);

#else
// Normal Flipper build - use real furi.h
#include <furi.h>
#endif