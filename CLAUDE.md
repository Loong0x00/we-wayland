# WE-Wayland 项目执行规范

## 项目概述

将 Wallpaper Engine（运行在 Proton/Xwayland 中）的渲染输出桥接到
GNOME/Wayland 桌面层，实现活壁纸功能。

核心方案：通过 GNOME Shell 扩展的 `Clutter.Clone` 直接克隆 WE 的
Xwayland 窗口 actor 到桌面背景层。零拷贝，无中间进程。
详细技术方案见 DESIGN.md。

---

## Agent 工作流模型

### 角色分工

```
Worker Agent   →  实现代码、更新文档、提交 git
Checker Agent  →  运行验证脚本、检查输出、判断通过/拒绝
Human Gate     →  视觉验收、授权进入真实桌面、最终确认
```

### 执行循环

```
取最低 ID 的 pending 任务
    ↓
Worker 执行任务
    ↓
Worker 更新 DESIGN.md（记录发现）
    ↓
Worker git commit（格式见下）
    ↓
Checker 运行对应验收脚本
    ↓
通过？
  是  →  TaskUpdate 标记 completed  →  取下一任务
  否  →  Worker 根据 Checker 反馈修正  →  重新提交 Checker
Human Gate（如需）？
  通过  →  继续
  否    →  Worker 修正后重新请求 Human Gate
```

### 任务状态流转

```
pending  →  in_progress（Worker 开始）  →  completed（Checker 通过）
```

---

## 强制安全规则

以下规则不得以任何理由绕过：

1. **嵌套 Shell 原则**
   所有 GNOME 扩展相关测试必须先在嵌套 Shell 中进行：
   ```bash
   dbus-run-session -- gnome-shell --nested --wayland
   ```
   未经 Human Gate 明确批准，不得将扩展安装到真实桌面。

2. **快照原则**
   进入真实桌面测试前必须先创建 Btrfs 快照：
   ```bash
   sudo btrfs subvolume snapshot / /.snapshots/pre-we-$(date +%Y%m%d)
   ```

3. **TTY 逃生出口**
   每个涉及 GNOME 扩展的任务，Worker 必须在 DESIGN.md 中记录当前的
   TTY 禁用命令（扩展 ID 确定后更新）：
   ```bash
   GSETTINGS_BACKEND=dconf gsettings set org.gnome.shell enabled-extensions \
     "$(gsettings get org.gnome.shell enabled-extensions | sed 's/,\?\s*we-wayland@local//')"
   ```

4. **防御性编码原则**
   扩展内所有操作必须包裹在 try/catch 中。
   任何异常静默降级为原始壁纸，绝不向上抛出导致 GNOME Shell 崩溃。
   ```javascript
   try { /* clone logic */ } catch (e) { log(`we-wayland: ${e.message}`); }
   ```

5. **不跳步原则**
   严格按任务编号顺序执行，前序任务未通过 Checker 不得开始后续任务。

---

## Git 规范

### Commit 格式

```
[Task N] 简短描述

- 具体做了什么
- 发现了什么（写入 DESIGN.md 的内容摘要）
- 验收结果：通过/需修正
```

### Commit 时机

- 每个任务完成且 Checker 通过后提交一次
- 修正后重新通过 Checker 再提交（不要提交未通过的状态）
- 禁止 force push

### 分支策略

```
main  →  只包含通过验收的状态
```

---

## 文档更新规范

每个任务完成后 Worker 必须将以下内容写入 DESIGN.md 对应的"实测数据记录"表格：

- 任务中发现的具体值（窗口类名、实测 CPU 占用等）
- 对"已知待确认项"的结论（确认/排除）
- 任何偏离原设计的决策及原因

---

## 任务定义

---

### Task 1：WE 窗口侦察 + Clutter.Clone 可行性验证

**目标**
确认 WE 的 Xwayland 窗口对 GNOME Shell 可见，验证 Clutter.Clone 方案可行性。
**此任务不写生产代码，只做侦察和概念验证。**

