/*
 * tls_logros - motor de logros offline + 60 FPS adaptativo para The Last Story (Dolphin)
 * Dionixu's Launcher
 *
 * Usa rcheevos (la misma libreria que RetroAchievements/Dolphin) para evaluar
 * las condiciones oficiales del set del juego 27 leyendo la memoria emulada
 * de Dolphin desde fuera del proceso. No necesita internet.
 *   Windows: ReadProcessMemory sobre las vistas MEM_MAPPED de Dolphin.
 *   Linux:   el segmento compartido de Dolphin (/dev/shm/dolphin-emu.<pid>)
 *            abierto via /proc/<pid>/fd (no requiere ptrace).
 *
 * Uso:  tls_logros <pid_dolphin> <logros.txt> [--adapt60]
 *   logros.txt : una linea por logro ->  id<TAB>memaddr   (puede estar vacio)
 *
 * Salida (stdout): READY n · HOOKED · UNLOCK id · PROGRESS id v t · FPS 30|60 · ADAPT ON · LOST
 * stderr: MEM1/MEM2 encontrados y DIAG zona=… cada vez que cambia
 * Entrada (stdin): QUIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "rc_runtime.h"

/* ------------------------------------------------------------------ capa de plataforma */
static int plat_open(unsigned long pid);         /* 1 si el proceso existe */
static int plat_find_ram(void);                  /* 1 cuando encuentra MEM1/MEM2 con el juego */
static int plat_read(int region, uint32_t off, uint8_t *dst, uint32_t size);
static void plat_write(uint32_t off, const uint8_t *src, uint32_t size);   /* en MEM1 */
static int plat_alive(void);                     /* proceso vivo */
static double plat_now_ms(void);
static void plat_sleep_ms(double ms);
static void plat_start_stdin(void);
static volatile int quit_req;
static void plat_describe(void);

/* ------------------------------------------------------------------ memoria */
#define MEM1_SIZE 0x01800000u   /* 24 MB visibles (RA: 0x00000000-0x017FFFFF) */
#define MEM2_BASE 0x10000000u
#define MEM2_SIZE 0x04000000u   /* 64 MB (RA: 0x10000000-0x13FFFFFF) */
#define PAGE 0x1000u
#define NPAGES1 (MEM1_SIZE / PAGE)
#define NPAGES2 (MEM2_SIZE / PAGE)

static uint8_t *cache1, *cache2;          /* copias de paginas leidas este frame */
static uint32_t *stamp1, *stamp2;         /* frame en que se leyo cada pagina */
static uint32_t frame_no = 1;

static uint8_t *page_ptr(int region, uint32_t off)
{
  uint32_t pg = off / PAGE;
  uint8_t *cache = region ? cache2 : cache1;
  uint32_t *stamp = region ? stamp2 : stamp1;
  if (stamp[pg] != frame_no) {
    if (!plat_read(region, pg * PAGE, cache + (size_t)pg * PAGE, PAGE))
      memset(cache + (size_t)pg * PAGE, 0, PAGE);
    stamp[pg] = frame_no;
  }
  return cache + (size_t)pg * PAGE + (off % PAGE);
}

static uint32_t RC_CCONV peek(uint32_t address, uint8_t *buffer, uint32_t num_bytes, void *ud)
{
  uint32_t i;
  (void)ud;
  for (i = 0; i < num_bytes; i++) {
    uint32_t a = address + i;
    if (a < MEM1_SIZE) buffer[i] = *page_ptr(0, a);
    else if (a >= MEM2_BASE && a < MEM2_BASE + MEM2_SIZE) buffer[i] = *page_ptr(1, a - MEM2_BASE);
    else return i;
  }
  return num_bytes;
}

/* ------------------------------------------------------------------ eventos */
static rc_runtime_t runtime;

static void out(const char *fmt, uint32_t a, uint32_t b, uint32_t c)
{
  printf(fmt, a, b, c);
  fflush(stdout);
}

