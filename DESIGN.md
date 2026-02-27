# Wallpaper Engine → GNOME/Wayland 实现方案

## 问题背景

Wallpaper Engine (WE) 在 Windows 上通过注入 WorkerW 窗口实现活壁纸。
在 Proton/Wine 环境下，WorkerW 机制不存在，WE 退化为普通窗口。
GNOME/Wayland 的安全模型禁止任意窗口捕获，导致现有方案（linux-wallpaperengine）
在高分辨率和部分壁纸类型上存在兼容性问题。

### 为什么不直接修 linux-wallpaperengine

lwe 通过逆向工程重新实现 WE 的渲染引擎，这条路有根本性的维护问题：

```
WE 更新场景格式  →  lwe 需要跟着逆向重写
WE 加新粒子效果  →  lwe 需要重新实现
WE 加新着色器    →  lwe 永远在追赶
```

WE 是闭源商业软件，更新频率高。让 WE 自己渲染、只捕获输出，
彻底解耦渲染复杂度，无论 WE 内部怎么更新都不影响捕获层。

### 长期维护模型

```
Windows 向后兼容承诺  →  DWM 渲染管线不能大改
                      →  WE 依赖的 API 不会消失
                      →  WE 输出行为不变
                      →  Proton/DXVK 跟着 WE 走

唯一变量  →  GNOME 大版本更新（每年一次）
```

预期维护频率：GNOME 扩展层每年大版本后小幅适配。

---

## WE 壁纸的三种类型

| 类型     | 格式        | 渲染方式            | 本方案覆盖 |
|----------|-------------|---------------------|-----------|
| 视频类   | mp4/webm    | 纯视频播放          | 完全覆盖  |
| Scene 类 | scene.json  | 引擎渲染（粒子/3D） | 完全覆盖  |
| HTML 类  | index.html  | 浏览器渲染          | 完全覆盖  |

Clutter.Clone 方案对所有类型一视同仁，只关心 WE 的窗口渲染输出。

---

## 主方案：Clutter.Clone 窗口克隆（v2）

### 方案来源

通过分析 gnome-ext-hanabi 源码（`refs/gnome-ext-hanabi/`）发现：
Hanabi 并未在 GNOME Shell 扩展内使用 GStreamer appsrc 推帧。
它的实际做法是：

1. 启动独立 GTK4 渲染进程，创建窗口播放视频
2. GNOME Shell 扩展通过 `global.get_window_actors()` 找到该窗口
3. 用 `Clutter.Clone({ source: windowActor })` 克隆到桌面背景层

这意味着：**Mutter 已经持有所有受管窗口的纹理**，包括 Xwayland 窗口。
无需 ffmpeg 捕获、无需 socket 传输、无需 GStreamer 管道。

### 稳定性评级

```
WE 渲染管线        →  ★★★★★  闭源商业软件，核心渲染行为极少大改
Proton/DXVK        →  ★★★★★  成熟稳定，向后兼容性强
Mutter 窗口管理    →  ★★★★☆  Mutter 管理 Xwayland 窗口是核心功能，高度稳定
Clutter.Clone API  →  ★★★★☆  GNOME Shell 自身大量使用（概览、工作区预览等）
GNOME 扩展层       →  ★★☆☆☆  每次大版本几乎必然破坏扩展 API
```

### 核心设计原则

```
整个项目只有一个组件：GNOME Shell 扩展
    只做"找窗口 → 克隆到背景"这一件事
    代码量极小（核心逻辑 < 200 行）
    GNOME 更新后只需改这一个文件
```

### 数据流

```
WE (Proton/Xwayland)
    └─ DXVK 渲染 → Xwayland 持有窗口纹理（GPU 显存）
        └─ Mutter 管理该 Xwayland 窗口（window actor）
            └─ GNOME Shell 扩展：
                ├─ global.get_window_actors() 找到 WE 窗口
                ├─ Clutter.Clone({ source: weWindowActor })
                └─ 插入 backgroundActor 的子节点 → 显示在桌面最底层
```

**性能特征**：Clutter.Clone 不拷贝像素数据，它引用同一个 GPU 纹理。
这实质上是 DMA-BUF 级别的零拷贝，无额外 CPU/GPU/带宽开销。

### 关键前提

1. WE 通过 Proton 运行 → 使用 Xwayland → 是 X11 应用
2. Mutter 管理所有 Xwayland 窗口 → 窗口 actor 存在于 GNOME Shell 内
3. `global.get_window_actors()` 可枚举所有窗口 actor（已在 Hanabi 中验证）
4. `Clutter.Clone` 可克隆任意窗口 actor（GNOME Shell 概览功能本身就用此机制）

### 待验证前提（Task 1 负责）

