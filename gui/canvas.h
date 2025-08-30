#pragma once
#include <stdint.h>

// Mock canvas types for testing - match engine/canvas.h exactly
typedef void Canvas;

typedef enum {
    AlignLeft,
    AlignCenter,
    AlignRight
} Align;

// Mock canvas functions - match engine/canvas.h signatures exactly
void canvas_draw_dot(Canvas* canvas, int x, int y);
void canvas_draw_circle(Canvas* canvas, int x, int y, int radius);
void canvas_draw_line(Canvas* canvas, float x1, float y1, float x2, float y2);
void canvas_draw_disc(Canvas* canvas, int x, int y, int radius);
void canvas_printf(Canvas* canvas, uint8_t x, uint8_t y, const char* format, ...)
    __attribute__((__format__(__printf__, 4, 5)));

// Additional functions from engine/canvas.h
size_t canvas_printf_width(Canvas* canvas, const char* format, ...)
    __attribute__((__format__(__printf__, 2, 3)));
void canvas_printf_aligned(
    Canvas* canvas,
    uint8_t x,
    uint8_t y,
    Align h,
    Align v,
    const char* format,
    ...) __attribute__((__format__(__printf__, 6, 7)));
void canvas_draw_str_aligned_outline(
    Canvas* canvas,
    uint8_t x,
    uint8_t y,
    Align h,
    Align v,
    const char* cstr);