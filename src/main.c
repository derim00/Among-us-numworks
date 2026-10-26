// Among Us SP - NumWorks port
// Credits: discord.gg/sti2d | discord : cjnw (yamii)
#include <eadk.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "gfx.h"
#include "assets.h"

const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Among Us";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;

#define S 0.25f
#define PI 3.14159265f
#define CREDIT1 "discord.gg/sti2d"
#define CREDIT2 "discord : cjnw (yamii)"

static inline float frand(void) { return (eadk_random() & 0xFFFFFF) / 16777216.0f; }
static inline int irand(int n) { return (int)(eadk_random() % (uint32_t)n); }
static inline uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline float rdf(const uint8_t *p) { float f; memcpy(&f, p, 4); return f; }
static void itoa10(int v, char *b) {
  char t[12]; int n = 0, neg = v < 0;
  if (neg) v = -v;
  do { t[n++] = '0' + v % 10; v /= 10; } while (v);
  int i = 0;
  if (neg) b[i++] = '-';
  while (n) b[i++] = t[--n];
  b[i] = 0;
}

// ------------------------------------------------------------------ input
static uint64_t k_now, k_prev;
static bool kdown(int k) { return (k_now >> k) & 1; }
static bool kpress(int k) { return ((k_now & ~k_prev) >> k) & 1; }
static void read_keys(void) { k_prev = k_now; k_now = eadk_keyboard_scan(); }

// ------------------------------------------------------------------ settings / session
static char g_name[13] = "Player";
static int g_color = 0, g_hat = 0, g_pet = 0, g_map = 0;
static int g_killdist = 200, g_killcd = 5, g_speed = 400, g_taskbar = 75;
static const char *g_crewname[9];
static int g_crewhat[9];
static bool g_crew_gone[9];     // killed or ejected in a previous round
static int g_playercount, g_progress, g_kills, g_sus;
static bool g_found;
static float g_length;
static bool g_victory;

static const char *name_at(int i) {
  const char *p = (const char *)asset(A_NAMES);
  while (i--) p += strlen(p) + 1;
  return p;
}
static void new_session(void) {
  for (int i = 0; i < 9; i++) {
    g_crewname[i] = name_at(irand(NNAMES));
    g_crewhat[i] = irand(14);
    g_crew_gone[i] = false;
  }
  g_playercount = 9; g_progress = 0; g_kills = 0; g_sus = 0; g_found = false; g_length = 0;
}

// ------------------------------------------------------------------ map data
typedef struct { const uint8_t *v; int n; float x0, y0, x1, y1, cx, cy; } obb_t;   // convex polygon in flash
typedef struct { int id; float x, y, hw, hh; } area_t;
typedef struct { int id; float x, y; } vent_t;
typedef struct { int target, spr; float x, y, ang; } arrow_t;
static obb_t m_cols[160]; static int m_ncols;
static area_t m_areas[40]; static int m_nareas;
static vent_t m_vents[12]; static int m_nvents;
static arrow_t m_arrows[20]; static int m_narrows;
static float m_ox, m_oy;                // world coords of map pixel (0,0)
static float m_spawn[11][2];            // player, crew 1..9, pet
static float m_cell; static int m_gw, m_gh; static const uint8_t *m_grid;

static void load_map(int mi) {
  const uint8_t *p = asset(MAPS[mi].data);
  m_ox = rdf(p); m_oy = rdf(p + 4); p += 8;
  m_ncols = rd16(p); p += 2;
  for (int i = 0; i < m_ncols; i++) {
    obb_t *o = &m_cols[i];
    o->n = rd16(p); p += 2;
    o->v = p;
    o->x0 = o->y0 = 1e30f; o->x1 = o->y1 = -1e30f; o->cx = o->cy = 0;
    for (int k = 0; k < o->n; k++) {
      float x = rdf(p + 8 * k), y = rdf(p + 8 * k + 4);
      if (x < o->x0) o->x0 = x;
      if (x > o->x1) o->x1 = x;
      if (y < o->y0) o->y0 = y;
      if (y > o->y1) o->y1 = y;
      o->cx += x; o->cy += y;
    }
    o->cx /= o->n; o->cy /= o->n;
    p += 8 * o->n;
  }
  m_nareas = rd16(p); p += 2;
  for (int i = 0; i < m_nareas; i++, p += 18) {
    m_areas[i].id = rd16(p); m_areas[i].x = rdf(p + 2); m_areas[i].y = rdf(p + 6); m_areas[i].hw = rdf(p + 10); m_areas[i].hh = rdf(p + 14);
  }
  m_nvents = rd16(p); p += 2;
  for (int i = 0; i < m_nvents; i++, p += 10) { m_vents[i].id = rd16(p); m_vents[i].x = rdf(p + 2); m_vents[i].y = rdf(p + 6); }
  m_narrows = rd16(p); p += 2;
  for (int i = 0; i < m_narrows; i++, p += 16) {
    m_arrows[i].target = rd16(p); m_arrows[i].spr = rd16(p + 2); m_arrows[i].x = rdf(p + 4); m_arrows[i].y = rdf(p + 8); m_arrows[i].ang = rdf(p + 12);
  }
  for (int i = 0; i < 11; i++, p += 8) { m_spawn[i][0] = rdf(p); m_spawn[i][1] = rdf(p + 4); }
  m_cell = rdf(p); m_gw = rd16(p + 4); m_gh = rd16(p + 6); m_grid = p + 8;
}
static const area_t *find_area(int id) {
  for (int i = 0; i < m_nareas; i++) if (m_areas[i].id == id) return &m_areas[i];
  return NULL;
}

// ------------------------------------------------------------------ collision / LOS
// player/crewmate foot box relative to origin (from the C3 collision polygon)
#define BOX_X0 (-32.5f)
#define BOX_X1 (33.5f)
#define BOX_Y0 (-7.5f)
#define BOX_Y1 (6.0f)
static inline void pvert(const obb_t *o, int k, float *x, float *y) {
  if (k >= o->n) k -= o->n;
  *x = rdf(o->v + 8 * k); *y = rdf(o->v + 8 * k + 4);
}
static bool box_hits(float x0, float y0, float x1, float y1) {
  float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f, hx = (x1 - x0) * 0.5f, hy = (y1 - y0) * 0.5f;
  for (int i = 0; i < m_ncols; i++) {
    obb_t *o = &m_cols[i];
    if (x1 <= o->x0 || x0 >= o->x1 || y1 <= o->y0 || y0 >= o->y1) continue;
    bool sep = false;
    for (int k = 0; k < o->n && !sep; k++) {
      float ax, ay, bx, by;
      pvert(o, k, &ax, &ay); pvert(o, k + 1, &bx, &by);
      float nx = -(by - ay), ny = bx - ax;
      if (nx == 0 && ny == 0) continue;
      float mn = 1e30f, mx = -1e30f;
      for (int j = 0; j < o->n; j++) {
        float px, py; pvert(o, j, &px, &py);
        float d = px * nx + py * ny;
        if (d < mn) mn = d;
        if (d > mx) mx = d;
      }
      float c = cx * nx + cy * ny, r = hx * fabsf(nx) + hy * fabsf(ny);
      if (c + r <= mn || c - r >= mx) sep = true;
    }
    if (!sep) return true;
  }
  return false;
}
static bool seg_hits_obb(const obb_t *o, float ax, float ay, float bx, float by) {
  // Cyrus-Beck clipping against the convex polygon
  float t0 = 0, t1 = 1, dx = bx - ax, dy = by - ay;
  for (int k = 0; k < o->n; k++) {
    float px, py, qx, qy;
    pvert(o, k, &px, &py); pvert(o, k + 1, &qx, &qy);
    float nx = -(qy - py), ny = qx - px;
    if (nx == 0 && ny == 0) continue;
    // outward normal: away from centroid
    if ((px - o->cx) * nx + (py - o->cy) * ny < 0) { nx = -nx; ny = -ny; }
    float num = (px - ax) * nx + (py - ay) * ny;   // >0 means a inside wrt this edge
    float den = dx * nx + dy * ny;
    if (den == 0) { if (num < 0) return false; continue; }
    float t = num / den;
    if (den > 0) { if (t < t1) t1 = t; } else { if (t > t0) t0 = t; }
    if (t0 > t1) return false;
  }
  return true;
}
static bool los(float ax, float ay, float bx, float by, float range) {
  float dx = bx - ax, dy = by - ay;
  if (dx * dx + dy * dy > range * range) return false;
  float x0 = fminf(ax, bx), x1 = fmaxf(ax, bx), y0 = fminf(ay, by), y1 = fmaxf(ay, by);
  for (int i = 0; i < m_ncols; i++) {
    obb_t *o = &m_cols[i];
    if (x1 < o->x0 || x0 > o->x1 || y1 < o->y0 || y0 > o->y1) continue;
    if (seg_hits_obb(o, ax, ay, bx, by)) return false;
  }
  return true;
}