**前置条件**
- 用户手动启动 Wallpaper Engine（通过 Steam + GE-Proton）
- 确认 WE 界面已显示壁纸渲染

**Worker 执行步骤**
1. 用 `xwininfo -tree -root` 列出所有 X11 窗口
2. 用 `xprop` 读取 WE 窗口的 WM_CLASS、WM_NAME
3. 在 GNOME Shell Looking Glass（Alt+F2 → lg）中执行：
   ```javascript
   global.get_window_actors().forEach(a =>
       log(`${a.meta_window.get_wm_class()} | ${a.meta_window.get_title()}`)
   );
   ```
   确认 WE 窗口出现在结果中
4. 在 Looking Glass 中测试 Clutter.Clone：
   ```javascript
   let we = global.get_window_actors().find(a =>
       a.meta_window.get_wm_class()?.includes('WE_CLASS_FROM_STEP2')
   );
   // 如果找到，记录属性；如不能在 lg 中直接测 Clone，记录到此为止
   ```
5. 测试 WE 窗口最小化/遮挡后 actor 是否仍存在
6. 将所有发现写入 DESIGN.md 的"Task 1 实测数据"表格

**Checker 自动验收**
```bash
# 检查 WE 进程正在运行
pgrep -f wallpaper 2>/dev/null || pgrep -f "Wallpaper" 2>/dev/null

# 检查 Xwayland 窗口可见
xwininfo -tree -root 2>/dev/null | grep -i "wallpaper\|engine" | head -5

# 检查 DESIGN.md 已更新
grep -c "待填" DESIGN.md | awk '{print "剩余待填项: " $1}'
```

**Human Gate（必须）**
- 确认 Looking Glass 输出中包含 WE 窗口信息
- 确认 WE 窗口属性已正确记录

**通过标准**
- [ ] WE 窗口的 WM_CLASS 和 title 已确认并记录
- [ ] WE 窗口出现在 `global.get_window_actors()` 结果中
- [ ] 遮挡/最小化行为已测试并记录
- [ ] DESIGN.md "Task 1 实测数据"表格已填写

**失败处理**
- WE 窗口不在 `get_window_actors()` 中 → 调查 Xwayland 窗口管理机制，检查是否需要特殊配置
- Clutter.Clone 产生黑帧 → 回退到 ffmpeg 捕获方案（见 DESIGN.md 备选方案 A），重新规划 Task 3-4

**关键决策点**
此任务的结论决定后续所有任务的实现路径：
- 方案 A（Clutter.Clone 可行）→ 继续 Task 2-4 按当前计划执行
- 方案 B（需要 ffmpeg fallback）→ 暂停，重新规划任务，通知 Human Gate

---

### Task 2：实现最小 GNOME Shell 扩展（嵌套 Shell 验证）

**目标**
实现核心扩展：找到 WE 窗口 → Clutter.Clone 到桌面背景层。
在嵌套 Shell 中完成全部验证。

**Worker 执行步骤**
1. 创建扩展骨架：`~/.local/share/gnome-shell/extensions/we-wayland@local/`
   - `metadata.json`：声明兼容 GNOME 49，uuid = `we-wayland@local`
   - `extension.js`：空壳，只有 enable/disable 日志
2. 在嵌套 Shell 中确认空壳扩展加载正常
3. 实现窗口查找逻辑（参考 Hanabi `wallpaper.js:146-176`）：
   - 通过 `meta_window.get_wm_class()` 或 `get_title()` 匹配 WE 窗口
   - 使用 Task 1 确认的标识值
4. 实现 Clutter.Clone 渲染（参考 Hanabi `wallpaper.js:39-51, 117-143`）：
   - 创建 `St.Widget` 容器 + `Clutter.BinLayout`
   - 挂载到 `backgroundActor`
   - `Clutter.Clone({ source: weWindowActor })`
5. 所有逻辑包裹在 try/catch，异常时 log 并静默降级
6. 在嵌套 Shell 中测试完整流程