static void RC_CCONV on_event(const rc_runtime_event_t *e)
{
  if (e->type == RC_RUNTIME_EVENT_ACHIEVEMENT_TRIGGERED) {
    out("UNLOCK %u\n", e->id, 0, 0);
    rc_runtime_deactivate_achievement(&runtime, e->id);
  } else if (e->type == RC_RUNTIME_EVENT_ACHIEVEMENT_PROGRESS_UPDATED) {
    unsigned v = 0, t = 0;
    if (rc_runtime_get_achievement_measured(&runtime, e->id, &v, &t) && t)
      out("PROGRESS %u %u %u\n", e->id, v, t);
  }
}

/* ------------------------------------------------------------------ definiciones */
static int load_defs(const char *path)
{
  FILE *f = fopen(path, "rb");
  size_t cap = 1 << 16, len;
  char *line;
  int n = 0;
  if (!f) return -1;
  line = (char *)malloc(cap);
  while (fgets(line, (int)cap, f)) {
    char *tab;
    uint32_t id;
    len = strlen(line);
    while (len == cap - 1 && line[len - 1] != '\n') {   /* linea mas larga que el buffer */
      cap *= 2;
      line = (char *)realloc(line, cap);
      if (!fgets(line + len, (int)(cap - len), f)) break;
      len = strlen(line);
    }
    while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
    tab = strchr(line, '\t');
    if (!tab) continue;
    *tab = 0;
    id = (uint32_t)strtoul(line, NULL, 10);
    if (rc_runtime_activate_achievement(&runtime, id, tab + 1, NULL, 0) == RC_OK) n++;
    else fprintf(stderr, "logro %u invalido\n", id);
  }
  free(line);
  fclose(f);
  return n;
}

/* ------------------------------------------------------------------ 60 FPS adaptativo
 * El codigo Gecko de 60 FPS vive en la RAM emulada (lista de codigos tras el
 * code handler, 0x80001800-0x80003000). Si Dolphin no llega a velocidad completa
 * cambiamos sus valores 60/0.5 por 30/1.0 (lo mismo que hace en cinematicas):
 * el juego va a la misma velocidad, solo con menos fluidez. Luego vuelve a 60. */
#define RETRACE 0x0087FE8Cu          /* __VIRetraceCount (r13-0x5834): 60 por segundo de consola */
static uint32_t gecko_off;           /* offset en MEM1 de "04C94DB0 0000003C 04C94C04 3F000000" */
static int adapt_on, adapt_low;      /* activado / ahora en 30 */
static uint32_t rd32(uint32_t off) { uint8_t b[4] = {0}; plat_read(0, off, b, 4); return ((uint32_t)b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]; }
static void wr32(uint32_t off, uint32_t v) {
  uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
  plat_write(off, b, 4);
}
static int find_gecko(void) {
  static uint8_t buf[0x1800];
  uint32_t i;
  if (!plat_read(0, 0x1800, buf, sizeof buf)) return 0;
  for (i = 0; i + 16 <= sizeof buf; i += 4) {
    static const uint8_t pat[16] = { 0x04,0xC9,0x4D,0xB0, 0,0,0,0x3C, 0x04,0xC9,0x4C,0x04, 0x3F,0,0,0 };
    if (memcmp(buf + i, pat, 16) == 0) { gecko_off = 0x1800 + i; return 1; }
  }
  return 0;
}
static void set_low(int low) {
  if (!gecko_off || low == adapt_low) return;
  wr32(gecko_off + 4, low ? 0x1E : 0x3C);
  wr32(gecko_off + 12, low ? 0x3F800000u : 0x3F000000u);
  adapt_low = low;
  out(low ? "FPS 30\n" : "FPS 60\n", 0, 0, 0);
}
/* se llama ~4 veces por segundo con el tiempo real en ms */
static void adapt_tick(double now_ms) {
  static double t_prev, low_since, hold = 8000, last_up;
  static uint32_t r_prev; static int bad;
  uint32_t r; double dt, rate;
  if (!adapt_on) return;
  if (!gecko_off) { static double tried; if (now_ms - tried > 2000) { tried = now_ms; if (find_gecko()) out("ADAPT ON\n", 0, 0, 0); } return; }
  r = rd32(RETRACE);
  if (t_prev == 0) { t_prev = now_ms; r_prev = r; return; }
  dt = now_ms - t_prev; if (dt < 480) return;         /* ventanas de ~0.5 s: menos ruido de cuantizacion */
  rate = (double)(r - r_prev) * 1000.0 / dt;      /* refrescos por segundo real (60 = velocidad completa) */
  t_prev = now_ms; r_prev = r;
  if (rate < 6 || rate > 200) { bad = 0; return; } /* pausa, carga o salto del contador: ignorar */
  if (!adapt_low) {
    bad = rate < 55.5 ? bad + 1 : 0;
    if (bad >= 1) {                                 /* 0.5 s por debajo del 92 % */
      set_low(1); low_since = now_ms; bad = 0;
      if (now_ms - last_up < 15000) hold = hold * 2 > 120000 ? 120000 : hold * 2; else hold = 8000;
    }
  } else if (rate >= 57.5 && now_ms - low_since > hold) { set_low(0); last_up = now_ms; }
}