- WE 的 Xwayland 窗口是否出现在 `global.get_window_actors()` 中
- WE 窗口的 title / WM_CLASS 值（用于匹配）
- Clutter.Clone 对 WE 窗口是否正常工作（无黑帧、无色差）

### 组件清单

| 组件        | 语言  | 职责                                         |
|-------------|-------|----------------------------------------------|
| gnome-ext   | GJS   | 找到 WE 窗口 actor → Clutter.Clone 到桌面层 |

仅此一个组件。无 we-capture，无 we-bridge，无 systemd 服务。

### Hanabi 关键代码参考

**窗口查找**（`wallpaper.js:146-176`）：
```javascript
let windowActors = global.get_window_actors(false);
const targetActors = windowActors.filter(window =>
    window.meta_window.title?.includes(applicationId)
);
```

**克隆到背景**（`wallpaper.js:117-143`）：
```javascript
// LiveWallpaper extends St.Widget, attached to backgroundActor
this._wallpaper = new Clutter.Clone({
    source: renderer,  // window actor
    pivot_point: new Graphene.Point({ x: 0.5, y: 0.5 }),
});
this.add_child(this._wallpaper);
```

**容器结构**（`wallpaper.js:39-51`）：
```javascript
// St.Widget 作为容器，挂到 backgroundActor 上
export const LiveWallpaper = GObject.registerClass(
    class LiveWallpaper extends St.Widget {
        constructor(backgroundActor) {
            super({
                layout_manager: new Clutter.BinLayout(),
                width: backgroundActor.width,
                height: backgroundActor.height,
                x_expand: true,
                y_expand: true,
            });
            backgroundActor.layout_manager = new Clutter.BinLayout();
            backgroundActor.add_child(this);
        }
    }
);
```

---

## 验收标准

### 功能验收

| 项目                          | 标准                                    |
|-------------------------------|-----------------------------------------|
| 视频类壁纸显示                | 画面正常，无明显色差，正确循环          |
| Scene 类壁纸显示              | 粒子/3D 效果正常，无撕裂               |
| HTML 类壁纸显示               | 页面渲染正常                            |
| 分辨率                        | 4K（3840×2160）下无缩放失真            |
| WE 窗口被遮挡时               | 壁纸继续正常更新                        |
| WE 进程退出时                 | 壁纸优雅降级为静态图，不崩溃            |
| 系统锁屏/解锁                 | 解锁后壁纸自动恢复                      |
| 休眠唤醒后                    | 壁纸自动恢复，无需手动重启              |

### 性能验收

| 项目              | 标准                                             |
|-------------------|--------------------------------------------------|
| CPU 额外占用      | Clutter.Clone 方案理论为零，实测 < 2%            |
| GPU 额外占用      | 仅多一次纹理采样，< 1%                           |
| 内存占用          | 稳定，无持续增长（无内存泄漏）                   |
| 帧率              | 壁纸帧率 = WE 渲染帧率（零拷贝，无帧率损失）    |
| 延迟              | 零额外延迟（同一纹理引用）                       |

### 稳定性验收

| 项目                        | 标准                                      |
|-----------------------------|-------------------------------------------|
| 连续运行 2 小时             | 无崩溃，无异常资源增长                    |
| GNOME Shell 崩溃测试        | 扩展崩溃不导致 GNOME Shell 崩溃           |
| 扩展启用/禁用               | 可随时热切换，无需重启                    |
| WE 退出/重启                | 自动检测并恢复                            |
| GNOME 大版本更新后          | 明确告知失效，不静默损坏                  |

---

## 安全策略（防止搞崩桌面）

GNOME Shell 扩展代码运行在 GNOME Shell 进程内部，一旦扩展抛出未捕获异常
就可能导致 GNOME Shell 崩溃，整个桌面重启。以下措施缺一不可：

### 隔离原则

```
gnome-ext 内部  →  只做"找窗口 + 克隆"两件事
                →  全部操作包裹在 try/catch 里
                →  任何异常静默降级为原始壁纸，不向上抛
                →  无复杂逻辑，无外部进程依赖
```

### 必备的紧急出口

扩展必须提供从 TTY 禁用的方式，以防 GNOME 因扩展崩溃无法进入图形界面：

```bash
# 从 TTY（Ctrl+Alt+F2）禁用扩展
GSETTINGS_BACKEND=dconf gsettings set org.gnome.shell enabled-extensions \
  "$(gsettings get org.gnome.shell enabled-extensions | sed 's/,\?\s*we-wayland@local//')"
```

### 测试顺序（不跳步）

```
步骤 1  →  安装空壳扩展（只有 enable/disable 日志），确认桌面正常启动
步骤 2  →  添加窗口查找逻辑，确认能找到目标窗口
步骤 3  →  添加 Clutter.Clone，确认渲染正常
步骤 4  →  添加生命周期管理（WE 退出/重启/锁屏），确认健壮性
步骤 5  →  真实桌面测试，长时间运行
```