// ------------------------------------------------------------------ pathfinding (grid BFS)
static uint8_t nav_par[(149 * 125 + 1) / 2 + 16];
static uint16_t nav_q[2048];
static const int8_t DX8[8] = {1, -1, 0, 0, 1, 1, -1, -1}, DY8[8] = {0, 0, 1, -1, 1, -1, 1, -1};
static const uint8_t OPP8[8] = {1, 0, 3, 2, 7, 6, 5, 4};
static inline bool g_blk(int x, int y) {
  if (x < 0 || y < 0 || x >= m_gw || y >= m_gh) return true;
  int i = y * m_gw + x;
  return (m_grid[i >> 3] >> (i & 7)) & 1;
}
static inline int par_get(int i) { return (nav_par[i >> 1] >> ((i & 1) * 4)) & 15; }
static inline void par_set(int i, int v) {
  uint8_t *b = &nav_par[i >> 1];
  if (i & 1) *b = (*b & 0x0F) | (v << 4); else *b = (*b & 0xF0) | v;
}
static bool free_near(int *x, int *y) {
  if (!g_blk(*x, *y)) return true;
  for (int r = 1; r < 5; r++)
    for (int dy = -r; dy <= r; dy++)
      for (int dx = -r; dx <= r; dx++)
        if (!g_blk(*x + dx, *y + dy)) { *x += dx; *y += dy; return true; }
  return false;
}
static bool grid_los(int ax, int ay, int bx, int by) {
  float dx = bx - ax, dy = by - ay;
  int n = (int)(fmaxf(fabsf(dx), fabsf(dy)) * 3) + 1;
  for (int i = 1; i <= n; i++) {
    float t = (float)i / n;
    float x = ax + 0.5f + dx * t, y = ay + 0.5f + dy * t;
    // check a small cross for thickness
    if (g_blk((int)x, (int)y) || g_blk((int)(x + 0.3f), (int)y) || g_blk((int)(x - 0.3f), (int)y) ||
        g_blk((int)x, (int)(y + 0.3f)) || g_blk((int)x, (int)(y - 0.3f)))
      return false;
  }
  return true;
}
#define MAXPATH 40
typedef struct { int16_t x, y; } pt16_t;
// returns number of waypoints written to out (world coords)
static int find_path(float sxw, float syw, float txw, float tyw, pt16_t *out) {
  int sx = (int)(sxw / m_cell), sy = (int)(syw / m_cell), tx = (int)(txw / m_cell), ty = (int)(tyw / m_cell);
  if (!free_near(&sx, &sy) || !free_near(&tx, &ty)) return 0;
  int N = m_gw * m_gh;
  memset(nav_par, 0xFF, (N + 1) / 2);
  int qh = 0, qt = 0;
  int ti = ty * m_gw + tx, si = sy * m_gw + sx;
  par_set(ti, 8);
  nav_q[qt++] = ti;
  bool found = (si == ti);
  while (qh != qt && !found) {
    int c = nav_q[qh]; qh = (qh + 1) & 2047;
    int cx = c % m_gw, cy = c / m_gw;
    for (int d = 0; d < 8; d++) {
      int nx = cx + DX8[d], ny = cy + DY8[d];
      if (g_blk(nx, ny)) continue;
      if (d >= 4 && (g_blk(cx + DX8[d], cy) || g_blk(cx, cy + DY8[d]))) continue;
      int ni = ny * m_gw + nx;
      if (par_get(ni) != 15) continue;
      par_set(ni, OPP8[d]);
      if (ni == si) { found = true; break; }
      int nq = (qt + 1) & 2047;
      if (nq == qh) return 0;
      nav_q[qt] = ni; qt = nq;
    }
  }
  if (!found) return 0;
  // follow parents from start, string-pull
  int n = 0;
  int ax = sx, ay = sy, px = sx, py = sy, cx = sx, cy = sy;
  int guard = 0;
  while (!(cx == tx && cy == ty) && guard++ < 2000) {
    int d = par_get(cy * m_gw + cx);
    if (d > 7) break;
    cx += DX8[d]; cy += DY8[d];
    if (!grid_los(ax, ay, cx, cy)) {
      if (n < MAXPATH - 1) { out[n].x = (int16_t)((px + 0.5f) * m_cell); out[n].y = (int16_t)((py + 0.5f) * m_cell); n++; }
      ax = px; ay = py;
    }
    px = cx; py = cy;
  }
  out[n].x = (int16_t)txw; out[n].y = (int16_t)tyw; n++;
  return n;
}

// ------------------------------------------------------------------ game objects
typedef struct {
  bool present, dead, fys, moving, report_pending;
  float x, y, speed, animt, diet, wander, report_t;
  int mirror;
  pt16_t path[MAXPATH]; int npath, ipath;
  int goal;            // 0 wander, 1 button, 2 reactor, 3 lights
} crew_t;
static crew_t crew[9];

static struct {
  float x, y, vx, vy, animt;
  int mirror, invent, anim; // anim: 0 idle 1 walk 2 goinginvent
  float vent_anim_t; int vent_open;
  float petx, pety, pvx, pvy; bool pet_moving; int pet_mirror;
} pl;

enum { SC_SPLASH, SC_MENU, SC_SETTINGS, SC_CUSTOM, SC_GAME, SC_VOTE, SC_END };
static int g_screen = SC_SPLASH;
static float g_camx, g_camy;
static float g_killcool, g_sabcool, g_tum, g_meltt, g_sec_acc, g_prog_t, g_prog_next;
static bool g_melt, g_lights;
static float g_fix_t; static int g_fix_kind;
static int g_sabmenu, g_sabsel;
static int g_report;  float g_report_t;   // 0 none 1 body 2 meeting
static bool g_paused;
static int g_target = -1;   // crewmate highlighted for kill
static bool g_body_visible;  // report button visible
static float g_time;

