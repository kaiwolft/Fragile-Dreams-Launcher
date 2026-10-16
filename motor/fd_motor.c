/*
 * fd_motor.exe - motor del launcher de Fragile Dreams (Dionixu's Launcher)
 * Frontend libretro mínimo para el core de Dolphin (dolphin_libretro.dll):
 *   ventana Win32 + OpenGL 3.3 core (HW render), audio waveOut, mando XInput + teclado,
 *   puntero de la Wii calculado aquí (linterna), acceso a la memoria del juego (mods),
 *   y órdenes del launcher por stdin / eventos por stdout.
 *
 * Uso: fd_motor.exe --core <dll> --game <main.dol|iso> --system <dir> --save <dir>
 *                   [--opts <archivo key=value>] [--log <archivo>] [--fullscreen 0|1|2 (2 = ventana sin bordes)] [--vsync 0|1]
 *                   [--width N --height N] [--volume 0-100] [--title texto]
 *
 * stdin  (una orden por línea): PAUSE | RESUME | VOLUME n | QUIT | FOCUS
 * stdout (eventos):              READY | STARTED | COMBO | KEY ESC | QUIT | ERROR texto | LOG texto
 *
 * Compilado con clang --target=x86_64-w64-windows-gnu, sin CRT de arranque (punto de entrada fd_entry).
 */
#include <windows.h>
#include <mmsystem.h>
#include <xinput.h>
#include <shellapi.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include "libretro.h"

/* ------------------------------------------------------------------ utilidades */
static FILE *g_log;
static CRITICAL_SECTION g_logcs;
static void logf_(const char *fmt, ...)
{
    if (!g_log) return;
    EnterCriticalSection(&g_logcs);
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(g_log, "%02d:%02d:%02d.%03d  ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
    fputc('\n', g_log); fflush(g_log);
    LeaveCriticalSection(&g_logcs);
}
static HANDLE g_out;
static void emit(const char *fmt, ...)
{
    char buf[1024]; va_list ap; va_start(ap, fmt); int n = vsnprintf(buf, sizeof buf - 2, fmt, ap); va_end(ap);
    if (n < 0) return; if (n > (int)sizeof buf - 2) n = sizeof buf - 2;
    buf[n++] = '\n'; DWORD w;
    if (g_out && g_out != INVALID_HANDLE_VALUE) WriteFile(g_out, buf, n, &w, NULL);
    logf_("> %.*s", n - 1, buf);
}
static char *w2u(const wchar_t *w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char *s = (char *)malloc(n); WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL); return s;
}
static wchar_t *u2w(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w = (wchar_t *)malloc(n * sizeof(wchar_t)); MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n); return w;
}
static double g_t0;
static double now_s(void)
{
    static LARGE_INTEGER f; LARGE_INTEGER c;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c); return (double)c.QuadPart / (double)f.QuadPart;
}
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
/* msvcrt no trae vsnprintf C99: puente a _vsnprintf (siempre termina en 0) */
int __cdecl __ms_vsnprintf(char *b, size_t n, const char *f, va_list ap)
{
    int r = _vsnprintf(b, n, f, ap);
    if (n) b[n - 1] = 0;
    return r < 0 ? (int)(n ? n - 1 : 0) : r;
}
/* curva de respuesta del stick (~x^1.6 entre 0 y 1) sin pow(): en msvcrt es un dato y sin CRT fallaría */
static float curve16(float a) { return 0.4f * a + 0.6f * a * a; }

/* ------------------------------------------------------------------ argumentos */
static char *a_core, *a_game, *a_system, *a_save, *a_opts, *a_log, *a_title;
static int a_full = 1, a_w = 1280, a_h = 720, a_vol = 100, a_vsync = 1;
static bool g_vsync_paced;          /* V-Sync activo y el monitor va a ~60 Hz: el ritmo lo marca SwapBuffers */
static void *g_swapint; static int g_hz, g_vsync_on = -1;

/* ------------------------------------------------------------------ core */
static HMODULE g_core;
#define CORE_FN(ret, name, args) static ret (*p_##name) args;
CORE_FN(void, retro_set_environment, (retro_environment_t))
CORE_FN(void, retro_set_video_refresh, (retro_video_refresh_t))
CORE_FN(void, retro_set_audio_sample, (retro_audio_sample_t))
CORE_FN(void, retro_set_audio_sample_batch, (retro_audio_sample_batch_t))
CORE_FN(void, retro_set_input_poll, (retro_input_poll_t))
CORE_FN(void, retro_set_input_state, (retro_input_state_t))
CORE_FN(void, retro_init, (void))
CORE_FN(void, retro_deinit, (void))
CORE_FN(unsigned, retro_api_version, (void))
CORE_FN(void, retro_get_system_info, (struct retro_system_info *))
CORE_FN(void, retro_get_system_av_info, (struct retro_system_av_info *))
CORE_FN(void, retro_set_controller_port_device, (unsigned, unsigned))
CORE_FN(void, retro_run, (void))
CORE_FN(bool, retro_load_game, (const struct retro_game_info *))
CORE_FN(void, retro_unload_game, (void))
CORE_FN(void *, retro_get_memory_data, (unsigned))
CORE_FN(size_t, retro_get_memory_size, (unsigned))

static struct retro_system_av_info g_av;
static struct retro_hw_render_callback g_hw;
static bool g_hw_ok;
static unsigned g_geo_w = 640, g_geo_h = 528;
static float g_aspect = 16.0f / 9.0f;

/* opciones del core: valores por defecto declarados por el core + los nuestros */
#define MAXOPT 400
static struct { char key[96]; char val[128]; } g_opt[MAXOPT];
static int g_nopt;
static bool g_opt_dirty = true;
static const char *opt_get(const char *key)
{
    for (int i = 0; i < g_nopt; i++) if (!strcmp(g_opt[i].key, key)) return g_opt[i].val;
    return NULL;
}
static void opt_set(const char *key, const char *val, bool override)
{
    for (int i = 0; i < g_nopt; i++)
        if (!strcmp(g_opt[i].key, key)) { if (override) { snprintf(g_opt[i].val, sizeof g_opt[i].val, "%s", val); g_opt_dirty = true; } return; }
    if (g_nopt >= MAXOPT) return;
    snprintf(g_opt[g_nopt].key, sizeof g_opt[0].key, "%s", key);
    snprintf(g_opt[g_nopt].val, sizeof g_opt[0].val, "%s", val);
    g_nopt++; g_opt_dirty = true;
}
/* las nuestras (del archivo --opts) tienen prioridad sobre los valores por defecto del core */
static struct { char key[96]; char val[128]; } g_user[MAXOPT];
static int g_nuser;
static void load_opts_file(const char *path)
{
    if (!path) return;
    FILE *f = _wfopen(u2w(path), L"rb"); if (!f) { logf_("sin archivo de opciones %s", path); return; }
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char *e = strchr(line, '='); if (!e || line[0] == '#') continue;
        *e = 0; char *k = line, *v = e + 1;
        while (*k == ' ') k++; char *ke = k + strlen(k); while (ke > k && ke[-1] == ' ') *--ke = 0;
        while (*v == ' ') v++; char *ve = v + strlen(v); while (ve > v && (ve[-1] == '\n' || ve[-1] == '\r' || ve[-1] == ' ')) *--ve = 0;
        if (g_nuser < MAXOPT) { snprintf(g_user[g_nuser].key, 96, "%s", k); snprintf(g_user[g_nuser].val, 128, "%s", v); g_nuser++; }
    }
    fclose(f);
    for (int i = 0; i < g_nuser; i++) opt_set(g_user[i].key, g_user[i].val, true);
    logf_("opciones propias: %d", g_nuser);
}
static bool is_user_opt(const char *key)
{
    for (int i = 0; i < g_nuser; i++) if (!strcmp(g_user[i].key, key)) return true;
    return false;
}

/* ------------------------------------------------------------------ ventana + OpenGL */
static HWND g_hwnd;
static HDC g_dc;
static HGLRC g_rc;
static volatile LONG g_quit;
static bool g_focus = true;
static volatile bool g_game_cursor;
static volatile bool g_mlook;        /* mirando con el ratón (cursor capturado) */
static float g_mdx, g_mdy;           /* movimiento del ratón acumulado (píxeles) */   /* el juego dibuja su propio cursor (menús): el de Windows se oculta */
static bool g_kbmode, g_paused;
static HCURSOR g_cursor;
static volatile LONG g_wheel;

typedef HGLRC (WINAPI *PFN_wglCreateContextAttribsARB)(HDC, HGLRC, const int *);
typedef BOOL (WINAPI *PFN_wglSwapIntervalEXT)(int);
#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x0001
#define WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB 0x0002