每一步确认无问题再进入下一步，出问题可以精确定位到哪一层。

---

## 已知待确认项

1. **窗口可见性**：WE 的 Xwayland 窗口是否出现在 `global.get_window_actors()` 中（Task 1 验证）
2. **窗口标识**：WE 在 Proton 里的窗口标题 / WM_CLASS（Task 1 确认）
3. **Clone 正确性**：Clutter.Clone 对 Xwayland 窗口是否产生正确画面（无黑帧/色差）
4. **遮挡行为**：WE 窗口被隐藏/最小化后 Clutter.Clone 是否仍有输出
5. **音频**：WE 壁纸音效通过 PulseAudio/PipeWire 独立输出，暂不纳入范围

---

## 备选方案

### 方案 A：ffmpeg 捕获管道（Clutter.Clone 失败时的 Fallback）

如果 Task 1 发现 Clutter.Clone 对 WE 窗口不可用，回退到原始设计：

```
WE (Proton/Xwayland)
    └─ ffmpeg x11grab 捕获帧流
        └─ Unix Socket / 共享内存 传输
            └─ GNOME 扩展内重建纹理 → 渲染到桌面层
```

此方案代价：需要额外的 we-bridge 进程、CPU 开销（帧编解码）、
延迟（多一次内存拷贝）、复杂度（socket 协议设计）。

### 方案 B：视频直通（功能有限但今天就能用）

```
WE 导出 mp4  →  Hanabi 或 mpv 循环播放
```

局限：静态循环视频，无交互，无实时效果。

---

## 参考资料（本地）

所有参考资料已下载到 `refs/` 目录：

### Git 仓库
- `refs/gnome-ext-hanabi/` — Hanabi 扩展源码（核心参考）
- `refs/gnome-shell/` — GNOME Shell 源码（扩展 API、background.js）
- `refs/mutter/` — Mutter 源码（Clutter API、Cogl）
- `refs/gjs/` — GJS 源码（JavaScript 绑定）
- `refs/linux-wallpaperengine/` — lwe 源码（对比参考）

### 文档
- `refs/docs/gjs-guide-*.html` — GJS 扩展开发指南
- `refs/docs/clutter-reference.html` — Clutter API 参考（804K）
- `refs/docs/xwayland-docs.html` — Xwayland 架构文档
- `refs/docs/xcomposite-*.txt` — XComposite 协议规范
- `refs/docs/kernel-dmabuf.html` — Linux DMA-BUF 内核文档
- `refs/docs/gstreamer-*.txt` — GStreamer 组件 inspect 输出
- `refs/docs/systemd-*.html` — systemd 服务/资源控制文档
- `refs/docs/ffmpeg-devices.html` — ffmpeg 设备文档（含 x11grab）

---

## Phase 0：WE 渲染修复（fake desktop hierarchy）

> 原设计假设 WE 已经能在 Proton 下正常渲染壁纸。
> 实测发现 Wine 缺少 Progman/WorkerW 窗口层级，WE 渲染器无法初始化。
> 本节记录修复过程和最终方案。

### 问题根因

WE 在 Windows 上的渲染流程：
1. `FindWindowW("Progman")` → 找到桌面 Program Manager 窗口
2. `SendMessageTimeoutW(Progman, 0x052C, ...)` → 触发 WorkerW 创建
3. `EnumChildWindows(desktop, callback)` → 在桌面子窗口中搜索：
   - 找到 class="WorkerW" 且有 SHELLDLL_DefView 子窗口的 WorkerW
   - 通过 `FindWindowExW(NULL, thatWorkerW, "WorkerW")` 找到下一个 WorkerW
   - 检查该 WorkerW 宽度 > 256px → 作为渲染目标
4. 在渲染目标 WorkerW 上创建 D3D11 swap chain

Wine 没有 Progman 窗口 → `FindWindowW("Progman")` 返回 NULL → 渲染器初始化失败
→ 渲染定时器回调指向无效地址 0x1092 → 持续崩溃循环。

### 解决方案：自定义 dwmapi.dll

通过 per-app Wine DLL override，让 wallpaper64.exe 加载我们的 dwmapi.dll。
该 DLL 在首次 `DwmIsCompositionEnabled()` 调用时创建完整的假桌面层级：

```
Z-order（前 → 后）：
  Progman ("Program Manager", WS_POPUP)
    → WorkerW-icons (WS_POPUP, 包含 SHELLDLL_DefView → SysListView32)
      → WorkerW-render (WS_POPUP, WE 的渲染目标)
```

**关键实现细节**：
- `SetWindowPos(HWND_BOTTOM)` 必须按 Progman → icons → render 顺序调用
  （每次 HWND_BOTTOM 将窗口推到最底，后调用的在更底层）