static void crew_path(int i, float x, float y, int goal) {
  crew_t *c = &crew[i];
  c->npath = find_path(c->x, c->y, x, y, c->path);
  c->ipath = 0;
  c->moving = c->npath > 0;
  c->goal = goal;
  c->speed = 0;
}
static void start_round(void) {
  load_map(g_map);
  pl.x = m_spawn[0][0]; pl.y = m_spawn[0][1];
  pl.vx = pl.vy = 0; pl.invent = 0; pl.anim = 0; pl.mirror = 0; pl.vent_anim_t = 0;
  pl.petx = m_spawn[10][0]; pl.pety = m_spawn[10][1]; pl.pvx = pl.pvy = 0;
  for (int i = 0; i < 9; i++) {
    crew_t *c = &crew[i];
    memset(c, 0, sizeof(*c));
    c->present = !g_crew_gone[i];
    c->x = m_spawn[1 + i][0]; c->y = m_spawn[1 + i][1];
    c->wander = 1 + irand(4);
  }
  g_camx = pl.x; g_camy = pl.y - 40;
  g_killcool = g_killcd; g_sabcool = g_killcd + 2.5f;
  g_melt = false; g_lights = false; g_tum = 25; g_meltt = 0;
  g_fix_t = 0; g_sabmenu = 0; g_report = 0; g_paused = false;
  g_prog_t = 0; g_prog_next = 5 + irand(5);
  g_sec_acc = 0;
  g_screen = SC_GAME;
}

static void menu_init(void);
static void go_end(bool victory) { g_victory = victory; g_screen = SC_END; g_time = 0; }

// vote screen state
static int v_kind; // 0 nobody, 1 crewmate k, 2 player
static int v_crew; static float v_t, v_x, v_ang; static char v_text[40];

static void begin_vote(void) {
  int vote = g_found ? 10 : 1 + irand(10);
  v_t = 0; v_x = 4; v_ang = 0;
  if (vote == 10) {
    v_kind = 2;
    strcpy(v_text, g_name); strcat(v_text, " was ejected");
  } else {
    int k = vote - 1;
    if (crew[k].present && !crew[k].dead) {
      v_kind = 1; v_crew = k;
      g_crew_gone[k] = true; g_playercount--;
      strcpy(v_text, g_crewname[k]); strcat(v_text, " was ejected");
    } else {
      v_kind = 0; strcpy(v_text, "No one was ejected");
    }
  }
  // bodies of this round are removed for the next round
  for (int i = 0; i < 9; i++) if (crew[i].present && crew[i].dead) g_crew_gone[i] = true;
  g_screen = SC_VOTE;
}

static void trigger_report(int kind, bool found) {
  if (g_report) return;
  g_report = kind; g_report_t = 0;
  if (found) g_found = true;
}

static void kill_crew(int i) {
  crew_t *c = &crew[i];
  c->dead = true; c->moving = false; c->diet = 0;
  g_playercount--;
}