static int game_alive(void)
{
  uint8_t id[4];
  return plat_alive() && plat_read(0, 0, id, 4) && memcmp(id, "R2GP", 4) == 0;
}

/* ------------------------------------------------------------------ programa */
#ifndef TLS_TEST
int main(int argc, char **argv)
{
  int n, k;
  double t0, next;
  if (argc < 3) { fprintf(stderr, "uso: tls_logros <pid> <logros.txt> [--adapt60]\n"); return 2; }
  for (k = 3; k < argc; k++) if (strcmp(argv[k], "--adapt60") == 0) adapt_on = 1;
  rc_runtime_init(&runtime);
  n = load_defs(argv[2]);
  if (n < 0) { fprintf(stderr, "no puedo abrir %s\n", argv[2]); return 3; }
  out("READY %u\n", (uint32_t)n, 0, 0);

  cache1 = (uint8_t *)malloc(MEM1_SIZE);  cache2 = (uint8_t *)malloc(MEM2_SIZE);
  stamp1 = (uint32_t *)calloc(NPAGES1, 4); stamp2 = (uint32_t *)calloc(NPAGES2, 4);
  plat_start_stdin();
  if (!plat_open(strtoul(argv[1], NULL, 10))) { fprintf(stderr, "no puedo abrir el proceso %s\n", argv[1]); return 4; }

  /* esperar a que el juego arranque (hasta que aparezca la RAM con el ID) */
  while (!plat_find_ram()) {
    if (quit_req || !plat_alive()) { out("LOST\n", 0, 0, 0); return 0; }
    plat_sleep_ms(500);
  }
  out("HOOKED\n", 0, 0, 0);
  plat_describe();

  /* bucle a 60 Hz, como los frames de la consola */
  t0 = plat_now_ms();
  next = 0;
  while (!quit_req) {
    double ms;
    if ((frame_no & 31) == 0 && !game_alive()) { out("LOST\n", 0, 0, 0); break; }
    rc_runtime_do_frame(&runtime, on_event, peek, NULL, NULL);
    if ((frame_no % 15) == 0) adapt_tick(plat_now_ms() - t0);
    if ((frame_no % 300) == 0) {   /* diagnostico: capitulo/zona y punteros que usa el set */
      static uint32_t last[3];
      static const uint32_t A[3] = { 0x87f038, 0x87f430, 0x87f4f0 };
      uint32_t v[3]; int ch = 0;
      for (k = 0; k < 3; k++) { uint8_t b[4] = {0}; peek(A[k], b, 4, NULL); v[k] = ((uint32_t)b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]; if (v[k] != last[k]) ch = 1; last[k] = v[k]; }
      if (ch) { fprintf(stderr, "DIAG zona=%u p1=%08x p2=%08x\n", v[0], v[1], v[2]); fflush(stderr); }
    }
    frame_no++;
    next += 1000.0 / 60.0;
    ms = plat_now_ms() - t0;
    if (next > ms) plat_sleep_ms(next - ms);
    else if (ms - next > 250) next = ms;   /* nos atrasamos (PC ocupado): no acumular */
  }
  if (adapt_low && game_alive()) set_low(0);
  return 0;
}
#endif

