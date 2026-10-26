// Host stub of the EADK API: scripted keys, framebuffer dumps
#include <eadk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint16_t screen[240][320];
static uint64_t ms;
static int frame;
static FILE *script;
static uint64_t cur_keys; static int hold;
static char outdir[256] = ".";
const char *eadk_external_data; size_t eadk_external_data_size;
static int keycode(const char *n) {
  static const char *names[] = {"left","up","down","right","ok","back","home","","onoff","","","","shift","alpha","xnt","var","toolbox","backspace","exp","ln","log","i","comma","power","sin","cos","tan","pi","sqrt","square","7","8","9","(",")","","4","5","6","*","/","","1","2","3","+","-","","0",".","ee","ans","exe",0};
  for (int i = 0; names[i]; i++) if (names[i][0] && !strcmp(names[i], n)) return i;
  fprintf(stderr, "bad key %s\n", n); exit(2);
}
static void save(const char *name) {
  char p[512]; snprintf(p, sizeof p, "%s/%s.ppm", outdir, name);
  FILE *f = fopen(p, "wb"); fprintf(f, "P6 320 240 255\n");
  for (int y = 0; y < 240; y++) for (int x = 0; x < 320; x++) {
    uint16_t c = screen[y][x];
    unsigned char rgb[3] = {(c >> 11) << 3, ((c >> 5) & 63) << 2, (c & 31) << 3};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}
// script lines: "<frames> key key ..."  |  "shot name"  |  "type text"
static void next_cmd(void) {
  char line[256];
  while (hold <= 0) {
    if (!fgets(line, sizeof line, script)) { fprintf(stderr, "script end at frame %d\n", frame); exit(0); }
    char *t = strtok(line, " \t\n");
    if (!t || t[0] == '#') continue;
    if (!strcmp(t, "shot")) { save(strtok(NULL, " \t\n")); continue; }
    hold = atoi(t); cur_keys = 0;
    while ((t = strtok(NULL, " \t\n"))) cur_keys |= 1ULL << keycode(t);
  }
}
eadk_keyboard_state_t eadk_keyboard_scan() { next_cmd(); hold--; frame++; return cur_keys; }
uint64_t eadk_timing_millis() { ms += 33; return ms; }
void eadk_timing_msleep(uint32_t m) { ms += m; }
void eadk_timing_usleep(uint32_t u) { ms += u / 1000; }
uint32_t eadk_random() { return (uint32_t)rand() * 2654435761u ^ (uint32_t)rand(); }
void eadk_display_push_rect(eadk_rect_t r, const eadk_color_t *px) {
  for (int y = 0; y < r.height; y++) for (int x = 0; x < r.width; x++)
    if (r.y + y < 240 && r.x + x < 320) screen[r.y + y][r.x + x] = px[y * r.width + x];
}
void eadk_display_push_rect_uniform(eadk_rect_t r, eadk_color_t c) {
  for (int y = 0; y < r.height; y++) for (int x = 0; x < r.width; x++) screen[r.y + y][r.x + x] = c;
}
bool eadk_display_wait_for_vblank() { return true; }
eadk_event_t eadk_event_get(int32_t *timeout) {
  (void)timeout;
  static const char *pending = 0; static char buf[64];
  if (!pending || !*pending) {
    // consume one script line: "type xxx" or key frames
    char line[256];
    if (!fgets(line, sizeof line, script)) exit(0);
    if (!strncmp(line, "type ", 5)) { strncpy(buf, line + 5, 63); buf[strcspn(buf, "\n")] = 0; pending = buf; }
    else if (!strncmp(line, "shot ", 5)) { line[strcspn(line, "\n")] = 0; save(line + 5); return 255; }
    else return eadk_event_ok;
  }
  char c = *pending++;
  if (c >= 'a' && c <= 'z') { int i = c - 'a'; return eadk_event_lower_a + i + (i >= 17) + (i >= 22); }
  if (c >= 'A' && c <= 'Z') { int i = c - 'A'; return eadk_event_upper_a + i + (i >= 17) + (i >= 22); }
  if (c == '<') return eadk_event_backspace;
  return eadk_event_space;
}
extern int game_main(void);
int main(int argc, char **argv) {
  script = fopen(argv[1], "r");
  if (argc > 2) strncpy(outdir, argv[2], 255);
  srand(argc > 3 ? atoi(argv[3]) : 1);
  return game_main();
}