// ------------------------------------------------------------------ game update
static void update_game(float dt) {
  if (g_report) {
    g_report_t += dt;
    if (g_report_t >= (g_report == 1 ? 3.0f : 1.0f)) begin_vote();
    return;
  }
  if (g_paused) {
    if (kpress(eadk_key_ok) || kpress(eadk_key_exe) || kpress(eadk_key_back)) g_paused = false;
    if (kpress(eadk_key_backspace)) { g_screen = SC_MENU; menu_init(); }
    return;
  }
  if (kpress(eadk_key_back)) { g_paused = true; return; }
  g_length += dt;
  // ---- one second ticks
  g_sec_acc += dt;
  while (g_sec_acc >= 1.0f) {
    g_sec_acc -= 1.0f;
    if (g_killcool > 0) g_killcool -= 1;
    if (g_sabcool > 0 && !g_melt && !g_lights) g_sabcool -= 1;
    if (g_sabcool < 0) g_sabcool = 0;
    if (g_melt) g_tum -= 1;
  }
  if (g_melt) {
    g_meltt += dt;
    if (g_meltt >= 24.99f) { go_end(true); return; }
  }
  g_prog_t += dt;
  if (g_prog_t >= g_prog_next) { g_prog_t = 0; g_prog_next = 5 + irand(5); g_progress += irand(10); }
  if (g_progress >= g_taskbar) { go_end(false); return; }
  if (g_playercount <= 1) { go_end(true); return; }

  // ---- sabotage menu
  if (kpress(eadk_key_nine) || kpress(eadk_key_alpha)) { g_sabmenu = !g_sabmenu; }
  if (g_sabmenu) {
    if (kpress(eadk_key_left) || kpress(eadk_key_one)) g_sabsel = 0;
    if (kpress(eadk_key_right) || kpress(eadk_key_two)) g_sabsel = 1;
    bool go = kpress(eadk_key_ok) || kpress(eadk_key_exe) || kpress(eadk_key_one) || kpress(eadk_key_two);
    if (go && g_sabcool <= 0 && !g_melt && !g_lights) {
      g_sabcool = g_killcd + 2.5f;
      if (g_sabsel == 0) {
        g_melt = true; g_meltt = 0; g_tum = 25;
        const area_t *a = find_area(7);
        if (a) for (int i = 0; i < 9; i++) if (crew[i].present && !crew[i].dead) crew_path(i, a->x, a->y, 2);
      } else {
        g_lights = true;
        const area_t *a = find_area(15);
        if (a) for (int i = 0; i < 9; i++) if (crew[i].present && !crew[i].dead) crew_path(i, a->x, a->y, 3);
      }
    }
    if (kpress(eadk_key_back)) g_sabmenu = 0;
  }

  // ---- player movement (8 direction)
  bool canmove = pl.invent == 0 && !g_sabmenu;
  float ix = 0, iy = 0;
  if (canmove) {
    if (kdown(eadk_key_right)) { ix += 1; pl.mirror = 1; }
    if (kdown(eadk_key_left)) { ix -= 1; pl.mirror = 0; }
    if (kdown(eadk_key_up)) iy -= 1;
    if (kdown(eadk_key_down)) iy += 1;
  }
  float maxs = (float)g_speed, acc = maxs + 500, dec = 1400;
  float *vv[2] = {&pl.vx, &pl.vy}; float in[2] = {ix, iy};
  for (int a = 0; a < 2; a++) {
    float *v = vv[a];
    if (in[a] != 0) {
      if (*v * in[a] < 0) *v += in[a] * (acc + dec) * dt; else *v += in[a] * acc * dt;
    } else {
      if (*v > 0) { *v -= dec * dt; if (*v < 0) *v = 0; }
      else if (*v < 0) { *v += dec * dt; if (*v > 0) *v = 0; }
    }
  }
  float sp = sqrtf(pl.vx * pl.vx + pl.vy * pl.vy);
  if (sp > maxs) { pl.vx *= maxs / sp; pl.vy *= maxs / sp; }
  if (pl.invent == 0) {
    float nx = pl.x + pl.vx * dt;
    if (!box_hits(nx + BOX_X0, pl.y + BOX_Y0, nx + BOX_X1, pl.y + BOX_Y1)) pl.x = nx; else pl.vx = 0;
    float ny = pl.y + pl.vy * dt;
    if (!box_hits(pl.x + BOX_X0, ny + BOX_Y0, pl.x + BOX_X1, ny + BOX_Y1)) pl.y = ny; else pl.vy = 0;
  }
  bool pmoving = (pl.vx != 0 || pl.vy != 0) && pl.invent == 0;
  if (pl.anim != 2) pl.anim = pmoving ? 1 : 0;
  pl.animt += dt;
  if (pl.vent_anim_t > 0) {
    pl.vent_anim_t -= dt;
    if (pl.vent_anim_t <= 0) { pl.vent_open = -1; pl.anim = 0; }
  }

  // ---- vents
  int near_vent = -1;
  for (int i = 0; i < m_nvents; i++) {
    float dx = pl.x - m_vents[i].x, dy = pl.y - m_vents[i].y;
    if (dx * dx + dy * dy < 75 * 75) near_vent = i;
  }
  if ((kpress(eadk_key_five) || kpress(eadk_key_shift)) && near_vent >= 0) {
    pl.invent++;
    if (pl.invent > 1) pl.invent = 0;
    pl.x = m_vents[near_vent].x; pl.y = m_vents[near_vent].y;
    pl.vx = pl.vy = 0;
    pl.anim = 2; pl.vent_open = near_vent; pl.vent_anim_t = 0.2f;
  }
  if (pl.invent == 1) {
    int dir = -1;
    if (kpress(eadk_key_right)) dir = 0;
    if (kpress(eadk_key_down)) dir = 1;
    if (kpress(eadk_key_left)) dir = 2;
    if (kpress(eadk_key_up)) dir = 3;
    if (dir >= 0) {
      float wx = dir == 0 ? 1 : dir == 2 ? -1 : 0, wy = dir == 1 ? 1 : dir == 3 ? -1 : 0;
      int best = -1; float bs = 0.3f;
      for (int i = 0; i < m_narrows; i++) {
        float dx = m_arrows[i].x - pl.x, dy = m_arrows[i].y - pl.y;
        if (dx * dx + dy * dy > 160 * 160) continue;
        float sc = cosf(m_arrows[i].ang) * wx + sinf(m_arrows[i].ang) * wy;
        if (sc > bs) { bs = sc; best = i; }
      }
      if (best >= 0) {
        for (int v = 0; v < m_nvents; v++) if (m_vents[v].id == m_arrows[best].target) {
          pl.x = m_vents[v].x; pl.y = m_vents[v].y;
          pl.petx = pl.x; pl.pety = pl.y;
          break;
        }
      }
    }
  }

  // ---- pet (MoveTo the "pet" image point when far)
  if (g_pet) {
    float tx = pl.x + (pl.mirror ? 1 : -1) * (0.52f - 0.192f) * 125, ty = pl.y + (0.888f - 0.848f) * 125;
    float dx = pl.x - pl.petx, dy = pl.y - pl.pety;
    float d = sqrtf(dx * dx + dy * dy);
    if (d > 500) { pl.petx = tx; pl.pety = ty; }
    else if (d > 75) {
      float ddx = tx - pl.petx, ddy = ty - pl.pety, dd = sqrtf(ddx * ddx + ddy * ddy);
      float step = 400 * dt;
      if (dd > step) { pl.petx += ddx / dd * step; pl.pety += ddy / dd * step; } else { pl.petx = tx; pl.pety = ty; }
      pl.pet_moving = true;
    } else pl.pet_moving = pmoving;
    pl.pet_mirror = pl.mirror;
  }

  // ---- crewmates
  float crange = g_lights ? 30 : 500;
  for (int i = 0; i < 9; i++) {
    crew_t *c = &crew[i];
    if (!c->present) continue;
    if (c->dead) { c->diet += dt; continue; }
    // wander
    c->wander -= dt;
    if (c->wander <= 0) {
      c->wander = 1 + irand(4);
      if (!c->moving && !g_melt && !g_lights && !c->fys) {
        const area_t *a = &m_areas[irand(m_nareas)];
        crew_path(i, a->x, a->y, 0);
      }
    }
    // follow path
    bool was_moving = c->moving;
    if (c->moving) {
      float maxv = (float)g_speed;
      c->speed += (maxv + 500) * dt;
      if (c->speed > maxv) c->speed = maxv;
      float step = c->speed * dt;
      while (step > 0 && c->moving) {
        float tx = c->path[c->ipath].x, ty = c->path[c->ipath].y;
        float dx = tx - c->x, dy = ty - c->y, d = sqrtf(dx * dx + dy * dy);
        if (d > 0.5f) {
          float ang = atan2f(dy, dx) * 180 / PI;
          if (fabsf(fabsf(ang) - 180) <= 60) c->mirror = 0;
          else if (fabsf(ang) <= 60) c->mirror = 1;
        }
        if (d <= step) {
          c->x = tx; c->y = ty; step -= d;
          if (++c->ipath >= c->npath) c->moving = false;
        } else {
          c->x += dx / d * step; c->y += dy / d * step; step = 0;
        }
      }
    }
    c->animt += dt;
    // arrived at emergency button -> meeting
    if (was_moving && !c->moving && c->fys && c->goal == 1 && !g_melt && !g_lights) {
      const area_t *b = find_area(0);
      if (b && fabsf(c->x - b->x) < b->hw + 33 && fabsf(c->y - b->y) < b->hh + 7) trigger_report(2, true);
    }
    // saw the player going in/out of a vent
    if (!c->fys && pl.anim == 2 && los(c->x, c->y, pl.x, pl.y, crange)) {
      c->fys = true;
      const area_t *b = find_area(0);
      if (b) crew_path(i, b->x, b->y, 1);
    }
    // sees a body
    if (!c->report_pending) {
      for (int j = 0; j < 9; j++) {
        if (j == i || !crew[j].present || !crew[j].dead) continue;
        if (los(c->x, c->y, crew[j].x, crew[j].y, crange)) { c->report_pending = true; c->report_t = 0.5f; break; }
      }
    } else {
      c->report_t -= dt;
      if (c->report_t <= 0) {
        bool seen = pl.invent == 0 && los(c->x, c->y, pl.x, pl.y, crange);
        trigger_report(1, seen);
        return;
      }
    }
    // fixing sabotages
    if ((g_melt || g_lights) && g_fix_t <= 0) {
      const area_t *a = find_area(g_melt ? 7 : 15);
      if (a && fabsf(c->x - a->x) < a->hw + 33 && fabsf(c->y - a->y) < a->hh + 7) { g_fix_t = 0.5f; g_fix_kind = g_melt ? 1 : 2; }
    }
  }
  if (g_fix_t > 0) {
    g_fix_t -= dt;
    if (g_fix_t <= 0) {
      if (g_fix_kind == 1) { g_melt = false; g_tum = 25; g_meltt = 0; }
      else g_lights = false;
      g_sabcool = g_killcd + 5;
      for (int i = 0; i < 9; i++) if (crew[i].present && !crew[i].dead && (crew[i].goal == 2 || crew[i].goal == 3)) { crew[i].moving = false; crew[i].goal = 0; }
    }
  }

  // ---- kill target (nearest alive crewmate)
  g_target = -1;
  float bd = 1e30f; int nearest = -1;
  for (int i = 0; i < 9; i++) {
    crew_t *c = &crew[i];
    if (!c->present || c->dead) continue;
    float dx = c->x - pl.x, dy = c->y - pl.y, d = dx * dx + dy * dy;
    if (d < bd) { bd = d; nearest = i; }
  }
  if (nearest >= 0 && pl.invent == 0 && los(pl.x, pl.y, crew[nearest].x, crew[nearest].y, (float)g_killdist)) g_target = nearest;
  if ((kpress(eadk_key_ok) || kpress(eadk_key_six)) && !g_sabmenu && g_target >= 0 && g_killcool <= 0 && pl.invent == 0) {
    crew_t *c = &crew[g_target];
    pl.x = c->x; pl.y = c->y;
    kill_crew(g_target);
    g_killcool = g_killcd;
    g_kills++;
  }
  // ---- self report
  g_body_visible = false;
  for (int i = 0; i < 9; i++)
    if (crew[i].present && crew[i].dead && los(pl.x, pl.y, crew[i].x, crew[i].y, (float)g_killdist)) g_body_visible = true;
  if (g_body_visible && pl.invent == 0 && (kpress(eadk_key_eight) || kpress(eadk_key_exe))) {
    if (g_sus < 1) { g_sus++; trigger_report(1, false); }
    else { g_sus++; trigger_report(1, true); }
  }

  // ---- camera (lerp 0.09 per 60Hz tick)
  float k = 1 - powf(1 - 0.09f, dt * 60);
  g_camx += (pl.x - g_camx) * k;
  g_camy += (pl.y - 40 - g_camy) * k;
}

