/*
 * we-launcher-wrapper.c
 *
 * Drop-in wrapper for WE's launcher.exe.
 * Starts fake-workerw.exe first (creating Progman/WorkerW),
 * waits 2 seconds, then launches the real launcher.exe.
 *
 * Place in WE's game directory alongside launcher.exe.
 * Set Steam launch options to:
 *   /home/user/we-wayland/launch-we.sh %command%
 */

#include <windows.h>
#include <stdio.h>

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev,
                   LPSTR cmdLine, int show)
{
    /* Get our own directory (same dir as launcher.exe) */
    char myPath[MAX_PATH];
    GetModuleFileNameA(NULL, myPath, MAX_PATH);
    char *slash = strrchr(myPath, '\\');
    if (slash) *slash = '\0';

    STARTUPINFOA si = {0};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {0};

    /* 1. Start fake-workerw.exe (from C:\) */
    if (CreateProcessA("C:\\fake-workerw.exe", NULL, NULL, NULL,
                       FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }

    /* 2. Wait for fake-workerw to create its windows */
    Sleep(3000);

    /* 3. Build command for real launcher.exe */
    char launcherCmd[4096];
    _snprintf(launcherCmd, sizeof(launcherCmd),
              "\"%s\\launcher.exe\" %s", myPath, cmdLine ? cmdLine : "");

    STARTUPINFOA si2 = {0};
    si2.cb = sizeof(si2);
    PROCESS_INFORMATION pi2 = {0};

    /* 4. Start real launcher.exe with same arguments, same working dir */
    if (!CreateProcessA(NULL, launcherCmd, NULL, NULL,
                        FALSE, 0, NULL, myPath, &si2, &pi2)) {
        return 1;
    }

    /* 5. Wait for launcher.exe to exit */
    CloseHandle(pi2.hThread);
    WaitForSingleObject(pi2.hProcess, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeProcess(pi2.hProcess, &exitCode);
    CloseHandle(pi2.hProcess);

    return (int)exitCode;
}