#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#define GL_DEPTH24_STENCIL8 0x88F0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_RGBA8 0x8058
#define GL_CLAMP_TO_EDGE 0x812F
typedef void (APIENTRY *PFN_glGenFramebuffers)(GLsizei, GLuint *);
typedef void (APIENTRY *PFN_glBindFramebuffer)(GLenum, GLuint);
typedef void (APIENTRY *PFN_glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef void (APIENTRY *PFN_glGenRenderbuffers)(GLsizei, GLuint *);
typedef void (APIENTRY *PFN_glBindRenderbuffer)(GLenum, GLuint);
typedef void (APIENTRY *PFN_glRenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei);
typedef void (APIENTRY *PFN_glFramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint);
typedef GLenum (APIENTRY *PFN_glCheckFramebufferStatus)(GLenum);
typedef void (APIENTRY *PFN_glBlitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
typedef void (APIENTRY *PFN_glDeleteFramebuffers)(GLsizei, const GLuint *);
typedef void (APIENTRY *PFN_glDeleteRenderbuffers)(GLsizei, const GLuint *);
static PFN_glGenFramebuffers glGenFramebuffers_;
static PFN_glBindFramebuffer glBindFramebuffer_;
static PFN_glFramebufferTexture2D glFramebufferTexture2D_;
static PFN_glGenRenderbuffers glGenRenderbuffers_;
static PFN_glBindRenderbuffer glBindRenderbuffer_;
static PFN_glRenderbufferStorage glRenderbufferStorage_;
static PFN_glFramebufferRenderbuffer glFramebufferRenderbuffer_;
static PFN_glCheckFramebufferStatus glCheckFramebufferStatus_;
static PFN_glBlitFramebuffer glBlitFramebuffer_;
static PFN_glDeleteFramebuffers glDeleteFramebuffers_;
static PFN_glDeleteRenderbuffers glDeleteRenderbuffers_;
static HMODULE g_opengl32;

static retro_proc_address_t RETRO_CALLCONV gl_proc(const char *name)
{
    void *p = (void *)wglGetProcAddress(name);
    if (p == 0 || p == (void *)1 || p == (void *)2 || p == (void *)3 || p == (void *)-1)
        p = (void *)GetProcAddress(g_opengl32, name);
    return (retro_proc_address_t)p;
}

static GLuint g_fbo, g_fbo_tex, g_fbo_rb;
static unsigned g_fbo_w, g_fbo_h;
static void fbo_create(unsigned w, unsigned h)
{
    if (g_fbo && w <= g_fbo_w && h <= g_fbo_h) return;
    if (g_fbo) { glDeleteFramebuffers_(1, &g_fbo); glDeleteTextures(1, &g_fbo_tex); glDeleteRenderbuffers_(1, &g_fbo_rb); }
    g_fbo_w = w; g_fbo_h = h;
    glGenTextures(1, &g_fbo_tex);
    glBindTexture(GL_TEXTURE_2D, g_fbo_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);
    glGenFramebuffers_(1, &g_fbo);
    glBindFramebuffer_(GL_FRAMEBUFFER, g_fbo);
    glFramebufferTexture2D_(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_fbo_tex, 0);
    if (g_hw.depth) {
        glGenRenderbuffers_(1, &g_fbo_rb);
        glBindRenderbuffer_(GL_RENDERBUFFER, g_fbo_rb);
        glRenderbufferStorage_(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
        glFramebufferRenderbuffer_(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, g_fbo_rb);
        glBindRenderbuffer_(GL_RENDERBUFFER, 0);
    }
    GLenum st = glCheckFramebufferStatus_(GL_FRAMEBUFFER);
    logf_("FBO %ux%u estado 0x%X", w, h, st);
    glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer_(GL_FRAMEBUFFER, 0);
}
static uintptr_t RETRO_CALLCONV hw_get_fb(void) { return g_fbo; }

/* ---- F8: graba 30 imágenes seguidas (mitad de tamaño) en motor\volcados\capNN_XX.bmp para ver animaciones ---- */
#define CAP_N 30
static uint32_t rd32(uint32_t addr);
static int g_cap_left, g_cap_set;
static unsigned g_cap_w, g_cap_h;
static uint8_t *g_cap_buf, *g_cap_tmp;
static void cap_save(void)
{
    wchar_t dir[MAX_PATH]; GetModuleFileNameW(NULL, dir, MAX_PATH);
    wchar_t *sl = wcsrchr(dir, L'\\'); if (sl) *sl = 0;
    wcscat(dir, L"\\volcados"); CreateDirectoryW(dir, NULL);
    unsigned W = g_cap_w, H = g_cap_h, row = (W * 3 + 3) & ~3u;
    for (int i = 0; i < CAP_N; i++) {
        wchar_t f[MAX_PATH]; _snwprintf(f, MAX_PATH, L"%ls\\cap%02d_%02d.bmp", dir, g_cap_set, i);
        FILE *o = _wfopen(f, L"wb"); if (!o) continue;
        uint8_t hd[54] = {'B','M'}; uint32_t fs = 54 + row * H;
        memcpy(hd + 2, &fs, 4); hd[10] = 54; hd[14] = 40; memcpy(hd + 18, &W, 4); memcpy(hd + 22, &H, 4);
        hd[26] = 1; hd[28] = 24; fwrite(hd, 1, 54, o);
        uint8_t *fr = g_cap_buf + (size_t)i * W * H * 3, pad[4] = {0};
        for (unsigned y = 0; y < H; y++) { fwrite(fr + (size_t)y * W * 3, 1, W * 3, o); fwrite(pad, 1, row - W * 3, o); }
        fclose(o);
    }
    logf_("CAPTURAS %02d guardadas (%ux%u, intervalo %u)", g_cap_set, W, H, rd32(0x80412268));
}
static void cap_frame(unsigned w, unsigned h)
{
    static bool f8p; bool f8 = (GetAsyncKeyState(VK_F8) & 0x8000) && GetForegroundWindow() == g_hwnd;
    if (f8 && !f8p && !g_cap_left) {
        g_cap_w = w / 2; g_cap_h = h / 2;
        free(g_cap_buf); free(g_cap_tmp);
        g_cap_buf = malloc((size_t)g_cap_w * g_cap_h * 3 * CAP_N); g_cap_tmp = malloc((size_t)w * h * 4);
        if (g_cap_buf && g_cap_tmp) { g_cap_left = CAP_N; g_cap_set++; MessageBeep(MB_OK); }
    }
    f8p = f8;
    if (!g_cap_left || w / 2 != g_cap_w || h / 2 != g_cap_h) return;
    glBindFramebuffer_(GL_READ_FRAMEBUFFER, g_fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, g_cap_tmp);
    glBindFramebuffer_(GL_READ_FRAMEBUFFER, 0);
    int i = CAP_N - g_cap_left;
    uint8_t *dst = g_cap_buf + (size_t)i * g_cap_w * g_cap_h * 3;
    for (unsigned y = 0; y < g_cap_h; y++) {
        unsigned sy = g_hw.bottom_left_origin ? y * 2 : (h - 1 - y * 2);
        const uint8_t *src = g_cap_tmp + (size_t)sy * w * 4;
        uint8_t *d = dst + (size_t)y * g_cap_w * 3;
        for (unsigned x = 0; x < g_cap_w; x++) { d[x*3] = src[x*8+2]; d[x*3+1] = src[x*8+1]; d[x*3+2] = src[x*8]; }
    }
    if (--g_cap_left == 0) cap_save();
}

static void present(unsigned w, unsigned h)
{
    cap_frame(w, h);
    RECT rc; GetClientRect(g_hwnd, &rc);
    int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
    if (cw <= 0 || ch <= 0) return;
    /* encaje con la relación de aspecto del juego (bandas negras si hace falta) */
    float a = g_aspect > 0.1f ? g_aspect : (float)w / (float)h;
    int vw = cw, vh = (int)(cw / a + 0.5f);
    if (vh > ch) { vh = ch; vw = (int)(ch * a + 0.5f); }
    int vx = (cw - vw) / 2, vy = (ch - vh) / 2;
    glBindFramebuffer_(GL_DRAW_FRAMEBUFFER, 0);
    glViewport(0, 0, cw, ch);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer_(GL_READ_FRAMEBUFFER, g_fbo);
    if (g_hw.bottom_left_origin)
        glBlitFramebuffer_(0, 0, w, h, vx, vy, vx + vw, vy + vh, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    else
        glBlitFramebuffer_(0, 0, w, h, vx, vy + vh, vx + vw, vy, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer_(GL_READ_FRAMEBUFFER, 0);
    if (!IsIconic(g_hwnd)) SwapBuffers(g_dc);
}

static unsigned g_last_w, g_last_h;
static long g_frames;
static void RETRO_CALLCONV cb_video(const void *data, unsigned w, unsigned h, size_t pitch)
{
    (void)pitch;
    if (data == RETRO_HW_FRAME_BUFFER_VALID) { g_last_w = w; g_last_h = h; g_frames++; present(w, h); }
    else if (!data && g_last_w) present(g_last_w, g_last_h); /* fotograma repetido */
    /* (sin render por software: el core usa siempre HW) */
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_CLOSE: InterlockedExchange(&g_quit, 1); return 0;
    case WM_ACTIVATEAPP: g_focus = w != 0; break;
    case WM_SETCURSOR: if (LOWORD(l) == HTCLIENT) { SetCursor(g_kbmode && !g_paused && !g_game_cursor && !g_mlook ? g_cursor : NULL); return TRUE; } break;
    case WM_MOUSEWHEEL: InterlockedIncrement(&g_wheel); return 0;
    case WM_SYSKEYDOWN: if (w == VK_F4) { InterlockedExchange(&g_quit, 1); return 0; } return 0;   /* Alt solo no abre el menú de ventana */
    case WM_SYSKEYUP: return 0;
    case WM_SYSCOMMAND: if ((w & 0xFFF0) == SC_KEYMENU) return 0; break;   /* Alt / F10 no "roban" el teclado */
    }
    return DefWindowProcW(h, m, w, l);
}

static bool window_create(void)
{
    HINSTANCE hi = GetModuleHandleW(NULL);
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof wc; wc.style = CS_OWNDC; wc.lpfnWndProc = wndproc; wc.hInstance = hi;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"FDMotor";
    wc.hIcon = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    RegisterClassExW(&wc);
    DWORD style; int x, y, w, h;
    if (a_full == 1) {
        HMONITOR mon = MonitorFromPoint((POINT){0, 0}, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi = {sizeof mi}; GetMonitorInfoW(mon, &mi);
        style = WS_POPUP; x = mi.rcMonitor.left; y = mi.rcMonitor.top;
        w = mi.rcMonitor.right - mi.rcMonitor.left; h = mi.rcMonitor.bottom - mi.rcMonitor.top;
        /* 1 píxel más alto que el monitor: así Windows/NVIDIA no lo tratan como pantalla completa exclusiva
         * ("independent flip", la imagen se salta el compositor) y las capturas (Discord, OBS ventana, Xbox Game Bar)
         * ven la imagen en vivo en vez de negra o congelada. Visualmente es igual. */
        h += 1;
    } else {
        style = a_full == 2 ? WS_POPUP : WS_OVERLAPPEDWINDOW;   /* 2 = ventana sin bordes */
        /* en ventana: que quepa en el escritorio (90 % del área útil, manteniendo 16:9) */
        RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        int mw = (wa.right - wa.left) * 9 / 10, mh = (wa.bottom - wa.top) * 9 / 10;
        if (a_w > mw) { a_h = a_h * mw / a_w; a_w = mw; }
        if (a_h > mh) { a_w = a_w * mh / a_h; a_h = mh; }
        RECT r = {0, 0, a_w, a_h}; AdjustWindowRect(&r, style, FALSE);
        w = r.right - r.left; h = r.bottom - r.top;
        x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2; y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
    }
    wchar_t *title = u2w(a_title ? a_title : "Fragile Dreams: Farewell Ruins of the Moon");
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, title, style, x, y, w, h, NULL, NULL, hi, NULL);
    if (!g_hwnd) return false;
    g_dc = GetDC(g_hwnd);
    PIXELFORMATDESCRIPTOR pfd = {0};
    pfd.nSize = sizeof pfd; pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 32; pfd.cDepthBits = 24; pfd.cStencilBits = 8;
    int pf = ChoosePixelFormat(g_dc, &pfd);
    if (!pf || !SetPixelFormat(g_dc, pf, &pfd)) { logf_("SetPixelFormat falló"); return false; }
    HGLRC tmp = wglCreateContext(g_dc); wglMakeCurrent(g_dc, tmp);
    PFN_wglCreateContextAttribsARB cca = (PFN_wglCreateContextAttribsARB)wglGetProcAddress("wglCreateContextAttribsARB");
    int maj = g_hw.version_major ? (int)g_hw.version_major : 3, min = g_hw.version_major ? (int)g_hw.version_minor : 3;
    bool core = g_hw.context_type == RETRO_HW_CONTEXT_OPENGL_CORE;
    if (!core) { maj = 3; min = 3; }
    int attrs[] = {WGL_CONTEXT_MAJOR_VERSION_ARB, maj, WGL_CONTEXT_MINOR_VERSION_ARB, min,
                   WGL_CONTEXT_PROFILE_MASK_ARB, core ? WGL_CONTEXT_CORE_PROFILE_BIT_ARB : WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB, 0};
    g_rc = cca ? cca(g_dc, NULL, attrs) : NULL;
    if (!g_rc) { logf_("wglCreateContextAttribsARB falló, uso el contexto básico"); g_rc = tmp; }
    else { wglMakeCurrent(NULL, NULL); wglDeleteContext(tmp); }
    wglMakeCurrent(g_dc, g_rc);
    g_opengl32 = GetModuleHandleW(L"opengl32.dll");
#define LOADGL(n) n##_ = (PFN_##n)gl_proc(#n)
    LOADGL(glGenFramebuffers); LOADGL(glBindFramebuffer); LOADGL(glFramebufferTexture2D);
    LOADGL(glGenRenderbuffers); LOADGL(glBindRenderbuffer); LOADGL(glRenderbufferStorage);
    LOADGL(glFramebufferRenderbuffer); LOADGL(glCheckFramebufferStatus); LOADGL(glBlitFramebuffer);
    LOADGL(glDeleteFramebuffers); LOADGL(glDeleteRenderbuffers);
    PFN_wglSwapIntervalEXT si = (PFN_wglSwapIntervalEXT)wglGetProcAddress("wglSwapIntervalEXT");
    /* V-Sync: intervalo 1 (sin cortes). Si el monitor va a ~60 Hz, SwapBuffers ya marca el ritmo y el limitador no espera;
     * a otras frecuencias (120/144 Hz) el limitador sigue dejando el juego a 60. */
    DEVMODEW dm = {0}; dm.dmSize = sizeof dm;
    int hz = EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &dm) ? (int)dm.dmDisplayFrequency : 0;
    /* Se arranca SIEMPRE con intervalo 0: con V-Sync y la ventana tapada (el launcher sigue encima hasta las primeras
     * imágenes) el driver de NVIDIA deja SwapBuffers bloqueado y el juego no avanza. vsync_update() lo activa solo
     * cuando la ventana del juego está al frente y lo quita si deja de estarlo. */
    g_swapint = (PFN_wglSwapIntervalEXT)si; g_hz = hz;
    if (si) si(0);
    logf_("V-Sync %s | monitor %d Hz", a_vsync ? (si ? "pedido (se activa con la ventana al frente)" : "no disponible") : "desactivado", hz);
    logf_("OpenGL %s | %s | %s", (const char *)glGetString(GL_VERSION), (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VENDOR));
    ShowWindow(g_hwnd, SW_SHOW); SetForegroundWindow(g_hwnd); SetFocus(g_hwnd);
    return glGenFramebuffers_ && glBlitFramebuffer_;
}

/* ------------------------------------------------------------------ audio (waveOut) */
#define AUD_BLOCKS 8
#define AUD_BLOCK_FRAMES 1024
static HWAVEOUT g_wo;
static WAVEHDR g_wh[AUD_BLOCKS];
static int16_t g_ab[AUD_BLOCKS][AUD_BLOCK_FRAMES * 2];
static int g_ab_cur, g_ab_fill;
static unsigned g_rate;
static float g_volume = 1.0f;
static void audio_open(unsigned rate)
{
    if (g_wo) { waveOutReset(g_wo); for (int i = 0; i < AUD_BLOCKS; i++) waveOutUnprepareHeader(g_wo, &g_wh[i], sizeof(WAVEHDR)); waveOutClose(g_wo); g_wo = NULL; }
    g_rate = rate ? rate : 48000;
    WAVEFORMATEX wf = {0};
    wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = 2; wf.nSamplesPerSec = g_rate; wf.wBitsPerSample = 16;
    wf.nBlockAlign = 4; wf.nAvgBytesPerSec = g_rate * 4;
    if (waveOutOpen(&g_wo, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) { logf_("waveOutOpen falló (%u Hz)", g_rate); g_wo = NULL; return; }
    for (int i = 0; i < AUD_BLOCKS; i++) {
        memset(&g_wh[i], 0, sizeof(WAVEHDR));
        g_wh[i].lpData = (LPSTR)g_ab[i]; g_wh[i].dwBufferLength = AUD_BLOCK_FRAMES * 4;
        waveOutPrepareHeader(g_wo, &g_wh[i], sizeof(WAVEHDR));
        g_wh[i].dwFlags |= WHDR_DONE;
    }
    g_ab_cur = 0; g_ab_fill = 0;
    logf_("audio %u Hz", g_rate);
}
static int audio_queued(void)
{
    int q = 0; for (int i = 0; i < AUD_BLOCKS; i++) if (!(g_wh[i].dwFlags & WHDR_DONE)) q++;
    return q;
}
static size_t RETRO_CALLCONV cb_audio_batch(const int16_t *d, size_t frames)
{
    if (!g_wo || g_paused) return frames;
    size_t done = 0;
    while (done < frames) {
        WAVEHDR *h = &g_wh[g_ab_cur];
        if (!(h->dwFlags & WHDR_DONE)) {
            /* todos los bloques en uso: espera un poco (esto también frena si el juego va más rápido de la cuenta) */
            int spins = 0;
            while (!(h->dwFlags & WHDR_DONE) && spins++ < 40) Sleep(1);
            if (!(h->dwFlags & WHDR_DONE)) return frames; /* audio atascado: descarta */
        }
        size_t n = AUD_BLOCK_FRAMES - g_ab_fill; if (n > frames - done) n = frames - done;
        int16_t *dst = g_ab[g_ab_cur] + g_ab_fill * 2;
        const int16_t *src = d + done * 2;
        if (g_volume >= 0.999f) memcpy(dst, src, n * 4);
        else for (size_t i = 0; i < n * 2; i++) dst[i] = (int16_t)(src[i] * g_volume);
        g_ab_fill += (int)n; done += n;
        if (g_ab_fill == AUD_BLOCK_FRAMES) {
            h->dwFlags &= ~WHDR_DONE;
            waveOutWrite(g_wo, h, sizeof(WAVEHDR));
            g_ab_cur = (g_ab_cur + 1) % AUD_BLOCKS; g_ab_fill = 0;
        }
    }
    return frames;
}
static void RETRO_CALLCONV cb_audio(int16_t l, int16_t r) { int16_t s[2] = {l, r}; cb_audio_batch(s, 1); }

/* ------------------------------------------------------------------ mando + teclado + ratón */
#include "cursor_luciernaga.h"
/* acciones del mando clásico (las mismas que en Ajustes → Controles del launcher) */
enum { AC_A, AC_B, AC_X, AC_Y, AC_R, AC_L, AC_ZL, AC_ZR, AC_PLUS, AC_MINUS, AC_DU, AC_DD, AC_DL, AC_DR,
       AC_LSU, AC_LSD, AC_LSL, AC_LSR, AC_RSU, AC_RSD, AC_RSL, AC_RSR, AC_N };
static const char *AC_NAME[AC_N] = {"a", "b", "x", "y", "r", "l", "zl", "zr", "plus", "minus", "du", "dd", "dl", "dr",
                                     "ls_up", "ls_down", "ls_left", "ls_right", "rs_up", "rs_down", "rs_left", "rs_right"};
/* entrada asignable: K<vk> (tecla o botón del ratón), P<botón del mando>, W (rueda del ratón) */
enum { T_KEY = 1, T_PAD, T_LT, T_RT, T_WHEEL };
typedef struct { uint8_t type; uint16_t code; } bind_t;
static bind_t g_bind[AC_N][8];
static int g_nbind[AC_N];
static void bind_add(int ac, int type, int code) { if (g_nbind[ac] < 8) { g_bind[ac][g_nbind[ac]].type = (uint8_t)type; g_bind[ac][g_nbind[ac]].code = (uint16_t)code; g_nbind[ac]++; } }
static int pad_code(const char *n)
{
    static const struct { const char *n; int m; } P[] = {
        {"A", XINPUT_GAMEPAD_A}, {"B", XINPUT_GAMEPAD_B}, {"X", XINPUT_GAMEPAD_X}, {"Y", XINPUT_GAMEPAD_Y},
        {"LB", XINPUT_GAMEPAD_LEFT_SHOULDER}, {"RB", XINPUT_GAMEPAD_RIGHT_SHOULDER}, {"L3", XINPUT_GAMEPAD_LEFT_THUMB},
        {"R3", XINPUT_GAMEPAD_RIGHT_THUMB}, {"START", XINPUT_GAMEPAD_START}, {"BACK", XINPUT_GAMEPAD_BACK},
        {"DU", XINPUT_GAMEPAD_DPAD_UP}, {"DD", XINPUT_GAMEPAD_DPAD_DOWN}, {"DL", XINPUT_GAMEPAD_DPAD_LEFT}, {"DR", XINPUT_GAMEPAD_DPAD_RIGHT}};
    for (unsigned i = 0; i < sizeof P / sizeof *P; i++) if (!strcmp(P[i].n, n)) return P[i].m;
    return 0;
}
static void bind_token(int ac, const char *t)
{
    if (t[0] == 'K') { int vk = atoi(t + 1); if (vk > 0 && vk < 256 && vk != VK_ESCAPE) bind_add(ac, T_KEY, vk); }
    else if (t[0] == 'P') { if (!strcmp(t + 1, "LT")) bind_add(ac, T_LT, 0); else if (!strcmp(t + 1, "RT")) bind_add(ac, T_RT, 0); else { int m = pad_code(t + 1); if (m) bind_add(ac, T_PAD, m); } }
    else if (t[0] == 'W') bind_add(ac, T_WHEEL, 0);
}
/* controles por defecto = los de siempre (posición Xbox) */
static void binds_default(void)
{
    static const char *D[AC_N] = {
        "PA K1 K13 K69", "PB K8 K81", "PY K70", "PX K32", "PRT K160", "PRB PL3 K67 K162", "PLB PR3 K82 K4 W", "PLT K2 K86",
        "PSTART K187", "PBACK K189", "PDU K38", "PDD K40", "PDL K37", "PDR K39",
        "K87", "K83", "K65", "K68", "K73", "K75", "K74", "K76"};
    for (int i = 0; i < AC_N; i++) {
        g_nbind[i] = 0;
        char tmp[96]; snprintf(tmp, sizeof tmp, "%s", D[i]);
        for (char *t = strtok(tmp, " "); t; t = strtok(NULL, " ")) bind_token(i, t);
    }
}
static char *a_controls;
static int a_mode;                 /* 0 auto, 1 mando, 2 teclado y ratón */
static bool a_padinv, a_mouseinv;
static float a_msens = 1.0f;
static bool a_fps60 = true;        /* 60 FPS mientras se juega (fps60 = on|off) */       /* sensibilidad del ratón al mirar (mouse_sens = %, 100 = normal) */
static void binds_load(void)
{
    binds_default();
    if (!a_controls) return;
    FILE *f = _wfopen(u2w(a_controls), L"rb"); if (!f) { logf_("sin archivo de controles %s", a_controls); return; }
    char line[512]; int n = 0;
    while (fgets(line, sizeof line, f)) {
        char *e = strchr(line, '='); if (!e || line[0] == '#') continue;
        *e = 0; char *k = line; while (*k == ' ') k++; char *ke = k + strlen(k); while (ke > k && ke[-1] == ' ') *--ke = 0;
        if (!strcmp(k, "mode")) { char *v = e + 1; while (*v == ' ') v++; a_mode = !strncmp(v, "pad", 3) ? 1 : !strncmp(v, "kb", 2) ? 2 : 0; continue; }
        if (!strcmp(k, "pad_invert")) { a_padinv = strstr(e + 1, "on") != NULL; continue; }
        if (!strcmp(k, "mouse_invert")) { a_mouseinv = strstr(e + 1, "on") != NULL; continue; }
        if (!strcmp(k, "fps60")) { a_fps60 = strstr(e + 1, "on") != NULL; continue; }
        if (!strcmp(k, "mouse_sens")) { int v = atoi(e + 1); if (v >= 10 && v <= 400) a_msens = v / 100.0f; continue; }
        for (int ac = 0; ac < AC_N; ac++) if (!strcmp(k, AC_NAME[ac])) {
            g_nbind[ac] = 0;
            for (char *t = strtok(e + 1, " \r\n\t"); t; t = strtok(NULL, " \r\n\t")) bind_token(ac, t);
            n++; break;
        }
    }
    fclose(f);
    logf_("controles cargados: %d acciones, modo %d", n, a_mode);
}

static XINPUT_STATE g_xi;
static bool g_xi_ok;
static WORD g_xi_prev;
static float g_ls[2], g_rs[2], g_lt, g_rt;
static bool g_kb[256];
static bool g_act[AC_N];
static double g_wheel_until;
static float g_mx, g_my;               /* ratón dentro de la imagen del juego, -1..1 */
static bool g_mouse_in;
/* puntero (linterna): calculado aquí, -1..1 */
static float g_px, g_py, g_fpx, g_fpy;
static volatile bool clipped;          /* ratón confinado a la imagen (pantalla completa) */
static bool g_cam_live;                /* el parche de cámara está corriendo (en juego, no en menús/escenas) */

static void cursor_create(void)
{
    BITMAPV5HEADER bi = {0};
    bi.bV5Size = sizeof bi; bi.bV5Width = CUR_W; bi.bV5Height = -CUR_H; bi.bV5Planes = 1; bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS; bi.bV5RedMask = 0x00FF0000; bi.bV5GreenMask = 0x0000FF00; bi.bV5BlueMask = 0x000000FF; bi.bV5AlphaMask = 0xFF000000;
    void *bits = NULL; HDC dc = GetDC(NULL);
    HBITMAP col = CreateDIBSection(dc, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, dc);
    if (!col || !bits) return;
    memcpy(bits, g_cursor_bgra, CUR_W * CUR_H * 4);
    HBITMAP mask = CreateBitmap(CUR_W, CUR_H, 1, 1, NULL);
    ICONINFO ii = {0}; ii.fIcon = FALSE; ii.xHotspot = CUR_W / 2; ii.yHotspot = CUR_H / 2; ii.hbmMask = mask; ii.hbmColor = col;
    g_cursor = (HCURSOR)CreateIconIndirect(&ii);
    DeleteObject(col); DeleteObject(mask);
}
static void set_kbmode(bool kb)
{
    if (a_mode == 1) kb = false; else if (a_mode == 2) kb = true;
    if (kb == g_kbmode) return;
    g_kbmode = kb;
    emit("DEVICE %s", kb ? "kb" : "pad");
    PostMessageW(g_hwnd, WM_SETCURSOR, (WPARAM)g_hwnd, MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
}
/* zona de la ventana donde se ve el juego (con las bandas negras fuera) */
static void game_viewport(int *vx, int *vy, int *vw, int *vh)
{
    RECT rc; GetClientRect(g_hwnd, &rc);
    int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
    float a = g_aspect > 0.1f ? g_aspect : 16.0f / 9.0f;
    *vw = cw; *vh = (int)(cw / a + 0.5f);
    if (*vh > ch) { *vh = ch; *vw = (int)(ch * a + 0.5f); }
    *vx = (cw - *vw) / 2; *vy = (ch - *vh) / 2;
}

static float axis(SHORT v, float dz)
{
    float f = v / 32767.0f; if (f < -1) f = -1;
    float a = fabsf(f); if (a < dz) return 0;
    return copysignf((a - dz) / (1 - dz), f);
}
static bool bind_down(const bind_t *b, double t)
{
    switch (b->type) {
    case T_KEY: return g_kb[b->code];
    case T_PAD: return (g_xi.Gamepad.wButtons & b->code) != 0;
    case T_LT: return g_lt > 0.5f;
    case T_RT: return g_rt > 0.5f;
    case T_WHEEL: return t < g_wheel_until;
    }
    return false;
}
static void input_poll_all(double dt)
{
    double t = now_s();
    g_xi_ok = XInputGetState(0, &g_xi) == ERROR_SUCCESS;
    if (!g_xi_ok) memset(&g_xi, 0, sizeof g_xi);
    XINPUT_GAMEPAD *p = &g_xi.Gamepad;
    g_ls[0] = axis(p->sThumbLX, 0.15f); g_ls[1] = axis(p->sThumbLY, 0.15f);
    g_rs[0] = axis(p->sThumbRX, 0.15f); g_rs[1] = axis(p->sThumbRY, 0.15f);
    if (a_padinv) g_rs[1] = -g_rs[1];
    g_lt = p->bLeftTrigger / 255.0f; g_rt = p->bRightTrigger / 255.0f;
    /* foco = la ventana del juego es la ventana activa (WM_ACTIVATEAPP no llega si se activó uniendo hilos) */
    g_focus = g_hwnd && GetForegroundWindow() == g_hwnd;
    for (int k = 0; k < 256; k++) g_kb[k] = g_focus && (GetAsyncKeyState(k) & 0x8000);
    LONG w = InterlockedExchange(&g_wheel, 0);
    if (w) g_wheel_until = t + 0.12;   /* un paso de rueda = pulsación corta */

    /* ¿qué se está usando? mando o teclado/ratón (cambio automático) */
    bool pad_used = g_xi_ok && (p->wButtons || fabsf(g_ls[0]) + fabsf(g_ls[1]) + fabsf(g_rs[0]) + fabsf(g_rs[1]) > 0.3f || g_lt > 0.3f || g_rt > 0.3f);
    bool kb_used = false;
    for (int ac = 0; ac < AC_N && !kb_used; ac++)
        for (int i = 0; i < g_nbind[ac]; i++) if ((g_bind[ac][i].type == T_KEY && g_kb[g_bind[ac][i].code]) || (g_bind[ac][i].type == T_WHEEL && w)) { kb_used = true; break; }
    /* ratón dentro de la imagen del juego */
    POINT cp; GetCursorPos(&cp); ScreenToClient(g_hwnd, &cp);
    int vx, vy, vw, vh; game_viewport(&vx, &vy, &vw, &vh);
    float mx = vw > 0 ? (cp.x - vx) / (float)vw * 2 - 1 : 0, my = vh > 0 ? (cp.y - vy) / (float)vh * 2 - 1 : 0;
    /* (invertir el ratón ahora solo afecta a la cámara, no al cursor de los menús) */
    static float lmx, lmy;
    if (g_focus && (fabsf(mx - lmx) + fabsf(my - lmy)) > 0.01f) kb_used = true;
    lmx = mx; lmy = my;
    g_mouse_in = mx >= -1 && mx <= 1 && my >= -1 && my <= 1;
    g_mx = clampf(mx, -1, 1); g_my = clampf(my, -1, 1);
    if (pad_used) set_kbmode(false); else if (kb_used) set_kbmode(true);

    for (int ac = 0; ac < AC_N; ac++) {
        bool d = false;
        for (int i = 0; i < g_nbind[ac] && !d; i++) d = bind_down(&g_bind[ac][i], t);
        g_act[ac] = d;
    }
    /* diagnóstico de teclado: cada 2 s (máx. 40 líneas) si hay teclas pulsadas, qué ve el motor */
    { static double lt; static int n; double tt = now_s();
      if (n < 40 && tt - lt > 2) {
        char buf[200]; int k2 = 0; buf[0] = 0;
        for (int k = 8; k < 256 && k2 < 180; k++) if (GetAsyncKeyState(k) & 0x8000) k2 += snprintf(buf + k2, sizeof buf - k2, "%d ", k);
        if (k2) { lt = tt; n++; logf_("teclado: pulsadas [%s] | foco %d | modo %s | izq %.1f %.1f | A %d B %d", buf, g_focus, g_kbmode ? "teclado" : "mando",
                  (g_act[AC_LSR] ? 1.f : 0.f) - (g_act[AC_LSL] ? 1.f : 0.f), (g_act[AC_LSU] ? 1.f : 0.f) - (g_act[AC_LSD] ? 1.f : 0.f), g_act[AC_A], g_act[AC_B]); } } }
    /* teclado: sticks con teclas */
    if (g_act[AC_LSL]) g_ls[0] = -1; if (g_act[AC_LSR]) g_ls[0] = 1; if (g_act[AC_LSU]) g_ls[1] = 1; if (g_act[AC_LSD]) g_ls[1] = -1;
    float krx = (g_act[AC_RSR] ? 1.f : 0.f) - (g_act[AC_RSL] ? 1.f : 0.f), kry = (g_act[AC_RSU] ? 1.f : 0.f) - (g_act[AC_RSD] ? 1.f : 0.f);
    if (krx || kry) { g_rs[0] = krx; g_rs[1] = kry; }

    /* L3+R3 o Esc: menú de pausa del launcher */
    WORD b = p->wButtons;
    bool both = (b & XINPUT_GAMEPAD_LEFT_THUMB) && (b & XINPUT_GAMEPAD_RIGHT_THUMB);
    bool bothp = (g_xi_prev & XINPUT_GAMEPAD_LEFT_THUMB) && (g_xi_prev & XINPUT_GAMEPAD_RIGHT_THUMB);
    if (both && !bothp) emit("COMBO");
    static bool escp;
    bool esc = g_kb[VK_ESCAPE];
    if (esc && !escp) emit("COMBO KB");
    escp = esc;
    g_xi_prev = b;

    /* ---- puntero de la Wii (linterna) ----
     * ratón: el puntero va donde está el ratón (como apuntar con el Wiimote)
     * mando, tercera persona: como una cámara; curva ~1.6, sube rápido (0,06 s) y vuelve al frente suave (0,3 s)
     * mando, primera persona (acción ZR = B del Wiimote): el puntero se queda donde lo dejas */
    bool fp = g_act[AC_ZR];
    if (g_cam_live && !g_kbmode) {
        /* mod de cámara: el stick derecho mueve la cámara (lo lee el parche); la linterna/mira queda al centro */
        g_px = g_py = g_fpx = g_fpy = 0;
    } else if (g_kbmode && !krx && !kry) {
        g_px = g_mx; g_py = g_my; g_fpx = g_px; g_fpy = g_py;
    } else if (fp) {
        g_fpx = clampf(g_fpx + g_rs[0] * 1.4f * (float)dt, -1, 1);
        g_fpy = clampf(g_fpy - g_rs[1] * 1.4f * (float)dt, -1, 1);
        g_px = g_fpx; g_py = g_fpy;
    } else {
        float tx = copysignf(curve16(fabsf(g_rs[0])), g_rs[0]);
        float ty = -copysignf(curve16(fabsf(g_rs[1])), g_rs[1]);
        float kx = (fabsf(tx) > fabsf(g_px) ? 0.06f : 0.30f), ky = (fabsf(ty) > fabsf(g_py) ? 0.06f : 0.30f);
        float mx2 = (float)dt / kx, my2 = (float)dt / ky;
        g_px += clampf(tx - g_px, -mx2, mx2); g_py += clampf(ty - g_py, -my2, my2);
        g_fpx = g_px; g_fpy = g_py;
    }
    /* ---- mirar con el ratón (estilo PC): jugando con teclado y ratón el cursor se oculta y se queda en el centro de la
     * ventana; cada fotograma se mide cuánto se movió y eso gira la cámara (en mods_pre). En menús/escenas, cursor libre. */
    bool ml = g_kbmode && g_cam_live && g_focus && !g_paused;
    {
        RECT rc; GetClientRect(g_hwnd, &rc); POINT c = {(rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2}; ClientToScreen(g_hwnd, &c);
        if (ml) {
            POINT q; GetCursorPos(&q);
            if (g_mlook) { g_mdx += (float)(q.x - c.x); g_mdy += (float)(q.y - c.y); }
            SetCursorPos(c.x, c.y);
        } else if (g_mlook) SetCursorPos(c.x, c.y);          /* al salir (menú) el cursor aparece al centro */
        if (ml != g_mlook) { g_mlook = ml; g_mdx = g_mdy = 0; PostMessageW(g_hwnd, WM_SETCURSOR, (WPARAM)g_hwnd, MAKELPARAM(HTCLIENT, WM_MOUSEMOVE)); }
    }
    /* con el ratón en pantalla completa (o mirando con el ratón), el cursor no se sale de la imagen */
    bool want = g_kbmode && g_focus && (a_full == 1 || g_mlook) && !g_paused;
    if (want && !clipped) { RECT r; GetClientRect(g_hwnd, &r); POINT a = {r.left, r.top}, z = {r.right, r.bottom}; ClientToScreen(g_hwnd, &a); ClientToScreen(g_hwnd, &z); RECT s = {a.x, a.y, z.x, z.y}; ClipCursor(&s); clipped = true; }
    else if (!want && clipped) { ClipCursor(NULL); clipped = false; }
}
static void RETRO_CALLCONV cb_input_poll(void) {}

/* acciones -> ids de RetroPad que el core traduce al mando clásico:
 *   JOYPAD_A=A  JOYPAD_B=B  JOYPAD_X=X  JOYPAD_Y=Y  L2=L  R2=R  L=ZL  R=ZR  START=+  SELECT=-  R3=Home */
static bool pad_btn(unsigned id)
{
    switch (id) {
    case RETRO_DEVICE_ID_JOYPAD_A: return g_act[AC_A];
    case RETRO_DEVICE_ID_JOYPAD_B: return g_act[AC_B];
    case RETRO_DEVICE_ID_JOYPAD_X: return g_act[AC_X];
    case RETRO_DEVICE_ID_JOYPAD_Y: return g_act[AC_Y];
    case RETRO_DEVICE_ID_JOYPAD_R2: return g_act[AC_R];
    case RETRO_DEVICE_ID_JOYPAD_L2: return g_act[AC_L];
    case RETRO_DEVICE_ID_JOYPAD_L: return g_act[AC_ZL];
    case RETRO_DEVICE_ID_JOYPAD_R: return g_act[AC_ZR];
    case RETRO_DEVICE_ID_JOYPAD_START: return g_act[AC_PLUS];
    case RETRO_DEVICE_ID_JOYPAD_SELECT: return g_act[AC_MINUS];
    case RETRO_DEVICE_ID_JOYPAD_UP: return g_act[AC_DU];
    case RETRO_DEVICE_ID_JOYPAD_DOWN: return g_act[AC_DD];
    case RETRO_DEVICE_ID_JOYPAD_LEFT: return g_act[AC_DL];
    case RETRO_DEVICE_ID_JOYPAD_RIGHT: return g_act[AC_DR];
    default: return false;
    }
}
static int16_t RETRO_CALLCONV cb_input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
    if (port != 0 || g_paused) return 0;
    switch (device & RETRO_DEVICE_MASK) {
    case RETRO_DEVICE_JOYPAD:
        if (id == RETRO_DEVICE_ID_JOYPAD_MASK) { int16_t m = 0; for (unsigned i = 0; i < 16; i++) if (pad_btn(i)) m |= 1 << i; return m; }
        return pad_btn(id) ? 1 : 0;
    case RETRO_DEVICE_ANALOG: {
        if (index == RETRO_DEVICE_INDEX_ANALOG_BUTTON) return pad_btn(id) ? 0x7FFF : 0;
        static const float zero[2];
        const float *s = index == RETRO_DEVICE_INDEX_ANALOG_LEFT ? g_ls : g_cam_live ? zero : g_rs;
        if (id == RETRO_DEVICE_ID_ANALOG_X) return (int16_t)(s[0] * 32767);
        if (id == RETRO_DEVICE_ID_ANALOG_Y) return (int16_t)(-s[1] * 32767); /* libretro: abajo es positivo */
        return 0;
    }
    case RETRO_DEVICE_POINTER:
        if (index != 0) return 0;
        if (id == RETRO_DEVICE_ID_POINTER_X) return (int16_t)(clampf(g_px, -1, 1) * 32767);
        if (id == RETRO_DEVICE_ID_POINTER_Y) return (int16_t)(clampf(g_py, -1, 1) * 32767);
        if (id == RETRO_DEVICE_ID_POINTER_PRESSED) return 0;
        if (id == RETRO_DEVICE_ID_POINTER_COUNT) return 1;
        return 0;
    }
    return 0;
}
static bool RETRO_CALLCONV cb_rumble(unsigned port, enum retro_rumble_effect e, uint16_t s)
{
    static XINPUT_VIBRATION v;
    if (port != 0) return false;
    if (e == RETRO_RUMBLE_STRONG) v.wLeftMotorSpeed = s; else v.wRightMotorSpeed = s;
    XInputSetState(0, &v);
    return true;
}

static uint8_t *g_mem2;
static size_t g_mem2_size;
/* ------------------------------------------------------------------ entorno libretro */
static uint64_t RETRO_CALLCONV perf_cpu(void) { return 0; }
static retro_time_t RETRO_CALLCONV perf_usec(void) { return (retro_time_t)(now_s() * 1e6); }
static retro_perf_tick_t RETRO_CALLCONV perf_counter(void) { LARGE_INTEGER c; QueryPerformanceCounter(&c); return c.QuadPart; }
static void RETRO_CALLCONV perf_register(struct retro_perf_counter *c) { c->registered = true; }
static void RETRO_CALLCONV perf_start(struct retro_perf_counter *c) { (void)c; }
static void RETRO_CALLCONV perf_stop(struct retro_perf_counter *c) { (void)c; }
static void RETRO_CALLCONV perf_log(void) {}
static void RETRO_CALLCONV core_log(enum retro_log_level lvl, const char *fmt, ...)
{
    static const char *n[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    if (lvl < RETRO_LOG_INFO) return;
    char buf[2048]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    if (strstr(buf, "warning C7") || strstr(buf, "might be used before being initialized")) return;  /* avisos inofensivos del compilador de shaders */
    size_t L = strlen(buf); while (L && (buf[L - 1] == '\n' || buf[L - 1] == '\r')) buf[--L] = 0;
    logf_("[core %s] %s", n[lvl & 3], buf);
}
static char *g_sysdir_u, *g_savedir_u;

static bool RETRO_CALLCONV cb_env(unsigned cmd, void *data)
{
    unsigned c = cmd & ~RETRO_ENVIRONMENT_EXPERIMENTAL;
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: ((struct retro_log_callback *)data)->log = core_log; return true;
    case RETRO_ENVIRONMENT_GET_PERF_INTERFACE: {
        struct retro_perf_callback *p = (struct retro_perf_callback *)data;
        p->get_time_usec = perf_usec; p->get_cpu_features = perf_cpu; p->get_perf_counter = perf_counter;
        p->perf_register = perf_register; p->perf_start = perf_start; p->perf_stop = perf_stop; p->perf_log = perf_log;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: *(const char **)data = g_sysdir_u; return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char **)data = g_savedir_u; return true;
    case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY: *(const char **)data = g_sysdir_u; return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool *)data = true; return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: return *(enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_XRGB8888;
    case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER: *(unsigned *)data = RETRO_HW_CONTEXT_OPENGL_CORE; return true;
    case RETRO_ENVIRONMENT_SET_HW_RENDER: {
        struct retro_hw_render_callback *h = (struct retro_hw_render_callback *)data;
        logf_("SET_HW_RENDER tipo %d versión %u.%u depth %d stencil %d bottom_left %d", h->context_type, h->version_major, h->version_minor, h->depth, h->stencil, h->bottom_left_origin);
        if (h->context_type != RETRO_HW_CONTEXT_OPENGL_CORE && h->context_type != RETRO_HW_CONTEXT_OPENGL) return false;
        h->get_current_framebuffer = hw_get_fb;
        h->get_proc_address = gl_proc;
        g_hw = *h; g_hw_ok = true;
        return true;
    }
    case RETRO_ENVIRONMENT_SET_HW_SHARED_CONTEXT: return true;
    case RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE: return false;
    case RETRO_ENVIRONMENT_SET_GEOMETRY: {
        const struct retro_game_geometry *g = (const struct retro_game_geometry *)data;
        g_geo_w = g->base_width; g_geo_h = g->base_height; if (g->aspect_ratio > 0) g_aspect = g->aspect_ratio;
        logf_("geometría %ux%u aspecto %.3f", g->base_width, g->base_height, g->aspect_ratio);
        if (g_rc) fbo_create(g->max_width > g_fbo_w ? g->max_width : g_fbo_w, g->max_height > g_fbo_h ? g->max_height : g_fbo_h);
        return true;
    }
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: {
        const struct retro_system_av_info *a = (const struct retro_system_av_info *)data;
        g_av = *a; if (a->geometry.aspect_ratio > 0) g_aspect = a->geometry.aspect_ratio;
        logf_("AV %.3f fps %.0f Hz %ux%u (máx %ux%u)", a->timing.fps, a->timing.sample_rate, a->geometry.base_width, a->geometry.base_height, a->geometry.max_width, a->geometry.max_height);
        if (g_rc) fbo_create(a->geometry.max_width, a->geometry.max_height);
        if ((unsigned)(a->timing.sample_rate + 0.5) != g_rate) audio_open((unsigned)(a->timing.sample_rate + 0.5));
        return true;
    }
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION: *(unsigned *)data = 2; return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2: {
        const struct retro_core_options_v2 *o = (const struct retro_core_options_v2 *)data;
        int n = 0;
        for (const struct retro_core_option_v2_definition *d = o->definitions; d && d->key; d++, n++) {
            const char *dv = d->default_value ? d->default_value : d->values[0].value;
            if (dv) opt_set(d->key, dv, false);
        }
        logf_("opciones del core declaradas: %d", n);
        for (int i = 0; i < g_nuser; i++) opt_set(g_user[i].key, g_user[i].val, true);
        return true;
    }
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS: {
        const struct retro_core_option_definition *d = (const struct retro_core_option_definition *)data;
        for (; d && d->key; d++) opt_set(d->key, d->default_value ? d->default_value : d->values[0].value, false);
        for (int i = 0; i < g_nuser; i++) opt_set(g_user[i].key, g_user[i].val, true);
        return true;
    }
    case RETRO_ENVIRONMENT_SET_VARIABLES: {
        const struct retro_variable *v = (const struct retro_variable *)data;
        for (; v && v->key; v++) {
            const char *p = strchr(v->value, ';'); if (!p) continue; p++; while (*p == ' ') p++;
            char first[128]; int i = 0; while (p[i] && p[i] != '|' && i < 127) { first[i] = p[i]; i++; } first[i] = 0;
            opt_set(v->key, first, false);
        }
        for (int i = 0; i < g_nuser; i++) opt_set(g_user[i].key, g_user[i].val, true);
        return true;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *v = (struct retro_variable *)data;
        v->value = v->key ? opt_get(v->key) : NULL;
        return v->value != NULL;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool *)data = g_opt_dirty; g_opt_dirty = false; return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
    case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
    case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE:
    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE:
    case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
    case RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE:
        return true;
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS: {
        const struct retro_memory_map *mm = (const struct retro_memory_map *)data;
        for (unsigned i = 0; i < mm->num_descriptors; i++) {
            const struct retro_memory_descriptor *d = &mm->descriptors[i];
            logf_("mapa de memoria: %08zX (%zu bytes) %p %s", d->start, d->len, d->ptr, d->addrspace ? d->addrspace : "");
            if (d->start == 0x90000000) { g_mem2 = (uint8_t *)d->ptr; g_mem2_size = d->len; }
        }
        return true;
    }
    case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE: ((struct retro_rumble_interface *)data)->set_rumble_state = cb_rumble; return true;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS: return true;
    case RETRO_ENVIRONMENT_GET_LANGUAGE: *(unsigned *)data = RETRO_LANGUAGE_SPANISH; return true;
    case RETRO_ENVIRONMENT_GET_USERNAME: *(const char **)data = "Seto"; return true;
    case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE: *(int *)data = 3; return true;
    case RETRO_ENVIRONMENT_GET_FASTFORWARDING: *(bool *)data = false; return true;
    case RETRO_ENVIRONMENT_GET_TARGET_REFRESH_RATE: *(float *)data = 60.0f; return true;
    case RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION: *(unsigned *)data = 1; return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE: logf_("[mensaje] %s", ((const struct retro_message *)data)->msg); return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE_EXT: logf_("[mensaje] %s", ((const struct retro_message_ext *)data)->msg); return true;
    case RETRO_ENVIRONMENT_SHUTDOWN: InterlockedExchange(&g_quit, 1); return true;
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL: return true;
    default:
        break;
    }
    if (c == 81) { *(unsigned *)data = 48000; return true; }  /* GET_TARGET_SAMPLE_RATE */
    static unsigned seen[64]; static int ns;
    int k; for (k = 0; k < ns; k++) if (seen[k] == cmd) break;
    if (k == ns && ns < 64) { seen[ns++] = cmd; logf_("entorno no atendido: %u (0x%X)", c, cmd); }
    return false;
}

/* ------------------------------------------------------------------ memoria del juego (mods) */
static uint8_t *g_mem1;
static size_t g_mem1_size;
static uint32_t rd32(uint32_t addr)
{
    uint32_t o = addr & 0x1FFFFFF;
    if (!g_mem1 || o + 4 > g_mem1_size) return 0;
    const uint8_t *p = g_mem1 + o; return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}
/* ---- herramienta de análisis: F9 guarda MEM1 + MEM2 en motor\volcados (para localizar cámara y jugador) ---- */
static int g_dump_n;
static void dump_memory(void)
{
    wchar_t dir[MAX_PATH]; GetModuleFileNameW(NULL, dir, MAX_PATH);
    wchar_t *sl = wcsrchr(dir, L'\\'); if (sl) *sl = 0;
    wcscat(dir, L"\\volcados"); CreateDirectoryW(dir, NULL);
    g_dump_n++;
    struct { const char *n; uint8_t *p; size_t z; } m[2] = {{"MEM1", g_mem1, g_mem1_size}, {"MEM2", g_mem2, g_mem2_size}};
    for (int i = 0; i < 2; i++) {
        if (!m[i].p) continue;
        wchar_t f[MAX_PATH]; _snwprintf(f, MAX_PATH, L"%ls\\%02d_%hs.bin", dir, g_dump_n, m[i].n);
        FILE *o = _wfopen(f, L"wb"); if (!o) continue;
        fwrite(m[i].p, 1, m[i].z, o); fclose(o);
    }
    XINPUT_GAMEPAD *p = &g_xi.Gamepad;
    logf_("VOLCADO %02d | fotograma %ld | izq %.2f %.2f der %.2f %.2f LT %.2f RT %.2f botones %04X | puntero %.3f %.3f | modo %s",
          g_dump_n, g_frames, g_ls[0], g_ls[1], g_rs[0], g_rs[1], g_lt, g_rt, p->wButtons, g_px, g_py, g_kbmode ? "teclado" : "mando");
    emit("DUMP %d", g_dump_n);
    MessageBeep(MB_OK);
}
/* Traer la ventana al frente de verdad (Windows bloquea SetForegroundWindow desde un proceso en segundo plano):
 * se une la entrada al hilo de la ventana delantera y se sube la ventana por encima de todo un instante. */
static void bring_front(void)
{
    HWND fgw = GetForegroundWindow();
    DWORD me = GetCurrentThreadId(), fg = fgw ? GetWindowThreadProcessId(fgw, NULL) : 0;
    bool att = fg && fg != me && AttachThreadInput(me, fg, TRUE);
    ShowWindow(g_hwnd, IsIconic(g_hwnd) ? SW_RESTORE : SW_SHOW);
    SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetWindowPos(g_hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    BringWindowToTop(g_hwnd); SetForegroundWindow(g_hwnd); SetActiveWindow(g_hwnd); SetFocus(g_hwnd);
    if (att) AttachThreadInput(me, fg, FALSE);
}
static void vsync_update(bool started)
{
    if (!g_swapint) return;
    int want = a_vsync && started && !g_paused && GetForegroundWindow() == g_hwnd && !IsIconic(g_hwnd);
    if (want == g_vsync_on) return;
    g_vsync_on = want;
    ((BOOL (WINAPI *)(int))g_swapint)(want);
    g_vsync_paced = want && g_hz >= 59 && g_hz <= 61;
    logf_("V-Sync %s%s", want ? "activo" : "en pausa", want ? (g_vsync_paced ? " (ritmo del monitor)" : " (ritmo del limitador)") : "");
}
static void wr32(uint32_t addr, uint32_t v)
{
    uint32_t o = addr & 0x1FFFFFF;
    if (!g_mem1 || o + 4 > g_mem1_size) return;
    uint8_t *p = g_mem1 + o; p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}
static void wrf(uint32_t addr, float f) { uint32_t v; memcpy(&v, &f, 4); wr32(addr, v); }

/* ---- mod de juego (parche_juego_eur.json): movimiento 360 + cámara con el stick derecho ----
 * El parche deja 64 bytes compartidos en MOD_SH: +0 stick X, +4 stick Y (floats), +8 activar, +12 contador
 * que el juego sube cada vez que actualiza su cámara (así sabemos si estamos jugando o en un menú/escena). */
#define MOD_SH 0x8025ECB0u
static bool g_mod_ok;
static uint32_t g_cam_stamp;
static int g_cam_idle = 99;
static bool g_cam_consumed = true;   /* el juego ya usó el último giro escrito */
static void pointer_follow(void);
static void mods_pre(void)
{
    if (!g_mem1) return;
    bool ok = rd32(0x8000B240) == 0x48253710u && rd32(0x8000B06C) == 0x48253894u;
    if (ok != g_mod_ok) { g_mod_ok = ok; logf_("mod de cámara: %s", ok ? "parche detectado" : "parche no encontrado"); }
    if (!ok) { g_cam_live = false; return; }
    float x = 0, y = 0;
    /* el parche gira "por fotograma del juego": a 60 FPS (intervalo de imagen 1, 0x80412268) hay el doble de
     * fotogramas, así que el stick se escala a la mitad para que la velocidad sea la misma que a 30 */
    uint32_t iv = rd32(0x80412268); float sk = (iv == 1) ? 0.5f : 1.0f;
    if (g_cam_live) {
        x = copysignf(curve16(fabsf(g_rs[0])), g_rs[0]) * sk;
        y = copysignf(curve16(fabsf(g_rs[1])), g_rs[1]) * sk;
        /* teclado y ratón: mirar con el ratón. Píxeles -> giro por fotograma (el parche gira yaw 0,05 rad, pitch 0,035 rad y
         * altura 0,08 m por unidad). Suavizado ligero para que se sienta fluido. Las teclas IJKL tienen prioridad. */
        if (g_mlook && x == 0 && y == 0) {
            /* el movimiento del ratón se acumula hasta que el juego lo consume (su contador de cámara avanza):
             * a 30 FPS el juego lee uno de cada dos fotogramas del motor y así no se pierde nada */
            static float sx, sy;
            if (g_cam_consumed) { sx = 0; sy = 0; g_cam_consumed = false; }
            sx += g_mdx; sy += g_mdy;
            g_mdx = g_mdy = 0;
            const float YAW = 0.0030f * a_msens, PITCH = 0.0024f * a_msens, HGT = 0.0045f * a_msens;
            bool fp = rd32(0x80413A18) == 1;              /* modo de cámara: 1 = primera persona */
            float inv = a_mouseinv ? -1.f : 1.f;
            x = clampf(sx * YAW, -0.25f, 0.25f) / 0.05f;
            y = fp ? clampf(-sy * inv * PITCH, -0.15f, 0.15f) / 0.035f : clampf(-sy * inv * HGT, -0.12f, 0.12f) / 0.08f;
        }
    } else { g_mdx = g_mdy = 0; }
    wrf(MOD_SH + 0, x); wrf(MOD_SH + 4, y); wr32(MOD_SH + 8, 1);
    /* +56: con mando y fuera del juego (menú de título, menús, escenas) el puntero del Wiimote se da por "fuera de
     * pantalla" y el juego no dibuja el cursor; con teclado y ratón el cursor sigue al ratón */
    wr32(MOD_SH + 56, (!g_kbmode && !g_cam_live) ? 1 : 0);
    pointer_follow();
}
/* Ratón = cursor del juego. Con el mando clásico emulado, el parche de Vague Rant arma la posición del cursor con el
 * stick derecho y el puntero IR del core no llega nunca. El parche de juego (gancho tras KPADRead, 0x80110CAC) copia
 * la posición que dejamos en MOD_SH-16 (activar, x, y; -1..1) en KPADStatus.pos. Gestor del mando: 0x8039C6B0. */
#define PAD0 0x8039C6B0u
/* seno/coseno propios (sinf/cosf de msvcrt se importan como datos y el enlazador avisa) */
static float fsin(float x)
{
    const float PI = 3.14159265f, TAU = 6.28318531f;
    x = x - TAU * (float)(int)(x / TAU); if (x > PI) x -= TAU; if (x < -PI) x += TAU;
    if (x > PI / 2) x = PI - x; else if (x < -PI / 2) x = -PI - x;
    float x2 = x * x; return x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72))));
}
static float fcos(float x) { return fsin(x + 1.57079633f); }
static float rdf(uint32_t a) { uint32_t v = rd32(a); float f; memcpy(&f, &v, 4); return f; }
/* punto delante de Seto -> coordenadas del puntero (-1..1, y hacia abajo) */
static bool light_target(float *ox, float *oy)
{
    float yaw = rdf(0x8030D464), pxw = rdf(0x8030D454), pyw = rdf(0x8030D458), pzw = rdf(0x8030D45C);
    float m[12]; for (int i = 0; i < 12; i++) m[i] = rdf(0x80308460 + 4 * i);   /* matriz de la cámara (filas 3x4) */
    float p00 = rdf(0x803084E0), p11 = rdf(0x803084F4);
    if (!(p00 > 0.2f && p00 < 10 && p11 > 0.2f && p11 < 10) || !(yaw == yaw)) return false;
    /* si Seto mira hacia la cámara, el punto a 2,5 m queda debajo de ella: se acerca hasta que entre en pantalla */
    static const float DS[] = {2.5f, 1.8f, 1.3f, 0.9f, 0.6f};
    float bx = 0, by = 0; bool any = false;
    for (int k = 0; k < 5; k++) {
        float tx = pxw + fsin(yaw) * DS[k], ty = pyw + 0.3f, tz = pzw + fcos(yaw) * DS[k];
        float vx = tx - m[3], vy = ty - m[7], vz = tz - m[11];
        float xc = vx * m[0] + vy * m[4] + vz * m[8];     /* eje X de la cámara = columna 0 */
        float yc = vx * m[1] + vy * m[5] + vz * m[9];
        float zc = -(vx * m[2] + vy * m[6] + vz * m[10]); /* profundidad (la cámara mira hacia -Z) */
        if (zc < 0.3f) continue;
        bx = p00 * xc / zc; by = -p11 * yc / zc; any = true;
        if (fabsf(bx) <= 0.6f && by <= 0.85f) break;
    }
    if (!any) return false;
    *ox = clampf(bx, -0.6f, 0.6f);           /* lejos de los bordes: ahí el juego giraría la cámara */
    *oy = clampf(by, -0.6f, 0.85f);
    return true;
}
static void pointer_follow(void)
{
    if (!g_mod_ok) return;
    bool on = g_kbmode && !g_paused;
    g_game_cursor = on;                         /* con teclado/ratón solo se ve el cursor del juego */
    if (on && !g_mlook) { wr32(MOD_SH - 16, 1); wrf(MOD_SH - 12, g_mx); wrf(MOD_SH - 8, g_my); }   /* menús: cursor = ratón */
    else {
        /* Mando, 3ª persona: la linterna sigue al puntero del Wii (rayo desde la cámara). Con el puntero al centro alumbraba
         * siempre hacia donde mira la cámara, aunque Seto mirara hacia ella. Se apunta el puntero a un punto del suelo
         * 2,5 m delante de Seto, proyectado con la matriz de la cámara: la luz va hacia donde él mira. */
        float px, py;
        bool fpm = rd32(0x80413A18) == 1;
        bool lamp = !g_paused && g_cam_live && !fpm && light_target(&px, &py);
        if (!lamp && g_mlook && fpm) { lamp = true; px = py = 0; }      /* 1ª persona con ratón: mira al centro */
        if (lamp) {
            static float sx, sy; static bool init;
            if (!init) { sx = px; sy = py; init = true; }
            sx += (px - sx) * 0.25f; sy += (py - sy) * 0.25f;          /* suavizado */
            wr32(MOD_SH - 16, 1); wrf(MOD_SH - 12, sx); wrf(MOD_SH - 8, sy);
        } else wr32(MOD_SH - 16, 0);
    }
    { static double lt; static int n; double tt = now_s();   /* diagnóstico: 1 línea cada 2 s con teclado/ratón (máx. 30) */
      if (on && n < 30 && tt - lt > 2) { lt = tt; n++;
        uint32_t a = rd32(PAD0 + 464), b2 = rd32(PAD0 + 468), c = rd32(PAD0 + 3288), d = rd32(PAD0 + 3292);
        float qx, qy, sx, sy; memcpy(&qx, &a, 4); memcpy(&qy, &b2, 4); memcpy(&sx, &c, 4); memcpy(&sy, &d, 4);
        logf_("cursor: ratón %.2f %.2f | KPAD %.2f %.2f | pantalla %.1f %.1f | válido %d | menú %d",
              g_mx, g_my, qx, qy, sx, sy, (int)rd32(PAD0 + 420), !g_cam_live); } }
}
/* ---- F10: traza para analizar los 60 FPS: 90 llamadas a 60 FPS y luego 90 a 30 FPS, guardando cada vez la
 * estructura del jugador (48 KB) y la cámara; sale en motor\volcados\traza_NN.bin ---- */
#define TR_N 90
#define TR_PL 0xC000
#define TR_CAM 0x200
#define TR_REC (16 + TR_PL + TR_CAM)
static int g_tr_state;              /* 0 nada, 1 grabando a 60, 2 esperando a 30, 3 grabando a 30 */
static int g_tr_i, g_tr_wait, g_tr_n;
static uint8_t *g_tr_buf;
static void tr_snap(int slot)
{
    uint8_t *r = g_tr_buf + (size_t)slot * TR_REC;
    uint32_t h[4] = { (uint32_t)g_frames, rd32(0x80412268), rd32(MOD_SH + 12), (uint32_t)slot };
    memcpy(r, h, 16);
    if (g_mem1) { memcpy(r + 16, g_mem1 + (0x80309BC0u & 0x1FFFFFF), TR_PL); memcpy(r + 16 + TR_PL, g_mem1 + (0x80308420u & 0x1FFFFFF), TR_CAM); }
}
static void trace_step(void)
{
    static bool f10p; bool f10 = g_kb[VK_F10] && g_focus;
    if (f10 && !f10p && !g_tr_state && g_mem1) {
        if (!g_tr_buf) g_tr_buf = malloc((size_t)TR_REC * TR_N * 2);
        if (g_tr_buf) { g_tr_state = 1; g_tr_i = 0; logf_("TRAZA: grabando (intervalo %u)", rd32(0x80412268)); MessageBeep(MB_OK); }
    }
    f10p = f10;
    if (!g_tr_state) return;
    if (g_tr_state == 1) { tr_snap(g_tr_i++); if (g_tr_i >= TR_N) { g_tr_state = 2; g_tr_wait = 30; wr32(0x80412268, 2); } }
    else if (g_tr_state == 2) { wr32(0x80412268, 2); if (--g_tr_wait <= 0) g_tr_state = 3; }
    else if (g_tr_state == 3) {
        wr32(0x80412268, 2); tr_snap(g_tr_i++);
        if (g_tr_i >= 2 * TR_N) {
            g_tr_state = 0;
            wchar_t dir[MAX_PATH]; GetModuleFileNameW(NULL, dir, MAX_PATH);
            wchar_t *sl = wcsrchr(dir, L'\\'); if (sl) *sl = 0;
            wcscat(dir, L"\\volcados"); CreateDirectoryW(dir, NULL);
            wchar_t f[MAX_PATH]; _snwprintf(f, MAX_PATH, L"%ls\\traza_%02d.bin", dir, ++g_tr_n);
            FILE *o = _wfopen(f, L"wb"); if (o) { fwrite(g_tr_buf, TR_REC, 2 * TR_N, o); fclose(o); }
            logf_("TRAZA %02d guardada", g_tr_n); MessageBeep(MB_ICONASTERISK);
        }
    }
}

static void mods_post(void)
{
    if (!g_mod_ok) return;
    uint32_t st = rd32(MOD_SH + 12);
    if (st != g_cam_stamp) { g_cam_stamp = st; g_cam_idle = 0; g_cam_consumed = true; } else if (g_cam_idle < 99) g_cam_idle++;
    bool live = g_cam_idle < 4;
    /* 60 FPS solo jugando: el juego usa intervalo de imagen 2 (30 fps) en el campo; mientras la cámara del jugador
     * está activa se pone a 1 (60 fps; la lógica se escala sola con 300/fps). En escenas, eventos y menús se devuelve
     * el 2: los eventos cuentan fotogramas y a 60 se trababan (escena inicial). Solo se toca si el juego tiene 2. */
    trace_step();
    if (!g_tr_state) { static bool over;
      uint32_t iv = rd32(0x80412268);
      if (a_fps60 && live && iv == 2) { wr32(0x80412268, 1); if (!over) logf_("60 FPS: activo (jugando)"); over = true; }
      else if (over && (!live || !a_fps60) && iv == 1) { wr32(0x80412268, 2); over = false; logf_("60 FPS: vuelve a 30 (escena o menú)"); }
      else if (over && iv != 1) over = false; }
    if (live != g_cam_live) { g_cam_live = live; logf_("mod de cámara: %s", live ? "en juego" : "fuera de juego (menú o escena)"); }
}

static void mods_frame(void)
{
    static bool f9p;
    bool f9 = g_kb[VK_F9];
    if (f9 && !f9p) dump_memory();
    f9p = f9;
    static double last;
    double t = now_s();
    if (g_mem1 && t - last > 60) { last = t; logf_("fps aprox. %ld fotogramas | MEM1 0x80000000 = %08X", g_frames, rd32(0x80000000)); }
}

/* ------------------------------------------------------------------ órdenes del launcher (stdin) */
static DWORD WINAPI stdin_thread(LPVOID p)
{
    (void)p;
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    char buf[512]; int len = 0;
    for (;;) {
        char ch; DWORD r;
        if (!ReadFile(in, &ch, 1, &r, NULL) || r == 0) {
            /* el launcher se cerró (o murió): sin él no hay pausa ni salida, así que el motor también se cierra */
            if (GetFileType(in) == FILE_TYPE_PIPE) { logf_("el launcher cerró la conexión: saliendo"); InterlockedExchange(&g_quit, 1); }
            break;
        }
        if (ch == '\r') continue;
        if (ch != '\n') { if (len < 511) buf[len++] = ch; continue; }
        buf[len] = 0; len = 0;
        logf_("< %s", buf);
        if (!strcmp(buf, "QUIT")) InterlockedExchange(&g_quit, 1);
        else if (!strcmp(buf, "PAUSE")) { g_paused = true; if (g_wo) waveOutPause(g_wo); ClipCursor(NULL); clipped = false; }
        else if (!strcmp(buf, "RESUME")) { g_paused = false; if (g_wo) waveOutRestart(g_wo); PostMessageW(g_hwnd, WM_USER + 1, 0, 0); }
        else if (!strncmp(buf, "VOLUME ", 7)) { int v = atoi(buf + 7); g_volume = clampf(v / 100.0f, 0, 1); }
        else if (!strcmp(buf, "FOCUS")) PostMessageW(g_hwnd, WM_USER + 1, 0, 0);
    }
    return 0;
}

/* ------------------------------------------------------------------ arranque */
static bool load_core(void)
{
    wchar_t *wpath = u2w(a_core);
    /* las DLL que necesite el core se buscan junto a él */
    wchar_t dir[MAX_PATH]; wcsncpy(dir, wpath, MAX_PATH - 1); dir[MAX_PATH - 1] = 0;
    wchar_t *sl = wcsrchr(dir, L'\\'); if (sl) { *sl = 0; SetDllDirectoryW(dir); }
    g_core = LoadLibraryW(wpath);
    if (!g_core) { emit("ERROR no se pudo cargar el core (%lu)", GetLastError()); return false; }
#define LOADC(n) if (!(p_##n = (void *)GetProcAddress(g_core, #n))) { emit("ERROR falta %s en el core", #n); return false; }
    LOADC(retro_set_environment) LOADC(retro_set_video_refresh) LOADC(retro_set_audio_sample)
    LOADC(retro_set_audio_sample_batch) LOADC(retro_set_input_poll) LOADC(retro_set_input_state)
    LOADC(retro_init) LOADC(retro_deinit) LOADC(retro_api_version) LOADC(retro_get_system_info)
    LOADC(retro_get_system_av_info) LOADC(retro_set_controller_port_device) LOADC(retro_run)
    LOADC(retro_load_game) LOADC(retro_unload_game) LOADC(retro_get_memory_data) LOADC(retro_get_memory_size)
    return true;
}

static int run(void)
{
    int argc; wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i + 1 < argc; i++) {
        char *v = w2u(argv[i + 1]);
        if (!wcscmp(argv[i], L"--core")) a_core = v, i++;
        else if (!wcscmp(argv[i], L"--game")) a_game = v, i++;
        else if (!wcscmp(argv[i], L"--system")) a_system = v, i++;
        else if (!wcscmp(argv[i], L"--save")) a_save = v, i++;
        else if (!wcscmp(argv[i], L"--opts")) a_opts = v, i++;
        else if (!wcscmp(argv[i], L"--log")) a_log = v, i++;
        else if (!wcscmp(argv[i], L"--title")) a_title = v, i++;
        else if (!wcscmp(argv[i], L"--fullscreen")) a_full = atoi(v), i++;
        else if (!wcscmp(argv[i], L"--width")) a_w = atoi(v), i++;
        else if (!wcscmp(argv[i], L"--height")) a_h = atoi(v), i++;
        else if (!wcscmp(argv[i], L"--volume")) a_vol = atoi(v), i++;
        else if (!wcscmp(argv[i], L"--vsync")) a_vsync = atoi(v), i++;
        else if (!wcscmp(argv[i], L"--controls")) a_controls = v, i++;
    }
    InitializeCriticalSection(&g_logcs);
    if (a_log) g_log = _wfopen(u2w(a_log), L"w");
    g_out = GetStdHandle(STD_OUTPUT_HANDLE);
    logf_("fd_motor 1.0 | core %s | juego %s", a_core ? a_core : "?", a_game ? a_game : "?");
    if (!a_core || !a_game || !a_system || !a_save) { emit("ERROR faltan argumentos (--core --game --system --save)"); return 2; }
    g_sysdir_u = a_system; g_savedir_u = a_save;
    g_volume = clampf(a_vol / 100.0f, 0, 1);
    timeBeginPeriod(1);
    load_opts_file(a_opts);
    binds_load();
    if (!load_core()) return 3;

    p_retro_set_environment(cb_env);
    p_retro_set_video_refresh(cb_video);
    p_retro_set_audio_sample(cb_audio);
    p_retro_set_audio_sample_batch(cb_audio_batch);
    p_retro_set_input_poll(cb_input_poll);
    p_retro_set_input_state(cb_input_state);
    struct retro_system_info si = {0};
    p_retro_get_system_info(&si);
    logf_("core: %s %s (API %u)", si.library_name, si.library_version, p_retro_api_version());
    p_retro_init();

    struct retro_game_info gi = {0};
    gi.path = a_game;
    double t0 = now_s(); g_t0 = t0;
    if (!p_retro_load_game(&gi)) { emit("ERROR el core no pudo cargar el juego"); return 4; }
    logf_("juego cargado en %.2f s", now_s() - t0);
    if (!g_hw_ok) { emit("ERROR el core no pidió render OpenGL"); return 5; }

    cursor_create();
    if (!window_create()) { emit("ERROR no se pudo crear la ventana OpenGL"); return 6; }
    p_retro_get_system_av_info(&g_av);
    if (g_av.geometry.aspect_ratio > 0) g_aspect = g_av.geometry.aspect_ratio;
    logf_("AV inicial %.3f fps %.0f Hz %ux%u (máx %ux%u)", g_av.timing.fps, g_av.timing.sample_rate, g_av.geometry.base_width, g_av.geometry.base_height, g_av.geometry.max_width, g_av.geometry.max_height);
    fbo_create(g_av.geometry.max_width ? g_av.geometry.max_width : 640, g_av.geometry.max_height ? g_av.geometry.max_height : 528);
    audio_open((unsigned)(g_av.timing.sample_rate + 0.5));
    if (g_hw.context_reset) g_hw.context_reset();
    p_retro_set_controller_port_device(0, (4 << 8) | RETRO_DEVICE_JOYPAD);   /* Wiimote + mando clásico */
    CreateThread(NULL, 0, stdin_thread, NULL, 0, NULL);
    emit("READY");
    if (a_mode == 2) set_kbmode(true);

    double fps = g_av.timing.fps > 1 ? g_av.timing.fps : 60.0;
    double next = now_s(), last = next;
    bool started = false;
    MSG msg;
    while (!g_quit) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_USER + 1) { bring_front(); continue; }
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        if (g_quit) break;
        if (g_paused) { Sleep(10); next = now_s(); last = next; continue; }
        double t = now_s(), dt = t - last; last = t; if (dt > 0.1) dt = 0.1;
        input_poll_all(dt);
        if (!g_mem1) { g_mem1 = (uint8_t *)p_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM); g_mem1_size = p_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM); if (g_mem1) logf_("MEM1 %p (%zu bytes)", g_mem1, g_mem1_size); }
        mods_pre();
        p_retro_run();
        mods_post();
        mods_frame();
        /* STARTED cuando el juego ya dibuja con fluidez (no con la primera imagen suelta): el launcher se funde entonces */
        if (!started && g_frames >= 15) { started = true; emit("STARTED"); logf_("primeras imágenes a los %.1f s", now_s() - g_t0); }
        { static double vt; double tt = now_s(); if (tt - vt > 0.25) { vt = tt; vsync_update(started); } }
        /* en ventana / sin bordes: mientras el juego es la ventana activa va "siempre encima" (que nada lo tape); si el
         * usuario cambia a otra app (Alt+Tab, clic fuera) se le quita, para no estorbar */
        if (a_full != 1 && started) { static double tt0; static int top = -1; double tt = now_s();
          if (tt - tt0 > 0.25) { tt0 = tt;
            HWND fgw = GetForegroundWindow(); DWORD fpid = 0; if (fgw) GetWindowThreadProcessId(fgw, &fpid);
            int want = fgw == g_hwnd && !g_paused;
            if (!want && fpid == GetCurrentProcessId()) want = top;      /* ventanas propias: sin cambios */
            if (want != top) { top = want;
              SetWindowPos(g_hwnd, want ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
              logf_("ventana %s", want ? "siempre encima (activa)" : "normal (otra app activa o pausa)"); } } }
        /* el launcher se oculta ~3 s después de STARTED: durante 8 s, si la ventana del juego quedó detrás, se trae al frente */
        { static double t_st, last_bf; double tt = now_s();
          if (started && !t_st) t_st = tt;
          if (t_st && tt - t_st > 2.5 && tt - t_st < 10.5 && !g_paused && GetForegroundWindow() != g_hwnd && tt - last_bf > 0.5) {
              last_bf = tt; bring_front(); logf_("ventana del juego traída al frente"); } }
        { static int nlog; static double lt; double tt = now_s();   /* diagnóstico de arranque: 6 líneas, una cada 5 s */
          if (nlog < 6 && tt - lt > 5) { lt = tt; nlog++; logf_("fotogramas: %ld | ventana %s", g_frames, GetForegroundWindow() == g_hwnd ? "al frente" : "detrás"); } }
        /* limitador: el ritmo del juego (60 Hz). Si el audio ya va lleno, también frena él. */
        fps = g_av.timing.fps > 1 ? g_av.timing.fps : 60.0;
        next += 1.0 / fps;
        double wait = next - now_s();
        if (g_vsync_paced) wait = 0;   /* SwapBuffers ya esperó al refresco del monitor */
        if (wait > 0.002) Sleep((DWORD)((wait - 0.0015) * 1000));
        if (!g_vsync_paced) while (now_s() < next) ;
        if (now_s() - next > 0.25) next = now_s();
    }
    emit("QUIT");
    logf_("saliendo (%ld fotogramas)", g_frames);
    p_retro_unload_game();
    p_retro_deinit();
    if (g_wo) { waveOutReset(g_wo); waveOutClose(g_wo); }
    timeEndPeriod(1);
    return 0;
}

void fd_entry(void)
{
    int r = run();
    if (g_log) fclose(g_log);
    ExitProcess(r);
}