// ------------------------------------------------------------------ game render
#define WX(x) ((int)lrintf(((x) - g_camx) * S) + 160)
#define WY(y) ((int)lrintf(((y) - g_camy) * S) + 120)

static int skin_frame(int skin, int f) { return A_SKIN0 + skin * SKIN_NFRAMES + f; }

typedef struct { float y; int8_t kind, idx; } drawitem_t;

static void draw_char(int skin, float x, float y, int frame, int mirror, int flags) {
  gfx_rle(skin_frame(skin, frame), WX(x), WY(y), (mirror ? DR_MIRROR : 0) | flags);
}
static void draw_hat_at(int hatspr, float x, float y, int frame, int mirror) {
  float hx = HATPTS[frame][0], hy = HATPTS[frame][1];
  if (mirror) hx = -hx;
  gfx_rle(hatspr, WX(x + hx), WY(y + hy), mirror ? DR_MIRROR : 0);
}

static void render_game(void) {
  // background: space + baked map
  gfx_fill(0, g_sy0, SCR_W, STRIP_H, 0);
  int mx0 = (int)lrintf((g_camx - m_ox) * S) - 160, my0 = (int)lrintf((g_camy - m_oy) * S) - 120;
  gfx_stars(WX(-482), WY(-490));
  gfx_tiles(MAPS[g_map].tiles, mx0, my0);
  // vents
  int near_vent = -1;
  for (int i = 0; i < m_nvents; i++) {
    float dx = pl.x - m_vents[i].x, dy = pl.y - m_vents[i].y;
    if (fabsf(dx) < 60 && fabsf(dy) < 30) near_vent = i;
  }
  for (int i = 0; i < m_nvents; i++) {
    int st = (pl.vent_open == i && pl.vent_anim_t > 0) ? 2 : (near_vent == i && pl.invent == 0) ? 1 : 0;
    gfx_rle(A_VENT0 + st, WX(m_vents[i].x), WY(m_vents[i].y), 0);
  }
  // y-sorted characters
  drawitem_t items[12]; int n = 0;
  for (int i = 0; i < 9; i++) if (crew[i].present) { items[n].y = crew[i].y; items[n].kind = 1; items[n].idx = i; n++; }
  if (pl.invent == 0) { items[n].y = pl.y; items[n].kind = 0; items[n].idx = 0; n++; }
  if (g_pet && pl.invent == 0) { items[n].y = pl.pety; items[n].kind = 2; items[n].idx = 0; n++; }
  for (int a = 1; a < n; a++) { drawitem_t t = items[a]; int b = a - 1; while (b >= 0 && items[b].y > t.y) { items[b + 1] = items[b]; b--; } items[b + 1] = t; }
  for (int k = 0; k < n; k++) {
    drawitem_t *it = &items[k];
    if (it->kind == 1) {
      crew_t *c = &crew[it->idx];
      int f;
      if (c->dead) { int di = (int)(c->diet * 5); if (di > 5) di = 5; f = DIE_SEQ[di]; }
      else f = c->moving ? WALK_SEQ[(int)(c->animt * 12) & 7] : F_IDLE;
      if (it->idx == g_target && !c->dead) {
        g_silcol = RGB(255, 0, 0);
        draw_char(it->idx, c->x - 4, c->y, f, c->mirror, DR_SIL);
        draw_char(it->idx, c->x + 4, c->y, f, c->mirror, DR_SIL);
        draw_char(it->idx, c->x, c->y - 4, f, c->mirror, DR_SIL);
        draw_char(it->idx, c->x, c->y + 4, f, c->mirror, DR_SIL);
      }
      draw_char(it->idx, c->x, c->y, f, c->mirror, 0);
      if (!c->dead) {
        int hf = CREWHAT_MAP[g_crewhat[it->idx]];
        if (hf) gfx_rle(A_HAT0 + hf, WX(c->x), WY(c->y - 86), 0);
      }
    } else if (it->kind == 0) {
      int f = pl.anim == 2 ? F_VENT : pl.anim == 1 ? WALK_SEQ[(int)(pl.animt * 12) & 7] : F_IDLE;
      draw_char(9 + g_color, pl.x, pl.y, f, pl.mirror, 0);
      if (g_hat) draw_hat_at(g_hat == 8 ? A_HAT8C0 + g_color : A_HAT0 + g_hat, pl.x, pl.y, f, pl.mirror);
    } else {
      int pf = g_pet - 1;
      if (g_pet == 3 && pl.pet_moving) pf = 4 + ((int)(pl.animt * 10) & 1);
      gfx_rle(A_PET0 + pf, WX(pl.petx), WY(pl.pety), pl.pet_mirror ? DR_MIRROR : 0);
    }
  }
  // names
  for (int i = 0; i < 9; i++) {
    crew_t *c = &crew[i];
    if (!c->present || c->dead) continue;
    gfx_text_ol(A_FONT_V8, g_crewname[i], WX(c->x), WY(c->y - 120) - 5, 0xFFFF, 0, 1);
  }
  if (pl.invent == 0) gfx_text_ol(A_FONT_V8, g_name, WX(pl.x), WY(pl.y - (g_hat ? 150 : 120)) - 5, 0xFFFF, RGB(120, 0, 0), 1);
  // vent arrows
  if (pl.invent == 1)
    for (int i = 0; i < m_narrows; i++) gfx_rle(m_arrows[i].spr, WX(m_arrows[i].x), WY(m_arrows[i].y), 0);
  // light
  gfx_light(WX(pl.x), WY(pl.y - 40));
  // ---- HUD
  gfx_rle(A_PROGFRAME, (int)(797.5f * S) + 0, (int)(-34.2f * S), 0);
  {
    int w = (int)(184 * (g_progress > g_taskbar ? 1.0f : (float)g_progress / g_taskbar));
    gfx_fill(5, 5, w, 6, RGB(0, 255, 0));
  }
  gfx_text_ol(A_FONT_J12, "TASKS COMPLETED", 66, 13, 0xFFFF, 0, 0);
  char buf[48];
  if (g_melt) {
    strcpy(buf, "Time until Meltdown: "); itoa10((int)g_tum, buf + strlen(buf));
    gfx_text_ol(A_FONT_J12, buf, 7, 27, 0xFFFF, 0, 0);
  } else if (g_lights) gfx_text_ol(A_FONT_J12, "Lights are down", 7, 27, 0xFFFF, 0, 0);
  int ventflag = near_vent >= 0 || pl.invent ? 0 : DR_DIM;
  gfx_rle(A_SABBTN, 289, 180, 0);
  if (g_body_visible) gfx_rle(A_REPORTBTN, 254, 180, 0);
  gfx_rle(A_VENTBTN, 253, 216, ventflag);
  gfx_rle(A_KILL, 290, 215, g_killcool > 0 ? DR_DIM : 0);
  if (g_killcool > 0) { itoa10((int)g_killcool, buf); gfx_text(A_FONT_J28, buf, 290, 200, 0xFFFF, 1); }
  // key hints
  uint16_t hc = RGB(220, 220, 220);
  gfx_text_ol(A_FONT_V8, "9", 276, 189, hc, 0, 0);
  if (g_body_visible) gfx_text_ol(A_FONT_V8, "8", 241, 189, hc, 0, 0);
  gfx_text_ol(A_FONT_V8, "5", 240, 227, hc, 0, 0);
  gfx_text_ol(A_FONT_V8, "OK", 276, 227, hc, 0, 0);
  // meltdown flash
  if (g_melt && (((int)g_meltt) & 1) == 0) gfx_fill_blend(0, 0, SCR_W, SCR_H, RGB(255, 0, 18), 51);
  // sabotage map
  if (g_sabmenu) {
    const map_desc_t *md = &MAPS[g_map];
    gfx_rle(md->sab, (int)(md->sx * S), (int)(md->sy * S) + 30, 0);
    int fl = g_sabcool > 0 || g_melt || g_lights ? DR_DIM : 0;
    gfx_rle(A_REACTOR, (int)(md->rx * S), (int)(md->ry * S) + 30, fl | (g_sabsel == 0 ? DR_GREEN : 0));
    gfx_rle(A_LIGHTSBTN, (int)(md->lx * S), (int)(md->ly * S) + 30, fl | (g_sabsel == 1 ? DR_GREEN : 0));
    if (g_sabcool > 0 && !g_melt && !g_lights) {
      itoa10((int)ceilf(g_sabcool), buf);
      gfx_text_ol(A_FONT_J18, buf, 160, 205, 0xFFFF, 0, 1);
    }
    gfx_text_ol(A_FONT_V8, "<- -> : choisir   OK : saboter   9 : fermer", 160, 228, 0xFFFF, 0, 1);
  }
  if (pl.invent == 1) gfx_text_ol(A_FONT_V8, "Fleches : changer de vent   5 : sortir", 160, 228, 0xFFFF, 0, 1);
  // report overlay
  if (g_report) {
    gfx_rle(A_BV, 625 / 4, 323 / 4 + 30, 0);
    gfx_rle(g_report == 1 ? A_BVBODY : A_BVMEETING, 622 / 4, 331 / 4 + 30, 0);
  }
  if (g_paused) {
    gfx_fill_blend(0, 0, SCR_W, SCR_H, 0, 140);
    gfx_rle(A_NP_PAUSE, 72, 68, 0);
    gfx_text(A_FONT_J18, "PAUSE", 160, 76, 0xFFFF, 1);
    gfx_text(A_FONT_V8, "OK : reprendre", 160, 104, 0xFFFF, 1);
    gfx_text(A_FONT_V8, "Effacer (<x]) : menu principal", 160, 118, 0xFFFF, 1);
    gfx_text(A_FONT_V8, "Fleches: bouger  OK/6: tuer  5: vent", 160, 136, RGB(200, 200, 200), 1);
    gfx_text(A_FONT_V8, "9: sabotage  8/EXE: report", 160, 148, RGB(200, 200, 200), 1);
  }
}