- FakeProgmanProc 处理 0x052C 消息直接返回 0（模拟 WorkerW spawn 成功）
- 所有窗口使用 `WS_EX_NOACTIVATE` 避免抢夺焦点
- Per-app override 确保只有 wallpaper64.exe 加载假 DLL，launcher.exe/CEF 不受影响

**Wine 注册表配置**（`compatdata/431960/pfx/user.reg`）：
```ini
[Software\\Wine\\AppDefaults\\wallpaper64.exe\\DllOverrides]
"dwmapi"="native"
```

### 实测结果

| 项目 | 结果 |
|------|------|
| Scene 壁纸渲染 | **正常**，动画流畅，含音频 |
| Video 壁纸渲染 | 画面渲染正常但随后崩溃（Wine Media Foundation 不完整） |
| DXVK 初始化 | D3D11CreateDevice 成功，Feature Level 11.1，DXVK v2.7.1 |
| WE UI | 可正常使用，选壁纸、切壁纸均正常 |
| 0x1092 崩溃 | 仍有少量（非致命，SEH 恢复），不影响正常运行 |

### Video 壁纸崩溃分析

- 崩溃点：`wallpaper64.exe + 0xE7C31`
- 原因：`mfreadwrite.dll` 被过早卸载，WE 持有的 COM vtable 指向已释放内存
- 这是 Wine/Proton 的 Media Foundation 兼容性问题，与我们的修改无关
- 规避：使用 Scene 类型壁纸（D3D11 GPU 渲染，不走 MF）

### 源码位置

- `fake-workerw/dwmapi-override.c` — 自定义 dwmapi.dll 源码
- `fake-workerw/dwmapi.def` — DLL 导出定义
- `launch-we.sh` — Steam 启动参数脚本

---

## 实测数据记录

> 以下各节在对应 Task 完成后由 Worker 填写实测数据。

### Task 1 实测数据

#### X11 侧侦察结果（Phase 0 之前，无 dwmapi override）

**环境信息**
- 屏幕分辨率：2560x1440@74.94Hz (HDMI-2)
- Xwayland 由 Mutter 管理
- WE 通过 GE-Proton10-28 运行

**WE 进程架构**
| 进程 | PID | 角色 |
|------|-----|------|
| wallpaper64.exe | — | 壁纸渲染器（`-language schinese -updateuicmd`） |
| ui32.exe (主) | — | 壁纸浏览 UI（Chromium 内核） |
| ui32.exe (GPU) | — | Chromium GPU 进程 |
| ui32.exe (renderer) | — | Chromium 渲染进程 |
| explorer.exe | — | Wine 桌面管理（系统托盘） |

**WE WM_CLASS 确认**：`"steam_app_431960"` / `"steam_app_431960"`

#### Phase 0 修复后状态

dwmapi override 创建假桌面层级后：
- WE 成功 `FindWindowW("Progman")` → 找到我们创建的 Progman
- WE 成功通过 EnumChildWindows 找到 WorkerW-render 作为渲染目标
- DXVK 完整初始化：D3D11 设备、swap chain、Presenter 均创建成功
- 渲染窗口现在是 **mapped 且 visible** 的全屏窗口（2560x1440）

| 项目 | 结果 |
|------|------|
| WE 窗口标题 | ""（WorkerW-render，WE 在其上渲染） |
| WE WM_CLASS | `steam_app_431960`（所有 WE 进程窗口共用） |
| 渲染窗口状态 | **Mapped, Visible**（通过 fake desktop hierarchy 解决） |
| Clutter.Clone 可行性 | **待验证**（渲染窗口已 visible，理论上可 Clone） |
| 遮挡/最小化行为 | 待验证 |

#### 下一步

渲染窗口已经 visible，需要在 Looking Glass 中确认：
1. WorkerW-render 窗口是否出现在 `global.get_window_actors()` 中
2. Clutter.Clone 是否能正确克隆该窗口的内容

### Task 2 实测数据

| 项目 | 结果 |
|------|------|
| 嵌套 Shell 加载结果 | （待填） |
| Clone 渲染画面质量 | （待填） |
| try/catch 降级测试 | （待填） |

### Task 3 实测数据

| 项目 | 结果 |
|------|------|
| 窗口隐藏方式 | （待填） |
| WE 退出后行为 | （待填） |
| WE 重启后恢复 | （待填） |
| 锁屏/解锁恢复 | （待填） |

### Task 4 实测数据

| 项目 | 结果 |
|------|------|
| CPU 额外占用 | （待填） |
| GPU 额外占用 | （待填） |
| 内存变化（2小时） | （待填） |
| 帧率 | （待填） |
| 4K 显示效果 | （待填） |
