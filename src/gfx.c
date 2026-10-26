#include "gfx.h"
#include "assets.h"
#include <eadk.h>
#include <string.h>
#include <math.h>

uint16_t g_fb[SCR_W * STRIP_H];
int g_sy0;
uint16_t g_silcol;

static inline uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static inline uint16_t blend(uint16_t d, uint16_t s, int a) {
  // a: 0..256
  uint32_t dr = d >> 11, dg = (d >> 5) & 63, db = d & 31;
  uint32_t sr = s >> 11, sg = (s >> 5) & 63, sb = s & 31;
  dr += ((int)(sr - dr) * a) >> 8;
  dg += ((int)(sg - dg) * a) >> 8;
  db += ((int)(sb - db) * a) >> 8;
  return (uint16_t)((dr << 11) | (dg << 5) | db);
}
static inline uint16_t modc(uint16_t c, int flags) {
  if (flags & DR_SIL) return g_silcol;
  if (flags & DR_GREEN) c &= 0x07E0;
  if (flags & DR_DIM) c = (c >> 1) & 0x7BEF;
  if (flags & DR_DARK) c = (c >> 2) & 0x39E7;
  return c;
}

void gfx_fill(int x, int y, int w, int h, uint16_t c) {
  int y0 = y - g_sy0, y1 = y0 + h;
  if (y0 < 0) y0 = 0;
  if (y1 > STRIP_H) y1 = STRIP_H;
  int x0 = x, x1 = x + w;
  if (x0 < 0) x0 = 0;
  if (x1 > SCR_W) x1 = SCR_W;
  if (x1 <= x0) return;
  for (int yy = y0; yy < y1; yy++) {
    uint16_t *p = g_fb + yy * SCR_W;
    for (int xx = x0; xx < x1; xx++) p[xx] = c;
  }
}
void gfx_fill_blend(int x, int y, int w, int h, uint16_t c, int a) {
  int y0 = y - g_sy0, y1 = y0 + h;
  if (y0 < 0) y0 = 0;
  if (y1 > STRIP_H) y1 = STRIP_H;
  int x0 = x, x1 = x + w;
  if (x0 < 0) x0 = 0;
  if (x1 > SCR_W) x1 = SCR_W;
  if (x1 <= x0) return;
  a = a + (a >> 7);
  for (int yy = y0; yy < y1; yy++) {
    uint16_t *p = g_fb + yy * SCR_W;
    for (int xx = x0; xx < x1; xx++) p[xx] = blend(p[xx], c, a);
  }
}
void gfx_hline(int x, int y, int w, uint16_t c) { gfx_fill(x, y, w, 1, c); }
void gfx_rect(int x, int y, int w, int h, uint16_t c) {
  gfx_fill(x, y, w, 1, c);
  gfx_fill(x, y + h - 1, w, 1, c);
  gfx_fill(x, y, 1, h, c);
  gfx_fill(x + w - 1, y, 1, h, c);
}

void gfx_rle_size(int id, int *w, int *h) {
  const uint8_t *p = asset(id);
  *w = rd16(p);
  *h = rd16(p + 2);
}

void gfx_rle(int id, int x, int y, int flags) {
  const uint8_t *p = asset(id);
  int w = rd16(p), h = rd16(p + 2);
  int ox = (int16_t)rd16(p + 4), oy = (int16_t)rd16(p + 6);
  p += 8;
  int mir = flags & DR_MIRROR;
  int left = mir ? x - (w - ox) : x - ox;
  int top = y - oy;
  if (top >= g_sy0 + STRIP_H || top + h <= g_sy0 || left >= SCR_W || left + w <= 0) return;
  for (int row = 0; row < h; row++) {
    int sy = top + row - g_sy0;
    int nseg = *p++;
    if (sy < 0 || sy >= STRIP_H) {
      // skip row data
      for (int s = 0; s < nseg; s++) {
        p++;
        int cnt = *p++;
        int bl = cnt & 0x80;
        cnt &= 0x7F;
        p += cnt * (bl ? 3 : 2);
      }
      if (sy >= STRIP_H) return;
      continue;
    }
    uint16_t *dst = g_fb + sy * SCR_W;
    int cx = 0;
    for (int s = 0; s < nseg; s++) {
      cx += *p++;
      int cnt = *p++;
      int bl = cnt & 0x80;
      cnt &= 0x7F;
      for (int i = 0; i < cnt; i++, cx++) {
        uint16_t c = rd16(p);
        int a = 256;
        if (bl) { a = p[2]; a += a >> 7; p += 3; } else p += 2;
        int dx = mir ? left + (w - 1 - cx) : left + cx;
        if ((unsigned)dx >= SCR_W) continue;
        if (flags & DR_SIL) { if (a > 100) dst[dx] = g_silcol; continue; }
        c = modc(c, flags);
        dst[dx] = (a >= 256) ? c : blend(dst[dx], c, a);
      }
    }
  }
}

