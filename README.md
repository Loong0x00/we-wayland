# WE-Wayland

Run [Wallpaper Engine](https://store.steampowered.com/app/431960/Wallpaper_Engine/) as a live desktop wallpaper on GNOME/Wayland.

WE runs natively through Steam/Proton (GE-Proton recommended) — this project bridges its rendered output to the GNOME desktop background layer. No re-implementation of WE's rendering engine; WE does all the work, we just put it behind your windows.

## How it works

```
Wallpaper Engine (Proton/DXVK)
    → renders to a fake WorkerW window (custom dwmapi.dll)
    → Xwayland presents the frame to Mutter
    → GNOME Shell extension sets _NET_WM_WINDOW_TYPE_DESKTOP
    → Mutter natively places it behind all windows
```

Three components, each handling one layer:

| Component | Role |
|-----------|------|
| `fake-workerw/dwmapi.dll` | Creates the Progman/WorkerW hierarchy WE expects (Wine has none) |
| `gnome-ext/` | GNOME Shell extension — instantly sets DESKTOP window type on WE windows |
| `launch-we.sh` | Steam launch script — fallback property maintenance + input passthrough |

## Requirements

- Steam + [GE-Proton](https://github.com/GloriousEggroll/proton-ge-custom) (tested with GE-Proton10-28)
- Wallpaper Engine (Steam, Windows version via Proton)
- GNOME 49 on Wayland
- `xprop`, `xdotool`, `xwininfo` (usually pre-installed; on Arch: `xorg-xprop xdotool xorg-xwininfo`)

## Installation

### 1. Build and deploy dwmapi.dll

```bash
cd fake-workerw
x86_64-w64-mingw32-gcc -shared -o dwmapi.dll dwmapi-override.c dwmapi.def \
    -lkernel32 -luser32 -lgdi32
cp dwmapi.dll ~/.local/share/Steam/steamapps/common/wallpaper_engine/
```

### 2. Set Wine DLL override

Add to `~/.local/share/Steam/steamapps/compatdata/431960/pfx/user.reg`:

```ini
[Software\\Wine\\AppDefaults\\wallpaper64.exe\\DllOverrides]
"dwmapi"="native"
```

This only affects `wallpaper64.exe` — the WE launcher and CEF browser are unaffected.

### 3. Install GNOME Shell extension

```bash
ln -sf "$(pwd)/gnome-ext" ~/.local/share/gnome-shell/extensions/we-wayland@local
```

Log out and back in, then enable:

```bash
gnome-extensions enable we-wayland@local
```

### 4. Set Steam launch options

For Wallpaper Engine in Steam, set launch options to:

```
/path/to/we-wayland/launch-we.sh %command%
```

### 5. Launch

Start Wallpaper Engine from Steam. Select a **Scene** type wallpaper. The wallpaper should appear behind all your windows within ~1 second.

## Current status

**Working:**
- Scene wallpapers render with full animation and audio
- Wallpaper automatically placed behind all windows (DESKTOP stacking layer)
- Not visible in Alt+Tab, taskbar, or overview window list
- Near-instant setup via GNOME Shell `window-created` signal

**Known limitations:**
- Video wallpapers crash (Wine Media Foundation incomplete — not our bug)
- Mouse cursor disappears over wallpaper area
- Overview (Super key) shows static GNOME wallpaper as background, not WE

## How it actually works (technical)

### The problem

Wallpaper Engine on Windows renders to a hidden `WorkerW` window behind the desktop icons. It finds this window through `FindWindowW("Progman")` → `SendMessage(0x052C)` → `EnumChildWindows`. Wine has no Progman/WorkerW, so WE's renderer fails to initialize.

### Phase 0: Fake desktop hierarchy

A custom `dwmapi.dll` (loaded only by `wallpaper64.exe` via per-app DLL override) creates a complete fake desktop hierarchy on the first DWM API call:

```
Progman ("Program Manager")
  → WorkerW-icons (contains SHELLDLL_DefView → SysListView32)
    → WorkerW-render (title="WE_RENDER", WE's render target)
```

Z-order is set via `SetWindowPos(HWND_BOTTOM)` in correct order (Progman first, render last).

### Phase 1: Desktop layer integration

Clutter.Clone (used by Hanabi and GNOME's own workspace thumbnails) shows **black** for DXVK/Vulkan-rendered Xwayland windows — a fundamental incompatibility. Instead, we set the X11 window type directly:

1. **GNOME extension** catches `window-created`, identifies WE windows by `WM_CLASS=steam_app_431960`, calls `xprop` to set `_NET_WM_WINDOW_TYPE_DESKTOP`
2. **Render window** (`WE_RENDER` title) is raised within the DESKTOP layer
3. **Structural windows** (Progman, WorkerW-icons) are made transparent (`opacity=0`)
4. **launch-we.sh** monitors and re-applies properties every 5s as fallback

## Project structure

```
we-wayland/
├── fake-workerw/
│   ├── dwmapi-override.c    # Custom dwmapi.dll source
│   └── dwmapi.def           # DLL exports
├── gnome-ext/
│   ├── extension.js          # GNOME Shell extension
│   └── metadata.json
├── launch-we.sh              # Steam launch script
├── DESIGN.md                 # Detailed technical design document
└── CLAUDE.md                 # Development task spec
```

## License

MIT