**注意**：嵌套 Shell 中 WE 的 Xwayland 窗口可能不可见（嵌套 Shell 有独立
display server）。此任务可用一个替代窗口（如 mpv 播放视频）验证 Clone 机制，
真正的 WE 集成在 Task 4（真实桌面）中完成。

**Checker 自动验收（嵌套 Shell）**
```bash
# 检查扩展文件存在
test -f ~/.local/share/gnome-shell/extensions/we-wayland@local/metadata.json \
  && echo "Extension files OK" || echo "Extension files MISSING"

# 检查 metadata.json 格式正确
python3 -c "
import json
with open('$HOME/.local/share/gnome-shell/extensions/we-wayland@local/metadata.json') as f:
    m = json.load(f)
    assert 'we-wayland@local' == m['uuid']
    assert '49' in str(m.get('shell-version', []))
    print('metadata OK')
"

# 启动嵌套 Shell 并检查存活
dbus-run-session -- gnome-shell --nested --wayland &
NESTED_PID=$!
sleep 8

# 检查进程存活
kill -0 $NESTED_PID 2>/dev/null && echo "Shell alive" || echo "Shell CRASHED"

# 检查 journalctl 中无崩溃
journalctl --user --since "1 min ago" | grep -iE "we-wayland|crash|segfault" | head -10

kill $NESTED_PID 2>/dev/null
```

**Human Gate（必须 — 嵌套 Shell 视觉验收）**
- 在嵌套 Shell 中确认扩展加载无报错
- 确认替代窗口（如 mpv）的 Clone 正确显示为背景
- 确认扩展 disable 后 Clone 被清理干净

**通过标准**
- [ ] 嵌套 Shell 中扩展加载无报错
- [ ] Clutter.Clone 在嵌套 Shell 中渲染正常（使用替代窗口验证）
- [ ] 扩展 disable 后所有 actor 正确清理
- [ ] 扩展崩溃时 GNOME Shell 不崩溃（try/catch 生效）
- [ ] Human Gate 视觉确认通过

---

### Task 3：窗口管理 + 生命周期处理

**目标**
处理所有生命周期事件：WE 窗口出现/消失、锁屏/解锁、扩展热切换。
确保健壮性。

**Worker 执行步骤**
1. 实现 WE 窗口隐藏（参考 Hanabi `gnomeShellOverride.js` + `windowManager.js`）：
   - 隐藏 WE 窗口使其不出现在 Alt+Tab、概览、任务栏
   - 方式可能是设置 `meta_window` 的 skip_taskbar 或 override 窗口列表
2. 实现窗口生命周期监听：
   - 监听 `global.display` 的 `window-created` 信号
   - WE 窗口出现 → 自动绑定 Clutter.Clone
   - WE 窗口消失 → 移除 Clone，恢复静态壁纸（fade 动画）
   - WE 重新出现 → 重新绑定
3. 实现锁屏/解锁处理：
   - 监听 `Main.screenShield` 相关信号
   - 锁屏时暂停（可选），解锁后确认 Clone 仍正常
4. 实现扩展热切换：
   - `disable()` 时彻底清理所有 actor、断开所有信号连接
   - 重新 `enable()` 后能重新绑定

**Checker 自动验收（嵌套 Shell）**
```bash
# 启动嵌套 Shell
dbus-run-session -- gnome-shell --nested --wayland &
NESTED_PID=$!
sleep 8

# 验证 Shell 存活
kill -0 $NESTED_PID 2>/dev/null && echo "Shell alive" || echo "Shell CRASHED"

# 检查无内存泄漏相关警告
journalctl --user --since "1 min ago" | grep -i "leak\|dispose\|finalize" | head -5

# 检查扩展可以 enable/disable 多次无崩溃
# （此处需人工在嵌套 Shell 中操作 Extensions app）

kill $NESTED_PID 2>/dev/null
```

**Human Gate（必须）**
- 在嵌套 Shell 中验证：
  - 关闭替代窗口 → 壁纸恢复静态（fade 动画）
  - 重新打开 → 自动重新绑定
  - 扩展 disable/enable → 无残留 actor

