/*
 * test-workerw-children.c - Check WorkerW's Win32 children
 */
#include <windows.h>
#include <stdio.h>

static HWND g_workerW = NULL;

static BOOL CALLBACK FindWorkerW(HWND hwnd, LPARAM lParam)
{
    HWND sv = FindWindowExA(hwnd, NULL, "SHELLDLL_DefView", NULL);
    if (sv) {
        g_workerW = FindWindowExA(NULL, hwnd, "WorkerW", NULL);
    }
    return TRUE;
}

static BOOL CALLBACK EnumChildProc(HWND hwnd, LPARAM lParam)
{
    char cls[256] = {0};
    char title[256] = {0};
    GetClassNameA(hwnd, cls, sizeof(cls));
    GetWindowTextA(hwnd, title, sizeof(title));

    RECT rc;
    GetWindowRect(hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;

    DWORD style = GetWindowLongA(hwnd, GWL_STYLE);
    DWORD exstyle = GetWindowLongA(hwnd, GWL_EXSTYLE);

    printf("  child %p class='%s' title='%s' %dx%d style=0x%08lx exstyle=0x%08lx\n",
           hwnd, cls, title, w, h, (unsigned long)style, (unsigned long)exstyle);

    return TRUE;
}

int main(void)
{
    EnumWindows(FindWorkerW, 0);

    if (!g_workerW) {
        printf("WorkerW not found!\n");
        return 1;
    }

    printf("WorkerW = %p\n", g_workerW);

    RECT rc;
    GetWindowRect(g_workerW, &rc);
    printf("WorkerW rect: %ldx%ld\n", rc.right - rc.left, rc.bottom - rc.top);

    printf("\n=== WorkerW Win32 children ===\n");
    int count = 0;
    HWND child = NULL;
    while ((child = FindWindowExA(g_workerW, child, NULL, NULL)) != NULL) {
        count++;
        char cls[256] = {0};
        char title[256] = {0};
        GetClassNameA(child, cls, sizeof(cls));
        GetWindowTextA(child, title, sizeof(title));
        RECT crc;
        GetWindowRect(child, &crc);
        BOOL visible = IsWindowVisible(child);
        printf("  [%d] %p class='%s' title='%s' %ldx%ld visible=%d\n",
               count, child, cls, title,
               crc.right - crc.left, crc.bottom - crc.top, visible);
    }

    if (count == 0)
        printf("  (no children)\n");

    printf("\n=== Progman Win32 children ===\n");
    HWND progman = FindWindowA("Progman", NULL);
    if (progman) {
        child = NULL;
        count = 0;
        while ((child = FindWindowExA(progman, child, NULL, NULL)) != NULL) {
            count++;
            char cls[256] = {0};
            GetClassNameA(child, cls, sizeof(cls));
            RECT crc;
            GetWindowRect(child, &crc);
            printf("  [%d] %p class='%s' %ldx%ld\n",
                   count, child, cls, crc.right - crc.left, crc.bottom - crc.top);
        }
    }

    return 0;
}