void gfx_raw_rot(int id, int x, int y, float ang, float scale) {
  const uint8_t *p = asset(id);
  int w = rd16(p), h = rd16(p + 2);
  float ox = (int16_t)rd16(p + 4), oy = (int16_t)rd16(p + 6);
  const uint8_t *pix = p + 8;
  const uint8_t *alp = pix + w * h * 2;
  float c = cosf(ang), s = sinf(ang);
  float r = sqrtf((float)(w * w + h * h)) * scale;
  int y0 = (int)(y - r), y1 = (int)(y + r) + 1;
  int x0 = (int)(x - r), x1 = (int)(x + r) + 1;
  if (y0 < g_sy0) y0 = g_sy0;
  if (y1 > g_sy0 + STRIP_H) y1 = g_sy0 + STRIP_H;
  if (x0 < 0) x0 = 0;
  if (x1 > SCR_W) x1 = SCR_W;
  float inv = 1.0f / scale;
  for (int yy = y0; yy < y1; yy++) {
    uint16_t *dst = g_fb + (yy - g_sy0) * SCR_W;
    float dy = yy - y;
    for (int xx = x0; xx < x1; xx++) {
      float dx = xx - x;
      // inverse rotate
      float u = (dx * c + dy * s) * inv + ox;
      float v = (-dx * s + dy * c) * inv + oy;
      int iu = (int)u, iv = (int)v;
      if (u < 0 || v < 0 || iu >= w || iv >= h) continue;
      int a = alp[iv * w + iu];
      if (a < 10) continue;
      uint16_t col = rd16(pix + 2 * (iv * w + iu));
      dst[xx] = a > 245 ? col : blend(dst[xx], col, a + (a >> 7));
    }
  }
}

// ---------------------------------------------------------------- LZ4 + tiles
static int lz4_dec(const uint8_t *src, int srclen, uint8_t *dst, int dstcap) {
  const uint8_t *ip = src, *iend = src + srclen;
  uint8_t *op = dst, *oend = dst + dstcap;
  while (ip < iend) {
    int tok = *ip++;
    int lit = tok >> 4;
    if (lit == 15) { int b; do { b = *ip++; lit += b; } while (b == 255); }
    if (op + lit > oend) return -1;
    memcpy(op, ip, lit); op += lit; ip += lit;
    if (ip >= iend) break;
    int off = ip[0] | (ip[1] << 8); ip += 2;
    int ml = tok & 15;
    if (ml == 15) { int b; do { b = *ip++; ml += b; } while (b == 255); }
    ml += 4;
    const uint8_t *m = op - off;
    if (op + ml > oend) return -1;
    while (ml--) *op++ = *m++;
  }
  return (int)(op - dst);
}

#define TCACHE 16
static uint8_t tc_data[TCACHE][32 * 32];
static int32_t tc_key[TCACHE];
static uint32_t tc_age[TCACHE];
static uint32_t tc_clock;
static const uint8_t *tc_map;

static const uint8_t *get_tile(const uint8_t *m, int tidx) {
  if (tc_map != m) {
    for (int i = 0; i < TCACHE; i++) tc_key[i] = -1;
    tc_map = m;
  }
  tc_clock++;
  int best = 0;
  for (int i = 0; i < TCACHE; i++) {
    if (tc_key[i] == tidx) { tc_age[i] = tc_clock; return tc_data[i]; }
    if (tc_age[i] < tc_age[best]) best = i;
  }
  int tw = rd16(m), th = rd16(m + 2), nt = rd16(m + 4);
  int hdr = 8 + 512 + 2 * tw * th;
  hdr = (hdr + 3) & ~3;
  const uint32_t *offs = (const uint32_t *)(m + hdr);
  const uint8_t *data = m + hdr + 4 * (nt + 1);
  lz4_dec(data + offs[tidx], offs[tidx + 1] - offs[tidx], tc_data[best], 1024);
  tc_key[best] = tidx;
  tc_age[best] = tc_clock;
  return tc_data[best];
}

void gfx_tiles(int id, int mx0, int my0) {
  const uint8_t *m = asset(id);
  int tw = rd16(m), th = rd16(m + 2);
  const uint16_t *pal = (const uint16_t *)(m + 8);
  const uint16_t *tab = (const uint16_t *)(m + 8 + 512);
  int py0 = my0 + g_sy0, py1 = py0 + STRIP_H;   // map pixel rows covered by this strip
  int ty0 = py0 >> 5, ty1 = (py1 - 1) >> 5;
  int tx0 = mx0 >> 5, tx1 = (mx0 + SCR_W - 1) >> 5;
  if (py0 < 0) ty0 = py0 / 32 - 1;
  if (mx0 < 0) tx0 = mx0 / 32 - 1;
  for (int ty = ty0; ty <= ty1; ty++) {
    if (ty < 0 || ty >= th) continue;
    for (int tx = tx0; tx <= tx1; tx++) {
      if (tx < 0 || tx >= tw) continue;
      int t = tab[ty * tw + tx];
      if (t == 0xFFFF) continue;
      const uint8_t *td = get_tile(m, t);
      int sx = tx * 32 - mx0;
      int r0 = ty * 32 - py0, r1 = r0 + 32;
      int a = r0 < 0 ? -r0 : 0, b = r1 > STRIP_H ? 32 - (r1 - STRIP_H) : 32;
      int c0 = sx < 0 ? -sx : 0, c1 = sx + 32 > SCR_W ? SCR_W - sx : 32;
      for (int rr = a; rr < b; rr++) {
        const uint8_t *src = td + rr * 32;
        uint16_t *dst = g_fb + (r0 + rr) * SCR_W + sx;
        for (int cc = c0; cc < c1; cc++) {
          uint8_t v = src[cc];
          if (v) dst[cc] = pal[v];
        }
      }
    }
  }
}