// ------------------------------------------------------------------ menus
#define UX(x) ((int)((x) * 0.25f))
#define UY(y) ((int)((y) * 0.25f) + 30)

static float mm_t; static bool mm_about; static int mm_sel;
typedef struct { float x, y, phase; float vx, vy; int anim; float ang; } msc_t;
static msc_t msc[4];
static void menu_init(void) {
  static const float px[4] = {8, 532, 1047, 1352}, py[4] = {-47, 775, -61, 469};
  static const float sx[4] = {1, 1, -1, -1}, sy[4] = {1, -1, 1, 0};
  for (int i = 0; i < 4; i++) {
    msc[i].x = px[i]; msc[i].y = py[i]; msc[i].vx = 70 * sx[i]; msc[i].vy = 70 * sy[i];
    msc[i].anim = irand(3); msc[i].ang = i & 1 ? 2.56f : 0.36f;
  }
}
static void update_menu(float dt) {
  mm_t += dt;
  for (int i = 0; i < 4; i++) {
    msc_t *m = &msc[i];
    m->x += m->vx * dt; m->y += m->vy * dt; m->ang += 0.27f * dt;
    if (m->x < -200 || m->x > 1480 || m->y < -200 || m->y > 920) {
      static const float px[4] = {8, 532, 1047, 1352}, py[4] = {-47, 775, -61, 469};
      m->x = px[i]; m->y = py[i]; m->anim = irand(3);
    }
  }
  if (mm_about) {
    if (kpress(eadk_key_ok) || kpress(eadk_key_back) || kpress(eadk_key_exe)) mm_about = false;
    return;
  }
  if (kpress(eadk_key_down)) mm_sel = 1;
  if (kpress(eadk_key_up)) mm_sel = 0;
  if (kpress(eadk_key_ok) || kpress(eadk_key_exe)) {
    if (mm_sel == 0) { new_session(); g_screen = SC_SETTINGS; }
    else mm_about = true;
  }
}
static void render_menu(void) {
  gfx_fill(0, g_sy0, SCR_W, STRIP_H, 0);
  gfx_stars(UX(-153), UY(-56));
  for (int i = 0; i < 4; i++) gfx_raw_rot(A_MSC0 + i * 3 + msc[i].anim, UX(msc[i].x), UY(msc[i].y), msc[i].ang, 1.0f);
  int sel = mm_sel;
  gfx_rle(A_GAMEPAD, UX(624), UY(385), sel == 1 && !mm_about ? DR_GREEN : 0);
  gfx_rle(A_PLAY, UX(622), UY(293), sel == 0 && !mm_about ? DR_GREEN : 0);
  gfx_rle(A_BANNER, UX(630), UY(165), 0);
  gfx_text_ol(A_FONT_V8, "Credits : " CREDIT1 "  |  " CREDIT2, 160, 228, RGB(255, 255, 255), 0, 1);
  if (mm_about) {
    gfx_rle(A_NP_ABOUT, UX(328.8f), UY(135.6f), 0);
    gfx_rle(A_CLOSEBTN, UX(292), UY(141), DR_GREEN);
    int y = UY(150);
    gfx_text(A_FONT_J12, "All assets and sounds are owned", 160, y, 0xFFFF, 1);
    gfx_text(A_FONT_J12, "by Inner Sloth.", 160, y + 13, 0xFFFF, 1);
    gfx_text(A_FONT_J12, "Note: We are not associated", 160, y + 30, 0xFFFF, 1);
    gfx_text(A_FONT_J12, "with Innersloth in any way.", 160, y + 43, 0xFFFF, 1);
    gfx_text(A_FONT_J12, "Port NumWorks - Credits :", 160, y + 62, RGB(255, 80, 80), 1);
    gfx_text(A_FONT_J12, CREDIT1, 160, y + 75, 0xFFFF, 1);
    gfx_text(A_FONT_J12, CREDIT2, 160, y + 88, 0xFFFF, 1);
  }
}

