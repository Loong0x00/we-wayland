/*
 * test-findworkerw.c
 *
 * Diagnostic: simulates WE's Progman/WorkerW discovery algorithm.
 * Reports exactly what Win32 API finds.
 */

#include <windows.h>
#include <stdio.h>

static HWND g_workerW = NULL;

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
{
    char className[256] = {0};
    char title[256] = {0};

    GetClassNameA(hwnd, className, sizeof(className));
    GetWindowTextA(hwnd, title, sizeof(title));

    /* Check if this window has a SHELLDLL_DefView child */
    HWND shellView = FindWindowExA(hwnd, NULL, "SHELLDLL_DefView", NULL);
    if (shellView != NULL) {
        printf("[FOUND] Window %p class='%s' title='%s' has SHELLDLL_DefView child %p\n",
               hwnd, className, title, shellView);

        /* Find the next WorkerW after this window */
        HWND ww = FindWindowExA(NULL, hwnd, "WorkerW", NULL);
        if (ww) {
            printf("[FOUND] WorkerW %p found after window %p\n", ww, hwnd);
            g_workerW = ww;
        } else {
            printf("[MISS]  No WorkerW found after window %p\n", hwnd);
        }
    }

    return TRUE; /* continue enumeration */
}

int main(void)
{
    printf("=== Test: FindWindow(\"Progman\", NULL) ===\n");
    HWND progman = FindWindowA("Progman", NULL);
    if (progman) {
        char title[256] = {0};
        GetWindowTextA(progman, title, sizeof(title));
        printf("[FOUND] Progman: %p title='%s'\n", progman, title);
    } else {
        printf("[MISS]  Progman not found!\n");
    }

    printf("\n=== Test: SendMessage 0x052C to Progman ===\n");
    if (progman) {
        DWORD_PTR result = 0;
        LRESULT lr = SendMessageTimeoutA(progman, 0x052C, 0, 0,
                                         SMTO_NORMAL, 1000, &result);
        printf("SendMessage result: lr=%ld result=%lu\n", (long)lr, (unsigned long)result);
    }

    printf("\n=== Test: EnumWindows to find WorkerW ===\n");
    EnumWindows(EnumWindowsProc, 0);

    if (g_workerW) {
        printf("\n[SUCCESS] WorkerW found at %p\n", g_workerW);

        RECT rc;
        GetWindowRect(g_workerW, &rc);
        printf("WorkerW rect: (%ld,%ld)-(%ld,%ld) size=%ldx%ld\n",
               rc.left, rc.top, rc.right, rc.bottom,
               rc.right - rc.left, rc.bottom - rc.top);
    } else {
        printf("\n[FAILURE] WorkerW NOT found!\n");
    }

    return 0;
}