**通过标准**
- [ ] WE 窗口不出现在 Alt+Tab / 概览中
- [ ] WE 退出后壁纸优雅降级为静态壁纸
- [ ] WE 重启后自动重新绑定
- [ ] 扩展 disable 后所有资源彻底清理
- [ ] 反复 enable/disable 不导致崩溃或泄漏
- [ ] Human Gate 确认所有生命周期过渡正常

---

### Task 4：真实桌面集成测试与最终验收

**目标**
在真实桌面上进行端到端测试，验证所有验收标准，确认可日常使用。

**前置条件**
- Task 1-3 全部通过
- 用户已创建 Btrfs 快照
- 用户明确发出"可以安装到真实桌面"的指令

**Worker 执行步骤**
1. 确认 Btrfs 快照已创建：
   ```bash
   sudo btrfs subvolume snapshot / /.snapshots/pre-we-$(date +%Y%m%d)
   ```
2. 在真实桌面启用扩展
3. 启动 WE → 确认壁纸自动显示
4. 运行性能基准测试
5. 运行边界情况测试
6. 运行 2 小时稳定性测试
7. 将所有实测数据填入 DESIGN.md

**Checker 自动验收**
```bash
# 性能基准（运行 10 分钟取平均）
echo "GNOME Shell CPU 占用监控..."
top -b -n 60 -d 10 | grep "gnome-shell" | awk '{print $9}' | \
  awk '{sum+=$1; count++} END {print "平均CPU: " sum/count "%"}'

# 内存监控（运行 30 分钟对比）
GNOME_PID=$(pgrep gnome-shell)
MEM_START=$(cat /proc/$GNOME_PID/status | grep VmRSS | awk '{print $2}')
sleep 1800
MEM_END=$(cat /proc/$GNOME_PID/status | grep VmRSS | awk '{print $2}')
echo "GNOME Shell 内存变化: $((MEM_END - MEM_START)) kB"

# WE 退出恢复测试
# 关闭 WE → 等待 → 重新启动 WE → 确认壁纸恢复
echo "请手动关闭 WE，等待 10 秒后重新打开"

# 锁屏恢复测试
loginctl lock-session
sleep 10
loginctl unlock-session
sleep 5
echo "请确认壁纸是否恢复"
```

**Human Gate（最终验收，必须）**
确认以下所有项目：
- [ ] 视频类壁纸显示正常，正确循环
- [ ] Scene 类壁纸粒子/3D 效果正常，无撕裂
- [ ] 4K 分辨率下无缩放失真
- [ ] WE 窗口不出现在 Alt+Tab / 概览中
- [ ] 锁屏/解锁后壁纸自动恢复
- [ ] CPU 额外占用 < 2%
- [ ] 连续运行 2 小时无异常

**通过标准**
- [ ] 所有自动验收脚本通过
- [ ] Human Gate 全部确认
- [ ] DESIGN.md 实测数据已全部填写
- [ ] git tag 打上 `v1.0-stable`

---

## 紧急回滚流程

如果任何时候真实桌面出现问题：

**方案 A：从 TTY 禁用扩展**
```bash
# Ctrl+Alt+F2 进入 TTY
GSETTINGS_BACKEND=dconf gsettings set org.gnome.shell enabled-extensions \
  "$(gsettings get org.gnome.shell enabled-extensions | sed 's/,\?\s*we-wayland@local//')"
# Ctrl+Alt+F1 返回桌面
```

**方案 B：从 Btrfs 快照恢复**
```bash
# 在 TTY 或 live USB 中
sudo btrfs subvolume snapshot /.snapshots/pre-we-<date> /
# 重启
```

**方案 C：直接删除扩展**
```bash
rm -rf ~/.local/share/gnome-shell/extensions/we-wayland@local/
# 重启 GNOME Shell（X11 下 Alt+F2 → r；Wayland 下注销重登录）
```