// --- settings ("Settings and Stuff")
static int st_sel = 0;
static bool st_editing = false;
enum { ST_NAME, ST_CUSTOM, ST_KD, ST_KC, ST_SPEED, ST_MAP, ST_TASK, ST_START, ST_N };
static void slider_adj(int *v, int mn, int mx, int step, int d) {
  *v += d * step;
  if (*v < mn) *v = mn;
  if (*v > mx) *v = mx;
}
static void edit_name(void) {
  // text entry using events (alpha keys)
  int len = strlen(g_name);
  while (1) {
    int32_t to = 100;
    eadk_event_t e = eadk_event_get(&to);
    char ch = 0;
    if (e >= eadk_event_lower_a && e <= eadk_event_lower_z) {
      static const char map[] = "abcdefghijklmnopq\0rstuv\0wxyz";
      int idx = e - eadk_event_lower_a;
      if (idx < (int)sizeof(map)) ch = map[idx];
    } else if (e >= eadk_event_upper_a && e <= eadk_event_upper_z) {
      static const char map[] = "ABCDEFGHIJKLMNOPQ\0RSTUV\0WXYZ";
      int idx = e - eadk_event_upper_a;
      if (idx < (int)sizeof(map)) ch = map[idx];
    } else if (e == eadk_event_zero) ch = '0';
    else if (e == eadk_event_one) ch = '1';
    else if (e == eadk_event_two) ch = '2';
    else if (e == eadk_event_three) ch = '3';
    else if (e == eadk_event_four) ch = '4';
    else if (e == eadk_event_five) ch = '5';
    else if (e == eadk_event_six) ch = '6';
    else if (e == eadk_event_seven) ch = '7';
    else if (e == eadk_event_eight) ch = '8';
    else if (e == eadk_event_nine) ch = '9';
    else if (e == eadk_event_space) ch = ' ';
    else if (e == eadk_event_underscore) ch = '_';
    else if (e == eadk_event_backspace) { if (len) g_name[--len] = 0; }
    else if (e == eadk_event_ok || e == eadk_event_exe || e == eadk_event_back) break;
    if (ch && len < 12) { g_name[len++] = ch; g_name[len] = 0; }
    // redraw only the text box
    for (int s = 0; s < SCR_H / STRIP_H; s++) {
      g_sy0 = s * STRIP_H;
      if (g_sy0 > 80) break;
      gfx_fill(0, g_sy0, SCR_W, STRIP_H, 0);
      gfx_text(A_FONT_J18, "YOUR NAME.", 160, UY(-14) + 2, 0xFFFF, 1);
      gfx_fill(UX(487), UY(66), UX(279), 10, 0xFFFF);
      gfx_rect(UX(487) - 1, UY(66) - 1, UX(279) + 2, 12, RGB(0, 255, 0));
      int w = gfx_text(A_FONT_V8, g_name, UX(487) + 2, UY(66) + 0, 0, 0);
      gfx_fill(UX(487) + 3 + w, UY(66) + 1, 1, 8, 0);
      gfx_text(A_FONT_V8, "alpha + lettres, OK pour valider", 160, UY(66) + 12, RGB(180, 180, 180), 1);
      gfx_present();
    }
  }
  if (!len) strcpy(g_name, "Player");
  k_now = eadk_keyboard_scan();
}
static void update_settings(float dt) {
  (void)dt;
  if (kpress(eadk_key_back)) { g_screen = SC_MENU; return; }
  if (kpress(eadk_key_down)) st_sel = (st_sel + 1) % ST_N;
  if (kpress(eadk_key_up)) st_sel = (st_sel + ST_N - 1) % ST_N;
  int d = kpress(eadk_key_right) ? 1 : kpress(eadk_key_left) ? -1 : 0;
  if (d) {
    switch (st_sel) {
      case ST_KD: slider_adj(&g_killdist, 10, 999, 4 * 5, d); break;
      case ST_KC: slider_adj(&g_killcd, 0, 25, 1, d); break;
      case ST_SPEED: slider_adj(&g_speed, 200, 800, 25, d); break;
      case ST_TASK: slider_adj(&g_taskbar, 25, 200, 25, d); break;
      case ST_MAP: g_map = (g_map + 3 + d) % 3; break;
      default: break;
    }
  }
  if (kpress(eadk_key_ok) || kpress(eadk_key_exe)) {
    if (st_sel == ST_NAME) st_editing = true;
    else if (st_sel == ST_CUSTOM) g_screen = SC_CUSTOM;
    else if (st_sel == ST_MAP) g_map = (g_map + 1) % 3;
    else if (st_sel == ST_START) start_round();
  }
}
static void draw_slider(int x, int y, int w, int v, int mn, int mx, bool sel) {
  int p = (v - mn) * w / (mx - mn);
  gfx_fill(x, y - 1, w, 3, RGB(220, 220, 220));
  gfx_fill(x, y - 1, p, 3, RGB(0, 117, 255));
  gfx_fill(x + p - 2, y - 3, 5, 7, sel ? RGB(0, 255, 0) : RGB(0, 117, 255));
}
static void render_settings(void) {
  gfx_fill(0, g_sy0, SCR_W, STRIP_H, 0);
  gfx_stars(UX(-2452), UY(-2362));
  uint16_t W = 0xFFFF, G = RGB(0, 255, 0);
  char b[12];
  gfx_text(A_FONT_J18, "YOUR NAME.", UX(520 + 112), UY(-14) + 2, W, 1);
  gfx_fill(UX(487), UY(66), UX(279), 10, W);
  if (st_sel == ST_NAME) gfx_rect(UX(487) - 1, UY(66) - 1, UX(279) + 2, 12, G);
  gfx_text(A_FONT_V8, g_name, UX(487) + 2, UY(66), 0, 0);
  gfx_rle(st_sel == ST_CUSTOM ? A_NP_CUSTOM : A_NP_CUSTOM_W, UX(442), UY(150), 0);
  gfx_text(A_FONT_J18, "CUSTOMIZE PLAYER", UX(442 + 182), UY(150) + 1, st_sel == ST_CUSTOM ? G : W, 1);
  gfx_text(A_FONT_J18, "KILL DISTANCE", UX(149 + 252), UY(220) + 1, st_sel == ST_KD ? G : W, 1);
  draw_slider(UX(262), UY(302 + 18), UX(279), g_killdist, 10, 999, st_sel == ST_KD);
  itoa10(g_killdist, b); gfx_text(A_FONT_J12, b, UX(551), UY(302) + 2, W, 0);
  gfx_text(A_FONT_J18, "KILL COOLDOWN", UX(578 + 252), UY(220) + 1, st_sel == ST_KC ? G : W, 1);
  draw_slider(UX(696), UY(303 + 18), UX(279), g_killcd, 0, 25, st_sel == ST_KC);
  itoa10(g_killcd, b); gfx_text(A_FONT_J12, b, UX(989), UY(303) + 2, W, 0);
  gfx_text(A_FONT_J18, "SPEED", UX(-17 + 252), UY(415) + 1, st_sel == ST_SPEED ? G : W, 1);
  draw_slider(UX(98), UY(493 + 18), UX(279), g_speed, 200, 800, st_sel == ST_SPEED);
  itoa10(g_speed, b); gfx_text(A_FONT_J12, b, UX(390), UY(493) + 2, W, 0);
  gfx_text(A_FONT_J18, "TASKS AMOUNT", UX(746 + 252), UY(417) + 1, st_sel == ST_TASK ? G : W, 1);
  draw_slider(UX(859), UY(497 + 18), UX(279), g_taskbar, 25, 200, st_sel == ST_TASK);
  itoa10(g_taskbar, b); gfx_text(A_FONT_J12, b, UX(1142), UY(497) + 2, W, 0);
  gfx_rle(A_MAPICON0 + g_map, UX(626), UY(482), st_sel == ST_MAP ? DR_GREEN : 0);
  static const char *mapn[3] = {"Skeld", "Mixture", "Mira HQ"};
  gfx_text_ol(A_FONT_V8, mapn[g_map], UX(626), UY(482) + 18, W, 0, 1);
  gfx_rle(A_START, UX(629), UY(635), st_sel == ST_START ? DR_GREEN : 0);
  gfx_text(A_FONT_V8, "Haut/Bas : choisir  Gauche/Droite : regler  OK : valider", 160, 231, RGB(150, 150, 150), 1);
}