void gfx_stars(int ox, int oy) {
  // pattern 150x150, ox/oy = screen position of a pattern origin
  int x0 = ox % 150; if (x0 > 0) x0 -= 150;
  int y0 = oy % 150; if (y0 > 0) y0 -= 150;
  for (int y = y0; y < SCR_H; y += 150) {
    if (y + 150 <= g_sy0 || y >= g_sy0 + STRIP_H) continue;
    for (int x = x0; x < SCR_W; x += 150) gfx_rle(A_STARS, x, y, 0);
  }
}

void gfx_light(int cx, int cy) {
  const uint8_t *prof = asset(A_LIGHT);
  const float iw2 = 1.0f / (LIGHT_W * LIGHT_W), ih2 = 1.0f / (LIGHT_H * LIGHT_H);
  for (int r = 0; r < STRIP_H; r++) {
    int yy = g_sy0 + r;
    float dy = (float)(yy - cy);
    float ry = dy * dy * ih2;
    uint16_t *row = g_fb + r * SCR_W;
    // find lit span where profile == 255 cheaply: compute per pixel otherwise
    for (int x = 0; x < SCR_W; x++) {
      float dx = (float)(x - cx);
      float r2 = dx * dx * iw2 + ry;
      int k;
      if (r2 >= 1.0f) k = prof[255];
      else k = prof[(int)(r2 * 255.0f)];
      if (k >= 255) continue;
      uint16_t c = row[x];
      uint32_t rr = ((c >> 11) * k) >> 8, gg = (((c >> 5) & 63) * k) >> 8, bb = ((c & 31) * k) >> 8;
      row[x] = (uint16_t)((rr << 11) | (gg << 5) | bb);
    }
  }
}

// ---------------------------------------------------------------- text
typedef struct __attribute__((packed)) { uint8_t adv, w, h; int8_t xo, yo; uint16_t off; } glyph_t;

int gfx_font_h(int font) { return asset(font)[0]; }

int gfx_text_w(int font, const char *s) {
  const uint8_t *f = asset(font);
  const glyph_t *g = (const glyph_t *)(f + 2);
  int w = 0;
  for (; *s; s++) {
    int c = (uint8_t)*s;
    if (c < 32 || c > 126) c = '?';
    w += g[c - 32].adv;
  }
  return w;
}

int gfx_text(int font, const char *s, int x, int y, uint16_t col, int align) {
  const uint8_t *f = asset(font);
  const glyph_t *g = (const glyph_t *)(f + 2);
  const uint8_t *data = f + 2 + 95 * sizeof(glyph_t);
  int w = gfx_text_w(font, s);
  if (align == 1) x -= w / 2;
  else if (align == 2) x -= w;
  if (y >= g_sy0 + STRIP_H || y + f[0] + 4 < g_sy0) return w;
  for (; *s; s++) {
    int c = (uint8_t)*s;
    if (c < 32 || c > 126) c = '?';
    const glyph_t *gl = &g[c - 32];
    const uint8_t *gd = data + gl->off;
    int bw = (gl->w + 1) / 2;
    for (int r = 0; r < gl->h; r++) {
      int sy = y + gl->yo + r - g_sy0;
      if (sy < 0 || sy >= STRIP_H) continue;
      uint16_t *dst = g_fb + sy * SCR_W;
      for (int cx = 0; cx < gl->w; cx++) {
        int v = gd[r * bw + (cx >> 1)];
        v = (cx & 1) ? (v >> 4) : (v & 15);
        if (!v) continue;
        int dx = x + gl->xo + cx;
        if ((unsigned)dx >= SCR_W) continue;
        dst[dx] = v == 15 ? col : blend(dst[dx], col, v * 17);
      }
    }
    x += gl->adv;
  }
  return w;
}

void gfx_text_ol(int font, const char *s, int x, int y, uint16_t col, uint16_t ol, int align) {
  gfx_text(font, s, x - 1, y, ol, align);
  gfx_text(font, s, x + 1, y, ol, align);
  gfx_text(font, s, x, y - 1, ol, align);
  gfx_text(font, s, x, y + 1, ol, align);
  gfx_text(font, s, x, y, col, align);
}

void gfx_present(void) {
  eadk_rect_t r = {0, (uint16_t)g_sy0, SCR_W, STRIP_H};
  eadk_display_push_rect(r, g_fb);
}
