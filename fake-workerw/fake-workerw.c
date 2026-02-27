/*
 * fake-workerw.c
 *
 * Minimal Win32 program that fakes the Windows desktop window hierarchy
 * (Progman → SHELLDLL_DefView + WorkerW) so that Wallpaper Engine can
 * find WorkerW and inject its rendering window into it.
 *
 * Key insight: Wine's SetWindowPos doesn't reliably reorder Z-order.
 * So we create WorkerW FIRST, then Progman. Since newly created windows
 * go to the top of Z-order, the final order is:
 *   Progman (top) → WorkerW (below)
 * Which is exactly what WE's FindWindowEx(NULL, Progman, "WorkerW") needs.
 *
 * Build:
 *   x86_64-w64-mingw32-gcc -o fake-workerw.exe fake-workerw.c \
 *       -lgdi32 -luser32 -mwindows
 */

#include <windows.h>

static HWND g_progman   = NULL;
static HWND g_shellView = NULL;
static HWND g_workerW   = NULL;

static int g_screenW = 0;
static int g_screenH = 0;

/* ── WorkerW window proc ─────────────────────────────────────────── */
static LRESULT CALLBACK WorkerWProc(HWND hwnd, UINT msg,
                                    WPARAM wp, LPARAM lp)
{
    return DefWindowProc(hwnd, msg, wp, lp);
}

/* ── SHELLDLL_DefView window proc ────────────────────────────────── */
static LRESULT CALLBACK ShellViewProc(HWND hwnd, UINT msg,
                                      WPARAM wp, LPARAM lp)
{
    return DefWindowProc(hwnd, msg, wp, lp);
}

/* ── Progman window proc ─────────────────────────────────────────── */
static LRESULT CALLBACK ProgmanProc(HWND hwnd, UINT msg,
                                    WPARAM wp, LPARAM lp)
{
    /* WE sends 0x052C to Progman. WorkerW is already created,
     * just acknowledge the message. */
    if (msg == 0x052C)
        return 0;

    return DefWindowProc(hwnd, msg, wp, lp);
}

/* ── Entry point ─────────────────────────────────────────────────── */
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev,
                   LPSTR cmdLine, int show)
{
    g_screenW = GetSystemMetrics(SM_CXSCREEN);
    g_screenH = GetSystemMetrics(SM_CYSCREEN);

    /*
     * STEP 1: Create WorkerW FIRST (goes to top of Z-order).
     * When Progman is created next, it will be above WorkerW.
     * Result: Z-order = ..., Progman, WorkerW, ...
     * This is what WE's FindWindowEx(NULL, Progman, "WorkerW") needs.
     */
    WNDCLASSA wc = {0};
    wc.lpfnWndProc   = WorkerWProc;
    wc.hInstance      = hInst;
    wc.lpszClassName  = "WorkerW";
    RegisterClassA(&wc);

    g_workerW = CreateWindowExA(
        0, "WorkerW", "",
        WS_POPUP | WS_VISIBLE,
        0, 0, g_screenW, g_screenH,
        NULL, NULL, hInst, NULL);

    if (!g_workerW)
        return 1;

    /* STEP 2: Create Progman (goes to top, ABOVE WorkerW) */
    WNDCLASSA pc = {0};
    pc.lpfnWndProc   = ProgmanProc;
    pc.hInstance      = hInst;
    pc.lpszClassName  = "Progman";
    RegisterClassA(&pc);

    g_progman = CreateWindowExA(
        0, "Progman", "Program Manager",
        WS_POPUP | WS_VISIBLE,
        0, 0, g_screenW, g_screenH,
        NULL, NULL, hInst, NULL);

    if (!g_progman)
        return 1;

    /* STEP 3: Create SHELLDLL_DefView as child of Progman */
    WNDCLASSA sc = {0};
    sc.lpfnWndProc   = ShellViewProc;
    sc.hInstance      = hInst;
    sc.lpszClassName  = "SHELLDLL_DefView";
    RegisterClassA(&sc);

    g_shellView = CreateWindowExA(
        0, "SHELLDLL_DefView", "",
        WS_CHILD | WS_VISIBLE,
        0, 0, g_screenW, g_screenH,
        g_progman, NULL, hInst, NULL);

    /* Message loop */
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
