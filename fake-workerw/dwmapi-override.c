/*
 * dwmapi-override.c — Custom dwmapi.dll for Wallpaper Engine on Wine/Proton
 *
 * 1. Fakes DWM/Aero being enabled
 * 2. Creates a "Progman" window hierarchy on first use so that
 *    WE's FindWindowW("Progman") succeeds (Wine has no Progman)
 *
 * Log: Z:\tmp\dwmapi-trace.log (Linux: /tmp/dwmapi-trace.log)
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

static FILE *g_log = NULL;
static HWND g_progman       = NULL;
static HWND g_workerW_icons = NULL;   /* WorkerW with SHELLDLL_DefView */
static HWND g_workerW_render = NULL;  /* WorkerW render target (WE draws here) */
static BOOL g_desktop_init_done = FALSE;

static void trace(const char *fmt, ...)
{
    if (!g_log) {
        g_log = fopen("Z:\\tmp\\dwmapi-trace.log", "a");
        if (!g_log) g_log = fopen("C:\\dwmapi-trace.log", "a");
        if (g_log) {
            fprintf(g_log, "=== dwmapi-override loaded ===\n");
            fflush(g_log);
        }
    }
    if (g_log) {
        va_list ap;
        va_start(ap, fmt);
        vfprintf(g_log, fmt, ap);
        va_end(ap);
        fprintf(g_log, "\n");
        fflush(g_log);
    }
}

/* ── Fake desktop window hierarchy ──────────────────────────────── */

