#!/bin/bash
# Steam launch options: /home/user/we-wayland/launch-we.sh %command%
#
# dwmapi.dll override is set per-app in Wine registry (only wallpaper64.exe).
# No global WINEDLLOVERRIDES needed — launcher.exe and CEF are unaffected.

# === Diagnostic logging ===
export DXVK_LOG_LEVEL=info
export DXVK_LOG_PATH=/tmp

# Wine: log DLL loading + SEH exceptions (moderate verbosity)
export WINEDEBUG=+loaddll,+seh

# === X11: Set WE desktop windows to _NET_WM_WINDOW_TYPE_DESKTOP ===
# Mutter natively handles DESKTOP type windows:
#   - Always behind all other windows (desktop stacking layer)
#   - Not in Alt+Tab / overview / taskbar
#   - No focus stealing
# Loop re-checks every 5s in case Wine resets the property.
_LOG=/tmp/we-desktop-type.log
: > "$_LOG"

# Set X11 input shape to empty → click-through, cursor stays visible.
# Uses XShapeCombineRectangles(ShapeInput) via python3 ctypes.
_set_input_passthrough() {
    python3 -c "
import ctypes, ctypes.util, sys
x11 = ctypes.cdll.LoadLibrary(ctypes.util.find_library('X11'))
xext = ctypes.cdll.LoadLibrary(ctypes.util.find_library('Xext'))
x11.XOpenDisplay.restype = ctypes.c_void_p
dpy = x11.XOpenDisplay(None)
if dpy:
    xext.XShapeCombineRectangles(
        ctypes.c_void_p(dpy), ctypes.c_ulong(int(sys.argv[1])),
        2, 0, 0, None, 0, 0, 0)  # ShapeInput, empty region
    x11.XFlush(ctypes.c_void_p(dpy))
    x11.XCloseDisplay(ctypes.c_void_p(dpy))
" "$1" 2>/dev/null
}

_set_desktop_type() {
    local changed=0
    for wid in $(xdotool search --class steam_app_431960 2>/dev/null); do
        # Identify by WM_NAME — only target windows created by our dwmapi.dll
        local name
        name=$(xprop -id "$wid" WM_NAME 2>/dev/null | sed -n 's/.*= "\(.*\)"/\1/p')

        if [ "$name" = "WE_RENDER" ]; then
            # === Render window: the actual wallpaper ===
            if ! xprop -id "$wid" _NET_WM_WINDOW_TYPE 2>/dev/null | grep -q DESKTOP; then
                xprop -id "$wid" \
                    -f _NET_WM_WINDOW_TYPE 32a \
                    -set _NET_WM_WINDOW_TYPE _NET_WM_WINDOW_TYPE_DESKTOP 2>/dev/null
                changed=1
            fi
            xdotool windowraise "$wid" 2>/dev/null
            _set_input_passthrough "$wid"
            echo "$(date +%T) RENDER $wid \"$name\" — raise + passthrough" >> "$_LOG"

        elif [ "$name" = "Program Manager" ]; then
            # === Progman: structural, make invisible ===
            if ! xprop -id "$wid" _NET_WM_WINDOW_TYPE 2>/dev/null | grep -q DESKTOP; then
                xprop -id "$wid" \
                    -f _NET_WM_WINDOW_TYPE 32a \
                    -set _NET_WM_WINDOW_TYPE _NET_WM_WINDOW_TYPE_DESKTOP 2>/dev/null
                changed=1
            fi
            xprop -id "$wid" \
                -f _NET_WM_WINDOW_OPACITY 32c \
                -set _NET_WM_WINDOW_OPACITY 0 2>/dev/null
            echo "$(date +%T) HIDE   $wid \"$name\" — opacity=0" >> "$_LOG"

        elif [ -z "$name" ]; then
            # Empty title — could be WorkerW-icons (structural) or WE internal
            # Only hide if fullscreen (matches screen resolution)
            local W H
            W=$(xwininfo -id "$wid" 2>/dev/null | awk '/Width:/{print $2}')
            H=$(xwininfo -id "$wid" 2>/dev/null | awk '/Height:/{print $2}')
            local SCREEN_W SCREEN_H
            read SCREEN_W SCREEN_H < <(xdpyinfo 2>/dev/null | awk '/dimensions:/{split($2,a,"x"); print a[1], a[2]}')
            if [ "${W:-0}" -eq "${SCREEN_W:-2560}" ] && [ "${H:-0}" -eq "${SCREEN_H:-1440}" ]; then
                if ! xprop -id "$wid" _NET_WM_WINDOW_TYPE 2>/dev/null | grep -q DESKTOP; then
                    xprop -id "$wid" \
                        -f _NET_WM_WINDOW_TYPE 32a \
                        -set _NET_WM_WINDOW_TYPE _NET_WM_WINDOW_TYPE_DESKTOP 2>/dev/null
                    changed=1
                fi
                xprop -id "$wid" \
                    -f _NET_WM_WINDOW_OPACITY 32c \
                    -set _NET_WM_WINDOW_OPACITY 0 2>/dev/null
                echo "$(date +%T) HIDE   $wid (empty,${W}x${H}) — opacity=0" >> "$_LOG"
            fi
        fi
        # All other windows (Wallpaper UI, Input, Steam, etc.) → skip
    done
    return $changed
}

(
    # Tight poll: check every second until WE windows appear
    echo "$(date +%T) Desktop type watcher started" >> "$_LOG"
    for _i in $(seq 1 60); do
        if xdotool search --class steam_app_431960 >/dev/null 2>&1; then
            _set_desktop_type
            break
        fi
        sleep 1
    done

    # Then regular monitoring (re-set if Wine resets the property)
    while pgrep -f wallpaper64.exe >/dev/null 2>&1; do
        sleep 5
        _set_desktop_type
    done
    echo "$(date +%T) WE exited, watcher stopping" >> "$_LOG"
) &

# Redirect stderr to capture Wine debug output
exec "$@" 2>/tmp/we-wine-debug.log