#if defined(TLS_TEST)
/* ================================================================== prueba (Linux)
 * tls_logros_test <logros.txt> <frames.bin> : frames.bin = N volcados de MEM1(24MB) */
static FILE *ff;
static long frame_off;
static int plat_open(unsigned long pid) { (void)pid; return 1; }
static int plat_find_ram(void) { return 1; }
static int plat_read(int region, uint32_t off, uint8_t *dst, uint32_t size)
{
  if (region) return 0;
  fseek(ff, frame_off + (long)off, SEEK_SET);
  return fread(dst, 1, size, ff) == size;
}
static void plat_write(uint32_t off, const uint8_t *src, uint32_t size) { (void)off; (void)src; (void)size; }
static int plat_alive(void) { return 1; }
static double plat_now_ms(void) { return 0; }
static void plat_sleep_ms(double ms) { (void)ms; }
static void plat_start_stdin(void) {}
static void plat_describe(void) {}
int main(int argc, char **argv)
{
  int n, i, frames;
  long sz;
  if (argc < 3) return 2;
  rc_runtime_init(&runtime);
  n = load_defs(argv[1]);
  out("READY %u\n", (uint32_t)n, 0, 0);
  cache1 = malloc(MEM1_SIZE); cache2 = malloc(MEM2_SIZE);
  stamp1 = calloc(NPAGES1, 4); stamp2 = calloc(NPAGES2, 4);
  ff = fopen(argv[2], "rb");
  fseek(ff, 0, SEEK_END); sz = ftell(ff);
  frames = (int)(sz / MEM1_SIZE);
  for (i = 0; i < frames; i++) {
    frame_off = (long)i * MEM1_SIZE;
    rc_runtime_do_frame(&runtime, on_event, peek, NULL, NULL);
    frame_no++;
  }
  (void)plat_open; (void)plat_find_ram; (void)plat_write; (void)plat_alive; (void)plat_now_ms; (void)plat_sleep_ms;
  (void)plat_start_stdin; (void)plat_describe; (void)set_low; (void)adapt_tick; (void)game_alive;
  return 0;
}

#elif defined(_WIN32)
/* ================================================================== Windows */
#include <windows.h>

static HANDLE proc;
static uint8_t *base1, *base2;   /* direcciones de MEM1 / MEM2 dentro de Dolphin */
static LARGE_INTEGER qpf;

static int plat_read(int region, uint32_t off, uint8_t *dst, uint32_t size)
{
  SIZE_T got = 0;
  uint8_t *b = region ? base2 : base1;
  return b && ReadProcessMemory(proc, b + off, dst, size, &got) && got == size;
}
static void plat_write(uint32_t off, const uint8_t *src, uint32_t size)
{
  SIZE_T w = 0;
  if (base1) WriteProcessMemory(proc, base1 + off, src, size, &w);
}
static int is_mapped(uint8_t *addr, SIZE_T size)
{
  MEMORY_BASIC_INFORMATION mi;
  return VirtualQueryEx(proc, addr, &mi, sizeof mi) == sizeof mi &&
         mi.BaseAddress == addr && mi.Type == MEM_MAPPED && mi.RegionSize == size &&
         mi.State == MEM_COMMIT;
}
/* Igual que Dolphin Memory Engine: region MEM_MAPPED de 32 MB con el ID del
 * juego al principio y MEM2 (64 MB) justo 0x10000000 despues. */
