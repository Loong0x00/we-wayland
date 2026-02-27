/*
 * wallpaper64-wrapper.c
 *
 * Drop-in replacement for wallpaper64.exe.
 * 1. Launches fake-workerw.exe to create Progman/WorkerW window hierarchy
 * 2. Launches the real wallpaper64 (renamed to wallpaper64-real.exe)
 *
 * The fake desktop windows give WE the rendering target it expects.
 * Combined with our fake dwmapi.dll, this should let WE initialize
 * its D3D11 rendering pipeline.
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev,
                   LPSTR cmdLine, int show)
{
    /* Get our own directory */
    char myPath[MAX_PATH];
    GetModuleFileNameA(NULL, myPath, MAX_PATH);
    char *slash = strrchr(myPath, '\\');
    if (slash) *slash = '\0';

    /* Step 1: Launch fake-workerw.exe (detached, stays running) */
    char fwCmd[8192];
    _snprintf(fwCmd, sizeof(fwCmd), "\"%s\\fake-workerw.exe\"", myPath);

    STARTUPINFOA fwSi = {0};
    fwSi.cb = sizeof(fwSi);
    PROCESS_INFORMATION fwPi = {0};

    if (CreateProcessA(NULL, fwCmd, NULL, NULL,
                       FALSE, 0, NULL, myPath, &fwSi, &fwPi)) {
        CloseHandle(fwPi.hThread);
        CloseHandle(fwPi.hProcess);
        /* Give fake-workerw time to create windows */
        Sleep(500);
    }

    /* Step 2: Launch real wallpaper64 with original args */
    char cmd[8192];
    _snprintf(cmd, sizeof(cmd),
              "\"%s\\wallpaper64-real.exe\" %s",
              myPath, cmdLine ? cmdLine : "");

    STARTUPINFOA si = {0};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {0};

    if (!CreateProcessA(NULL, cmd, NULL, NULL,
                        FALSE, 0, NULL, myPath, &si, &pi)) {
        char msg[512];
        _snprintf(msg, sizeof(msg),
                  "Failed to start wallpaper64-real.exe (err=%lu)", GetLastError());
        MessageBoxA(NULL, msg, "wallpaper64-wrapper", MB_OK | MB_ICONERROR);
        return 1;
    }

    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);

    return (int)exitCode;
}