static LRESULT CALLBACK FakeProgmanProc(HWND hwnd, UINT msg,
                                         WPARAM wp, LPARAM lp)
{
    /* WE may send 0x052C to spawn WorkerW — just acknowledge */
    if (msg == 0x052C)
        return 0;

    return DefWindowProcW(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK FakeWorkerWProc(HWND hwnd, UINT msg,
                                         WPARAM wp, LPARAM lp)
{
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void ensure_desktop_windows(void)
{
    if (g_desktop_init_done) return;
    g_desktop_init_done = TRUE;

    HINSTANCE hInst = GetModuleHandleW(NULL);
    int w = GetSystemMetrics(SM_CXSCREEN);
    int h = GetSystemMetrics(SM_CYSCREEN);

    trace("Creating Progman hierarchy (%dx%d)...", w, h);

    /*
     * WE's callback (EnumChildWindows on desktop) looks for:
     *   1. A WorkerW with a SHELLDLL_DefView child  (= WorkerW-icons)
     *   2. The NEXT WorkerW in Z-order going BACKWARD (= render target)
     *
     * Required Z-order (front → back):
     *   Progman  →  WorkerW-icons  →  WorkerW-render
     *
     * FindWindowExW(NULL, WorkerW-icons, "WorkerW") searches backward
     * from icons → finds WorkerW-render ✓
     *
     * HWND_BOTTOM puts a window at the VERY back each time, so we must
     * call SetWindowPos in order: Progman first, icons next, render last.
     */

    /* ── Register window classes ── */
    WNDCLASSW wc = {0};
    wc.lpfnWndProc   = FakeWorkerWProc;
    wc.hInstance      = hInst;
    wc.lpszClassName  = L"WorkerW";
    wc.hbrBackground  = (HBRUSH)GetStockObject(BLACK_BRUSH);
    if (!RegisterClassW(&wc))
        trace("WorkerW RegisterClass err=%lu", GetLastError());

    WNDCLASSW pc = {0};
    pc.lpfnWndProc   = FakeProgmanProc;
    pc.hInstance      = hInst;
    pc.lpszClassName  = L"Progman";
    pc.hbrBackground  = (HBRUSH)GetStockObject(BLACK_BRUSH);
    if (!RegisterClassW(&pc))
        trace("Progman RegisterClass err=%lu", GetLastError());

    /* ── Create all windows (initially hidden to avoid flicker) ── */

    /* Progman */
    g_progman = CreateWindowExW(
        WS_EX_NOACTIVATE,
        L"Progman", L"Program Manager",
        WS_POPUP,            /* NOT visible yet */
        0, 0, w, h,
        NULL, NULL, hInst, NULL);
    trace("Progman: hwnd=%p", (void*)g_progman);

    /* WorkerW-icons (will hold SHELLDLL_DefView) */
    g_workerW_icons = CreateWindowExW(
        WS_EX_NOACTIVATE,
        L"WorkerW", L"",
        WS_POPUP,
        0, 0, w, h,
        NULL, NULL, hInst, NULL);
    trace("WorkerW-icons: hwnd=%p", (void*)g_workerW_icons);

    /* SHELLDLL_DefView inside WorkerW-icons */
    HWND defView = NULL;
    if (g_workerW_icons) {
        WNDCLASSW sc = {0};
        sc.lpfnWndProc   = DefWindowProcW;
        sc.hInstance      = hInst;
        sc.lpszClassName  = L"SHELLDLL_DefView";
        RegisterClassW(&sc);

        defView = CreateWindowExW(
            0, L"SHELLDLL_DefView", L"",
            WS_CHILD | WS_VISIBLE,
            0, 0, w, h,
            g_workerW_icons, NULL, hInst, NULL);

        /* SysListView32 inside SHELLDLL_DefView */
        if (defView) {
            WNDCLASSW lc = {0};
            lc.lpfnWndProc   = DefWindowProcW;
            lc.hInstance      = hInst;
            lc.lpszClassName  = L"SysListView32";
            RegisterClassW(&lc);

            CreateWindowExW(
                0, L"SysListView32", L"FolderView",
                WS_CHILD | WS_VISIBLE,
                0, 0, w, h,
                defView, NULL, hInst, NULL);
        }
    }

    /* WorkerW-render (render target — WE draws here) */
    g_workerW_render = CreateWindowExW(
        WS_EX_NOACTIVATE,
        L"WorkerW", L"",
        WS_POPUP,
        0, 0, w, h,
        NULL, NULL, hInst, NULL);
    trace("WorkerW-render: hwnd=%p", (void*)g_workerW_render);

    /* ── Set Z-order: Progman(front) → icons → render(back) ──
     * Each HWND_BOTTOM call pushes that window to the very back.
     * So call order: Progman first → icons → render last.
     */
    if (g_progman)
        SetWindowPos(g_progman, HWND_BOTTOM, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (g_workerW_icons)
        SetWindowPos(g_workerW_icons, HWND_BOTTOM, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (g_workerW_render)
        SetWindowPos(g_workerW_render, HWND_BOTTOM, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    /* Now show them (after Z-order is correct) */
    if (g_progman)
        ShowWindow(g_progman, SW_SHOWNOACTIVATE);
    if (g_workerW_icons)
        ShowWindow(g_workerW_icons, SW_SHOWNOACTIVATE);
    if (g_workerW_render)
        ShowWindow(g_workerW_render, SW_SHOWNOACTIVATE);

    trace("Desktop hierarchy ready. Progman=%p Icons=%p Render=%p",
          (void*)g_progman, (void*)g_workerW_icons, (void*)g_workerW_render);
}

/* ── DLL entry ──────────────────────────────────────────────────── */

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
        trace("DllMain: DLL_PROCESS_ATTACH pid=%lu", GetCurrentProcessId());
    return TRUE;
}

/* ── DWM stubs ──────────────────────────────────────────────────── */

HRESULT WINAPI DwmIsCompositionEnabled(BOOL *pfEnabled)
{
    /* Lazy-create Progman on first DWM call (before WE's FindWindowW) */
    ensure_desktop_windows();
    trace("DwmIsCompositionEnabled → TRUE");
    if (!pfEnabled) return (HRESULT)0x80070057L;
    *pfEnabled = TRUE;
    return 0;
}

HRESULT WINAPI DwmEnableComposition(UINT uCompositionAction)
{
    trace("DwmEnableComposition(%u)", uCompositionAction);
    return 0;
}

HRESULT WINAPI DwmGetWindowAttribute(HWND hwnd, DWORD dwAttribute,
                                      PVOID pvAttribute, DWORD cbAttribute)
{
    trace("DwmGetWindowAttribute(hwnd=%p, attr=%lu, cbAttr=%lu)",
          (void*)hwnd, dwAttribute, cbAttribute);
    if (!pvAttribute || cbAttribute == 0) return (HRESULT)0x80070057L;
    memset(pvAttribute, 0, cbAttribute);
    return 0;
}

HRESULT WINAPI DwmSetWindowAttribute(HWND hwnd, DWORD dwAttribute,
                                      LPCVOID pvAttribute, DWORD cbAttribute)
{
    DWORD val = 0;
    if (pvAttribute && cbAttribute >= 4) val = *(const DWORD*)pvAttribute;
    trace("DwmSetWindowAttribute(hwnd=%p, attr=%lu, val=%lu, cb=%lu)",
          (void*)hwnd, dwAttribute, val, cbAttribute);
    return 0;
}

HRESULT WINAPI DwmExtendFrameIntoClientArea(HWND hWnd, const void *pMarInset)
{
    trace("DwmExtendFrameIntoClientArea(hwnd=%p)", (void*)hWnd);
    return 0;
}

HRESULT WINAPI DwmEnableBlurBehindWindow(HWND hWnd, const void *pBlurBehind)
{
    trace("DwmEnableBlurBehindWindow(hwnd=%p)", (void*)hWnd);
    return 0;
}

HRESULT WINAPI DwmFlush(void)
{
    trace("DwmFlush()");
    return 0;
}

HRESULT WINAPI DwmGetColorizationColor(DWORD *pcrColorization, BOOL *pfOpaqueBlend)
{
    trace("DwmGetColorizationColor()");
    if (pcrColorization) *pcrColorization = 0x6B74B8FC;
    if (pfOpaqueBlend) *pfOpaqueBlend = FALSE;
    return 0;
}

HRESULT WINAPI DwmDefWindowProc(HWND hWnd, UINT msg, WPARAM wParam,
                                 LPARAM lParam, LRESULT *plResult)
{
    trace("DwmDefWindowProc(hwnd=%p, msg=0x%x)", (void*)hWnd, msg);
    if (plResult) *plResult = 0;
    return 0;
}

HRESULT WINAPI DwmGetTransportAttributes(BOOL *pfIsRemoting,
                                          BOOL *pfIsConnected,
                                          DWORD *pDwGeneration)
{
    trace("DwmGetTransportAttributes()");
    if (pfIsRemoting) *pfIsRemoting = FALSE;
    if (pfIsConnected) *pfIsConnected = TRUE;
    if (pDwGeneration) *pDwGeneration = 1;
    return 0;
}