static int plat_find_ram(void)
{
  uint8_t *p = 0, *m1 = 0, *m2 = 0;
  MEMORY_BASIC_INFORMATION mi;
  while (VirtualQueryEx(proc, p, &mi, sizeof mi) == sizeof mi) {
    uint8_t *b = (uint8_t *)mi.BaseAddress;
    if (mi.Type == MEM_MAPPED && mi.State == MEM_COMMIT) {
      if (mi.RegionSize == 0x2000000) {
        char id[6];
        SIZE_T got = 0;
        if (ReadProcessMemory(proc, b, id, 6, &got) && got == 6 && memcmp(id, "R2GP", 4) == 0) {
          if (is_mapped(b + 0x10000000, 0x4000000)) { base1 = b; base2 = b + 0x10000000; return 1; }
          if (!m1) m1 = b;
        }
      } else if (mi.RegionSize == 0x4000000 && !m2) {
        m2 = b;                                         /* MEM2 suelto (sin fastmem) */
      }
    }
    p = b + mi.RegionSize;
    if (!mi.RegionSize) break;
  }
  if (m1 && m2) { base1 = m1; base2 = m2; return 1; }
  return 0;
}
static int plat_open(unsigned long pid)
{
  QueryPerformanceFrequency(&qpf);
  proc = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION, FALSE, (DWORD)pid);
  if (!proc) proc = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, (DWORD)pid);
  { HMODULE wm = LoadLibraryA("winmm.dll"); if (wm) { typedef UINT(WINAPI *tbp_t)(UINT); tbp_t f = (tbp_t)GetProcAddress(wm, "timeBeginPeriod"); if (f) f(1); } }
  return proc != NULL;
}
static int plat_alive(void)
{
  DWORD code = 0;
  return GetExitCodeProcess(proc, &code) && code == STILL_ACTIVE;
}
static double plat_now_ms(void)
{
  LARGE_INTEGER q; QueryPerformanceCounter(&q);
  return (double)q.QuadPart * 1000.0 / (double)qpf.QuadPart;
}
static void plat_sleep_ms(double ms) { if (ms >= 1) Sleep((DWORD)ms); }
static DWORD WINAPI stdin_thread(LPVOID u)
{
  char buf[256];
  (void)u;
  while (fgets(buf, sizeof buf, stdin))
    if (strncmp(buf, "QUIT", 4) == 0) break;
  quit_req = 1;
  return 0;
}
static void plat_start_stdin(void) { CreateThread(NULL, 0, stdin_thread, NULL, 0, NULL); }
static void plat_describe(void) { fprintf(stderr, "MEM1=%p MEM2=%p\n", (void *)base1, (void *)base2); fflush(stderr); }

#elif defined(__linux__)
/* ================================================================== Linux
 * Dolphin guarda toda la RAM emulada en un segmento compartido creado con
 * shm_open("/dolphin-emu.<pid>") y luego desenlazado. Lo reabrimos desde
 * /proc/<pid>/fd/N (permitido para el mismo usuario, sin ptrace).
 * Disposicion (Wii): MEM1 (32 MB) en 0, cache L1, y MEM2 (64 MB) al final. */
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

static int shm_fd = -1;
static off_t mem2_off;
static pid_t root_pid, dol_pid;

