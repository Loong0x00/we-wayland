/*
 * test-enumall.c - Enumerate ALL windows to see Z-order
 */
#include <windows.h>
#include <stdio.h>

static int g_index = 0;

static BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lParam)
{
    char cls[256] = {0};
    char title[256] = {0};
    GetClassNameA(hwnd, cls, sizeof(cls));
    GetWindowTextA(hwnd, title, sizeof(title));

    RECT rc;
    GetWindowRect(hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;

    /* Only print interesting windows (not tiny 1x1) */
    if (w > 10 || h > 10 ||
        strcmp(cls, "Progman") == 0 ||
        strcmp(cls, "WorkerW") == 0 ||
        strcmp(cls, "SHELLDLL_DefView") == 0) {
        printf("[%3d] hwnd=%p class='%s' title='%s' %dx%d\n",
               g_index, hwnd, cls, title, w, h);

        /* Check for SHELLDLL_DefView child */
        HWND sv = FindWindowExA(hwnd, NULL, "SHELLDLL_DefView", NULL);
        if (sv) printf("      ^^ has SHELLDLL_DefView child %p\n", sv);
    }
    g_index++;
    return TRUE;
}

int main(void)
{
    printf("=== EnumWindows order (Z-order top to bottom) ===\n");
    EnumWindows(EnumProc, 0);
    printf("=== Total: %d windows ===\n\n", g_index);

    /* Try FindWindowEx in both directions */
    HWND progman = FindWindowA("Progman", NULL);
    printf("Progman = %p\n", progman);

    if (progman) {
        /* Forward: find WorkerW AFTER Progman */
        HWND ww1 = FindWindowExA(NULL, progman, "WorkerW", NULL);
        printf("FindWindowEx(NULL, Progman, WorkerW) = %p\n", ww1);

        /* Reverse: find WorkerW BEFORE Progman (above in Z) */
        HWND ww2 = FindWindowExA(NULL, NULL, "WorkerW", NULL);
        printf("FindWindowEx(NULL, NULL, WorkerW)    = %p [first WorkerW]\n", ww2);

        /* Find WorkerW anywhere */
        HWND ww3 = FindWindowA("WorkerW", NULL);
        printf("FindWindow(WorkerW, NULL)            = %p\n", ww3);
    }

    return 0;
}
