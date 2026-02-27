/**
 * WE-Wayland GNOME Shell Extension
 *
 * Sets _NET_WM_WINDOW_TYPE_DESKTOP on WE windows the instant they appear
 * (via window-created signal → near-zero delay).
 * Also hides structural windows (Progman, WorkerW-icons) with opacity=0.
 *
 * launch-we.sh handles input passthrough and serves as fallback.
 */

import GLib from 'gi://GLib';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import { Extension } from 'resource:///org/gnome/shell/extensions/extension.js';

const WE_WM_CLASS = 'steam_app_431960';

function _log(msg) {
    console.log(`[we-wayland] ${msg}`);
}

export default class WEWaylandExtension extends Extension {
    enable() {
        _log('enable() — instant DESKTOP type setter');
        this._signals = [];

        const id = global.display.connect('window-created', (_d, mw) => {
            try { this._onWindowCreated(mw); } catch (e) {
                _log(`window-created error: ${e.message}`);
            }
        });
        this._signals.push({ obj: global.display, id });

        // Also check existing windows (extension loaded after WE)
        for (const actor of global.get_window_actors()) {
            try { this._onWindowCreated(actor.meta_window); } catch (e) {}
        }
    }

    disable() {
        _log('disable()');
        for (const { obj, id } of this._signals)
            try { obj.disconnect(id); } catch (e) {}
        this._signals = [];
    }

    _onWindowCreated(metaWindow) {
        if (!metaWindow) return;

        const wmClass = metaWindow.get_wm_class();
        if (wmClass === WE_WM_CLASS) {
            this._handleWEWindow(metaWindow);
        } else if (!wmClass) {
            // WM_CLASS not yet available — wait for it
            const sid = metaWindow.connect('notify::wm-class', () => {
                try {
                    metaWindow.disconnect(sid);
                    if (metaWindow.get_wm_class() === WE_WM_CLASS)
                        this._handleWEWindow(metaWindow);
                } catch (e) {}
            });
        }
    }

    _handleWEWindow(metaWindow) {
        const title = metaWindow.get_title() || '';
        const desc = metaWindow.get_description() || '';
        const xid = desc.match(/0x[0-9a-f]+/)?.[0];
        if (!xid) return;

        // Only target windows created by our dwmapi.dll, identified by title
        if (title === 'WE_RENDER') {
            // Render window — DESKTOP type + raise
            _log(`RENDER: ${xid} "${title}" — DESKTOP + raise`);
            GLib.spawn_command_line_async(
                `xprop -id ${xid} -f _NET_WM_WINDOW_TYPE 32a ` +
                `-set _NET_WM_WINDOW_TYPE _NET_WM_WINDOW_TYPE_DESKTOP`
            );
            GLib.spawn_command_line_async(`xdotool windowraise ${xid}`);
        } else if (title === 'Program Manager') {
            // Progman — DESKTOP type + invisible
            _log(`HIDE: ${xid} "${title}" — DESKTOP + opacity=0`);
            GLib.spawn_command_line_async(
                `xprop -id ${xid} -f _NET_WM_WINDOW_TYPE 32a ` +
                `-set _NET_WM_WINDOW_TYPE _NET_WM_WINDOW_TYPE_DESKTOP`
            );
            GLib.spawn_command_line_async(
                `xprop -id ${xid} -f _NET_WM_WINDOW_OPACITY 32c ` +
                `-set _NET_WM_WINDOW_OPACITY 0`
            );
        } else if (title === '') {
            // Empty title — could be WorkerW-icons (structural, fullscreen)
            // Only hide if fullscreen to avoid touching WE internal windows
            const rect = metaWindow.get_frame_rect();
            const monitor = global.display.get_primary_monitor();
            const monRect = global.display.get_monitor_geometry(monitor);
            if (rect.width >= monRect.width * 0.95 && rect.height >= monRect.height * 0.95) {
                _log(`HIDE: ${xid} (empty title, ${rect.width}x${rect.height}) — DESKTOP + opacity=0`);
                GLib.spawn_command_line_async(
                    `xprop -id ${xid} -f _NET_WM_WINDOW_TYPE 32a ` +
                    `-set _NET_WM_WINDOW_TYPE _NET_WM_WINDOW_TYPE_DESKTOP`
                );
                GLib.spawn_command_line_async(
                    `xprop -id ${xid} -f _NET_WM_WINDOW_OPACITY 32c ` +
                    `-set _NET_WM_WINDOW_OPACITY 0`
                );
            }
        }
        // All other titles (Wallpaper UI, Input, Steam, etc.) → skip
    }
}
