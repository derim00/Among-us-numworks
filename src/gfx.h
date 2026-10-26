#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define SCR_W 320
#define SCR_H 240
#define STRIP_H 40

extern const uint8_t _binary_assets_bin_start[];
static inline const uint8_t *asset(int i) {
  const uint32_t *t = (const uint32_t *)_binary_assets_bin_start;
  return _binary_assets_bin_start + t[1 + i];
}

// current strip
extern uint16_t g_fb[SCR_W * STRIP_H];
extern int g_sy0;   // screen y of first row in g_fb

#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

enum {
  DR_MIRROR = 1,
  DR_DIM = 2,       // brightness 50%
  DR_GREEN = 4,     // hover tint (multiply by green)
  DR_SIL = 8,       // silhouette in g_silcol
  DR_DARK = 16,     // darker (25%)
};
extern uint16_t g_silcol;

void gfx_fill(int x, int y, int w, int h, uint16_t c);
void gfx_fill_blend(int x, int y, int w, int h, uint16_t c, int a);  // a 0..255
void gfx_hline(int x, int y, int w, uint16_t c);
void gfx_rect(int x, int y, int w, int h, uint16_t c);
void gfx_rle(int id, int x, int y, int flags);       // x,y = screen position of the sprite origin
void gfx_rle_size(int id, int *w, int *h);
void gfx_raw_rot(int id, int x, int y, float ang, float scale);
void gfx_tiles(int id, int mx0, int my0);            // map pixel at screen (0,0)
void gfx_stars(int ox, int oy);                      // tiled starfield, pattern origin offset
void gfx_light(int cx, int cy);                      // darkness around (cx,cy)
int gfx_text(int font, const char *s, int x, int y, uint16_t col, int align); // align 0 left 1 center 2 right; y = top
int gfx_text_w(int font, const char *s);
int gfx_font_h(int font);
void gfx_text_ol(int font, const char *s, int x, int y, uint16_t col, uint16_t ol, int align);
void gfx_present(void);