static int plat_read(int region, uint32_t off, uint8_t *dst, uint32_t size)
{
  off_t o = region ? mem2_off + (off_t)off : (off_t)off;
  return shm_fd >= 0 && pread(shm_fd, dst, size, o) == (ssize_t)size;
}
static void plat_write(uint32_t off, const uint8_t *src, uint32_t size)
{
  if (shm_fd >= 0 && pwrite(shm_fd, src, size, (off_t)off) != (ssize_t)size) { /* sin permiso de escritura: se ignora */ }
}
static pid_t parent_of(pid_t p)
{
  char path[64], buf[512]; FILE *f; int pp = 0;
  snprintf(path, sizeof path, "/proc/%d/stat", (int)p);
  if (!(f = fopen(path, "r"))) return 0;
  if (fgets(buf, sizeof buf, f)) { char *e = strrchr(buf, ')'); if (e) sscanf(e + 2, "%*c %d", &pp); }
  fclose(f);
  return (pid_t)pp;
}
static int descends(pid_t p)   /* p es root_pid o descendiente suyo */
{
  int guard = 0;
  while (p > 1 && guard++ < 64) { if (p == root_pid) return 1; p = parent_of(p); }
  return 0;
}
static int try_pid(pid_t p)
{
  char dir[64], link[300], target[512];
  DIR *d; struct dirent *e; int found = 0;
  snprintf(dir, sizeof dir, "/proc/%d/fd", (int)p);
  if (!(d = opendir(dir))) return 0;
  while (!found && (e = readdir(d))) {
    ssize_t n;
    if (e->d_name[0] == '.') continue;
    snprintf(link, sizeof link, "%s/%s", dir, e->d_name);
    n = readlink(link, target, sizeof target - 1);
    if (n <= 0) continue;
    target[n] = 0;
    if (!strstr(target, "dolphin-emu.")) continue;
    {
      int fd = open(link, O_RDWR | O_CLOEXEC);
      struct stat st; char id[4];
      if (fd < 0) fd = open(link, O_RDONLY | O_CLOEXEC);
      if (fd < 0) continue;
      if (fstat(fd, &st) == 0 && st.st_size >= (off_t)(0x2000000 + 0x4000000) &&
          pread(fd, id, 4, 0) == 4 && memcmp(id, "R2GP", 4) == 0) {
        if (shm_fd >= 0) close(shm_fd);
        shm_fd = fd; mem2_off = st.st_size - 0x4000000; dol_pid = p; found = 1;
      } else close(fd);
    }
  }
  closedir(d);
  return found;
}
static int scan_proc(int only_descendants)
{
  DIR *d; struct dirent *e; uid_t me = getuid(); int ok = 0;
  if (!(d = opendir("/proc"))) return 0;
  while (!ok && (e = readdir(d))) {
    char path[300]; struct stat st; pid_t p = (pid_t)atoi(e->d_name);
    if (p <= 1) continue;
    snprintf(path, sizeof path, "/proc/%s", e->d_name);
    if (stat(path, &st) != 0 || st.st_uid != me) continue;
    if (only_descendants && !descends(p)) continue;
    ok = try_pid(p);
  }
  closedir(d);
  return ok;
}
static int plat_find_ram(void)
{
  static int tries;
  if (try_pid(root_pid)) return 1;
  if (scan_proc(1)) return 1;
  /* Dolphin lanzado por otro camino (flatpak/AppImage reparentado): cualquier Dolphin del usuario */
  return ++tries > 6 && scan_proc(0);
}
static int plat_open(unsigned long pid) { root_pid = (pid_t)pid; return kill(root_pid, 0) == 0; }
static int plat_alive(void) { pid_t p = dol_pid ? dol_pid : root_pid; return kill(p, 0) == 0; }
static double plat_now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000.0 + t.tv_nsec / 1e6; }
static void plat_sleep_ms(double ms)
{
  struct timespec t; if (ms <= 0) return;
  t.tv_sec = (time_t)(ms / 1000); t.tv_nsec = (long)((ms - t.tv_sec * 1000.0) * 1e6);
  nanosleep(&t, NULL);
}
static void *stdin_thread(void *u)
{
  char buf[256]; (void)u;
  while (fgets(buf, sizeof buf, stdin)) if (strncmp(buf, "QUIT", 4) == 0) break;
  quit_req = 1;
  return NULL;
}
static void plat_start_stdin(void) { pthread_t t; pthread_create(&t, NULL, stdin_thread, NULL); pthread_detach(t); }
static void plat_describe(void) { fprintf(stderr, "SHM pid=%d MEM2@0x%llx\n", (int)dol_pid, (unsigned long long)mem2_off); fflush(stderr); }
#endif
