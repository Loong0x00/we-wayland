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
        const rect = metaWindow.get_frame_rect();
        if (rect.width < 1000) return; // Skip UI / small windows

        const title = metaWindow.get_title() || '';
        const desc = metaWindow.get_description() || '';
        const xid = desc.match(/0x[0-9a-f]+/)?.[0];
        if (!xid) {
            _log(`No X11 ID for "${title}" — skipping`);
            return;
        }

        _log(`WE window: ${xid} "${title}" ${rect.width}x${rect.height} — setting DESKTOP type`);

        // Set DESKTOP type immediately
        GLib.spawn_command_line_async(
            `xprop -id ${xid} -f _NET_WM_WINDOW_TYPE 32a ` +
            `-set _NET_WM_WINDOW_TYPE _NET_WM_WINDOW_TYPE_DESKTOP`
        );

        if (title === 'WE_RENDER') {
            // Render window — raise within DESKTOP layer
            GLib.spawn_command_line_async(`xdotool windowraise ${xid}`);
        } else {
            // Structural window (Progman, WorkerW-icons) — make invisible
            GLib.spawn_command_line_async(
                `xprop -id ${xid} -f _NET_WM_WINDOW_OPACITY 32c ` +
                `-set _NET_WM_WINDOW_OPACITY 0`
            );
        }
    }
}