// --- customise ("Settings and Stuff2")
static int cu_sel = 0;
static void update_custom(float dt) {
  (void)dt;
  if (kpress(eadk_key_back)) { g_screen = SC_SETTINGS; return; }
  if (kpress(eadk_key_down)) cu_sel = (cu_sel + 1) % 4;
  if (kpress(eadk_key_up)) cu_sel = (cu_sel + 3) % 4;
  int d = kpress(eadk_key_right) ? 1 : kpress(eadk_key_left) ? -1 : 0;
  if (d) {
    if (cu_sel == 0) g_color = (g_color + 7 + d) % 7;
    if (cu_sel == 1) g_hat = (g_hat + 15 + d) % 15;
    if (cu_sel == 2) g_pet = (g_pet + 5 + d) % 5;
  }
  if ((kpress(eadk_key_ok) || kpress(eadk_key_exe)) && cu_sel == 3) g_screen = SC_SETTINGS;
}
static void render_custom(void) {
  gfx_fill(0, g_sy0, SCR_W, STRIP_H, 0);
  gfx_stars(UX(-2452), UY(-2362));
  uint16_t W = 0xFFFF, G = RGB(0, 255, 0);
  gfx_text(A_FONT_J18, "YOUR COLOR.", UX(393 + 112), UY(67) - 2, cu_sel == 0 ? G : W, 1);
  gfx_rle(A_ARROWL, UX(423), UY(180), cu_sel == 0 ? DR_GREEN : 0);
  gfx_rle(A_SWATCH0 + g_color, UX(504), UY(180), 0);
  gfx_rle(A_ARROWR, UX(586), UY(181), cu_sel == 0 ? DR_GREEN : 0);
  gfx_text(A_FONT_J18, "YOUR HAT.", UX(670 + 112), UY(71) - 2, cu_sel == 1 ? G : W, 1);
  gfx_rle(A_ARROWL, UX(697), UY(180), cu_sel == 1 ? DR_GREEN : 0);
  if (g_hat == 8) gfx_rle(A_HAT8C0 + g_color, UX(788), UY(186), 0); else gfx_rle(A_HATUI0 + g_hat, UX(788), UY(186), 0);
  gfx_rle(A_ARROWR, UX(868), UY(182), cu_sel == 1 ? DR_GREEN : 0);
  gfx_text(A_FONT_J18, "YOUR PET.", UX(544 + 112), UY(222) - 2, cu_sel == 2 ? G : W, 1);
  gfx_rle(A_ARROWL, UX(572), UY(325), cu_sel == 2 ? DR_GREEN : 0);
  if (g_pet) gfx_rle(A_PETUI0 + g_pet - 1, UX(652), UY(338), 0);
  gfx_rle(A_ARROWR, UX(735), UY(325), cu_sel == 2 ? DR_GREEN : 0);
  // preview of the player
  gfx_rle(A_BIG0 + g_color, UX(400), UY(560), 0);
  gfx_rle(A_BACKBTN, UX(645), UY(577), cu_sel == 3 ? DR_GREEN : 0);
  gfx_text(A_FONT_V8, "Haut/Bas : choisir  Gauche/Droite : changer", 160, 231, RGB(150, 150, 150), 1);
}

// --- vote / ejection
static void update_vote(float dt) {
  v_t += dt;
  if (v_kind == 0) {
    if (v_t > 3.0f) start_round();
  } else {
    v_x += 300 * dt;          // 5 px per tick at 60 fps
    v_ang -= 3 * PI / 180 * 60 * dt;
    if (v_kind == 1 && v_x > 1280 + 100) start_round();
    if (v_kind == 2 && v_t > 5.0f) go_end(false);
  }
}
static void render_vote(void) {
  gfx_fill(0, g_sy0, SCR_W, STRIP_H, 0);
  gfx_stars(UX(-179), UY(-84));
  if (v_kind) {
    int skin = v_kind == 2 ? 9 + g_color : v_crew;
    gfx_raw_rot(A_OUT0 + skin, UX(v_x), UY(270), v_ang, 1.0f);
  }
  int n = (int)(v_t * strlen(v_text) / 1.0f);
  char tmp[40];
  int len = strlen(v_text);
  if (n > len) n = len;
  memcpy(tmp, v_text, n); tmp[n] = 0;
  gfx_text(A_FONT_J18, tmp, 160, UY(263 + 228) - 10, 0xFFFF, 1);
}

// --- end screen
static void update_end(float dt) {
  g_time += dt;
  if (g_time > 2 && (kpress(eadk_key_ok) || kpress(eadk_key_exe) || kpress(eadk_key_back))) {
    g_screen = SC_MENU; menu_init();
  }
}
static void render_end(void) {
  gfx_fill(0, g_sy0, SCR_W, STRIP_H, 0);
  gfx_rle(g_victory ? A_GLOW_V : A_GLOW_D, UX(623), UY(311), 0);
  gfx_rle(g_victory ? A_VICTORY : A_DEFEAT, UX(638), UY(102), 0);
  gfx_rle(A_BIG0 + g_color, UX(632), UY(382), 0);
  gfx_text_ol(A_FONT_V8, g_name, UX(632), UY(382 - 200), 0xFFFF, RGB(120, 0, 0), 1);
  char b[40], n[12];
  strcpy(b, "Kills: "); itoa10(g_kills, n); strcat(b, n);
  strcat(b, "   Time: "); itoa10((int)g_length, n); strcat(b, n); strcat(b, "s");
  gfx_text(A_FONT_V8, b, 160, UY(560), RGB(200, 200, 200), 1);
  if (g_time > 2) {
    gfx_rle(A_NP_CONTINUE, UX(542.2f), UY(158.9f + 450), DR_GREEN);
    gfx_text(A_FONT_J18, "Continue", UX(542.2f + 79), UY(158.9f + 450) + 0, 0xFFFF, 1);
  }
  gfx_text_ol(A_FONT_V8, CREDIT1 "  |  " CREDIT2, 160, 228, RGB(160, 160, 160), 0, 1);
}

static void render_splash(void) {
  gfx_fill(0, g_sy0, SCR_W, STRIP_H, 0);
  gfx_stars(UX(-56), UY(-11));
  gfx_rle(A_BANNER, UX(640), UY(254), 0);
  gfx_text(A_FONT_J18, "NumWorks Edition", 160, UY(330), 0xFFFF, 1);
  gfx_text(A_FONT_J12, "Credits :", 160, UY(420), RGB(255, 80, 80), 1);
  gfx_text(A_FONT_J12, CREDIT1, 160, UY(420) + 14, 0xFFFF, 1);
  gfx_text(A_FONT_J12, CREDIT2, 160, UY(420) + 28, 0xFFFF, 1);
}

// ------------------------------------------------------------------ main
int main(void) {
  menu_init();
  new_session();
  uint64_t last = eadk_timing_millis();
  float splash = 0;
  k_now = eadk_keyboard_scan();
  while (1) {
    read_keys();
    if (kdown(eadk_key_home)) return 0;
    uint64_t now = eadk_timing_millis();
    float dt = (now - last) / 1000.0f;
    last = now;
    if (dt > 0.1f) dt = 0.1f;
    switch (g_screen) {
      case SC_SPLASH: splash += dt; if (splash > 2.0f || kpress(eadk_key_ok)) g_screen = SC_MENU; break;
      case SC_MENU: update_menu(dt); break;
      case SC_SETTINGS:
        update_settings(dt);
        if (st_editing) { st_editing = false; edit_name(); }
        break;
      case SC_CUSTOM: update_custom(dt); break;
      case SC_GAME: update_game(dt); break;
      case SC_VOTE: update_vote(dt); break;
      case SC_END: update_end(dt); break;
    }
    for (int s = 0; s < SCR_H / STRIP_H; s++) {
      g_sy0 = s * STRIP_H;
      switch (g_screen) {
        case SC_SPLASH: render_splash(); break;
        case SC_MENU: render_menu(); break;
        case SC_SETTINGS: render_settings(); break;
        case SC_CUSTOM: render_custom(); break;
        case SC_GAME: render_game(); break;
        case SC_VOTE: render_vote(); break;
        case SC_END: render_end(); break;
      }
      gfx_present();
    }
  }
  return 0;
}
