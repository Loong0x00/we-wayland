#!/bin/bash
# Steam launch options: /home/user/we-wayland/launch-we.sh %command%
#
# dwmapi.dll override is set per-app in Wine registry (only wallpaper64.exe).
# No global WINEDLLOVERRIDES needed — launcher.exe and CEF are unaffected.

# === Diagnostic logging ===
export DXVK_LOG_LEVEL=info
export DXVK_LOG_PATH=/tmp/dxvk

# Wine: log DLL loading + SEH exceptions (moderate verbosity)
export WINEDEBUG=+loaddll,+seh

# Redirect stderr to capture Wine debug output
exec "$@" 2>/tmp/we-wine-debug.log
