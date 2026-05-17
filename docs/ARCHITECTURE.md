# zero-native 设计原理与核心执行流程详解

## 一、设计哲学

zero-native 的核心思想是**用 Zig 构建原生桌面应用外壳，让 Web 前端负责 UI 渲染**。它不捆绑浏览器运行时，而是利用平台原生 WebView（macOS 的 WKWebView、Linux 的 WebKitGTK），从而实现极小的二进制文件和瞬时启动。

```
┌─────────────────────────────────────────────────────────────┐
│                    zero-native 架构                          │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│   JavaScript (前端)                                          │
│       │                                                      │
│       │ window.zero.invoke()                                 │
│       ▼                                                      │
│   Bridge (bridge/root.zig) — 安全管理、命令路由、策略检查        │
│       │                                                      │
│       ▼                                                      │
│   Runtime (runtime/root.zig) — 事件循环、窗口管理、生命周期      │
│       │                                                      │
│       ▼                                                      │
│   Platform (platform/)  ─── macos / linux / windows          │
│       │                    (WebView 抽象层)                   │
│       ▼                                                      │
│   Native Surface (窗口、对话框、系统托盘、剪贴板等)              │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

## 二、核心概念

### 1. App — 应用入口点

```zig
pub const App = struct {
    context: *anyopaque,
    name: []const u8,
    source: platform.WebViewSource,     // HTML / URL / Assets
    source_fn: ?SourceFn = null,       // 动态 WebViewSource
    start_fn: ?StartFn = null,
    event_fn: ?EventFn = null,         // 事件回调
    stop_fn: ?StopFn = null,
};
```

App 描述了应用是什么（名称）、从哪里加载 Web 内容（source）、以及生命周期钩子（start/event/stop）。

### 2. Runtime — 运行时核心

```zig
pub const Runtime = struct {
    options: Options,
    surface: platform.Surface,           // 渲染表面（窗口）
    windows: [max_windows]RuntimeWindow,
    // ...
};
```

Runtime 拥有：
- **事件循环** — 驱动整个应用的 `app_start` → `surface_resized` → `frame_requested` → `app_shutdown` 生命周期
- **窗口管理** — 最多 16 个窗口（`RuntimeWindow`）
- **Bridge 分发** — 将 JS 调用路由到注册的 handler
- **平台服务** — 通过 `PlatformServices` 调用原生能力

### 3. WebViewSource — Web 内容来源

```zig
pub const WebViewSource = struct {
    kind: WebViewSourceKind,  // .html, .url, .assets
    bytes: []const u8,
    asset_options: ?WebViewAssetSource = null,
};

// 三种来源
WebViewSource.html("<p>Hello</p>")                    // 内联 HTML
WebViewSource.url("https://example.com")              // 远程 URL
WebViewSource.assets(.{ .root_path = "dist" })        // 本地打包资源（zero://app origin）
```

### 4. Bridge — JS ↔ Zig 双向桥接

**安全模型**：WebView 是**不受信任**的，所有跨边界调用必须经过：
1. **大小限制** — 消息最大 1MB，ID 最大 64 字节，命令名最大 128 字节
2. **Origin 检查** — 调用必须来自 `allowed_origins`（如 `zero://app`、`zero://inline`）
3. **权限检查** — 命令需要声明所需的权限（如 `"window"`）
4. **命令注册** — 命令必须在 `app.zon` 的 `.bridge.commands` 中注册

**请求格式**：
```json
{"id": "request-1", "command": "native.ping", "payload": {"source": "smoke"}}
```

**响应格式**：
```json
{"id": "request-1", "ok": true, "result": {"pong": "pong from Zig"}}
// 或
{"id": "request-1", "ok": false, "error": {"code": "permission_denied", "message": "..."}}
```

**Dispatcher 核心逻辑** (`bridge/root.zig:142`)：
```zig
pub fn dispatch(self: Dispatcher, raw: []const u8, source: Source, output: []u8) []const u8 {
    // 1. 大小检查
    if (raw.len > max_message_bytes) return error(...);

    // 2. 解析请求
    const request = parseRequest(raw) catch return error(...);

    // 3. 策略检查（origin + permission）
    if (!self.policy.allows(request.command, source.origin))
        return error(.permission_denied, ...);

    // 4. 查找并调用 handler
    const handler = self.registry.find(request.command) orelse
        return error(.unknown_command, ...);

    // 5. 执行并返回结果
    const result = handler.invoke_fn(...) catch |err|
        return error(.handler_failed, @errorName(err));
    return writeSuccessResponse(output, request.id, result);
}
```

## 三、核心执行流程

### 启动流程 (`zig build run`)

```
1. zig build run
   └─> build.zig: 执行 run_webview step
       └─> 调用 examples/webview/main.zig

2. main.zig
   ├─ 读取 app.zon 配置
   ├─ 创建 App 对象
   ├─ 创建 Runtime（传入 Platform）
   └─ 调用 runtime.run()

3. Runtime.run() — 事件循环
   ┌──────────────────────────────────────────────┐
   │ dispatchPlatformEvent(.app_start)             │
   │   └─> app.start_fn() — 用户初始化代码         │
   │                                              │
   │ dispatchPlatformEvent(.surface_resized)       │
   │   └─> 创建/调整原生窗口 surface               │
   │                                              │
   │ dispatchPlatformEvent(.window_frame_changed)  │
   │   └─> 通知窗口位置/大小变化                   │
   │                                              │
   │ [frame loop]                                 │
   │   dispatchPlatformEvent(.frame_requested)    │
   │     └─> app.event_fn(.frame)                │
   │     └─> 处理 bridge 命令                     │
   │                                              │
   │ dispatchPlatformEvent(.app_shutdown)         │
   │   └─> app.stop_fn() — 用户清理代码            │
   └──────────────────────────────────────────────┘
```

### 平台抽象 (Platform 接口)

```zig
pub const Platform = struct {
    context: *anyopaque,
    name: []const u8,
    surface_value: Surface,
    run_fn: *const fn (context, handler, handler_context) anyerror!void,
    services: PlatformServices,   // 原生能力抽象
    app_info: AppInfo,
};
```

三个平台实现：
- **`platform/macos/`** — WKWebView + Cocoa 窗口
- **`platform/linux/`** — WebKitGTK + GTK4 窗口
- **`platform/windows/`** — Windows WebView2

每个平台都实现了 `PlatformServices` 中的所有原生能力（剪贴板、对话框、窗口创建等）。

### 移动端嵌入 (C ABI)

通过 `embed/root.zig` 暴露 C 接口：

```c
// 移动端调用的 C API
void*  zero_native_app_create();           // 创建 app 实例
void   zero_native_app_destroy(void* app);   // 销毁
void   zero_native_app_start(void* app);    // 启动
void   zero_native_app_stop(void* app);     // 停止
void   zero_native_app_resize(void* app, float w, float h, float scale, void* surface);
void   zero_native_app_frame(void* app);    // 每帧调用
```

这使得 iOS/Android 原生应用可以加载 `libzero-native.a`，并在原生 UI 中嵌入 WebView。

## 四、安全策略

`security/root.zig` 定义了纵深防御：

```zig
pub const Policy = struct {
    permissions: []const []const u8,        // 全局权限声明
    navigation: NavigationPolicy = .{},     // 导航策略
};

pub const NavigationPolicy = struct {
    allowed_origins: []const []const u8,    // 允许的 origin
    external_links: ExternalLinkPolicy = .{}, // 外部链接处理
};
```

**内置权限**：
- `"window"` — 窗口操作（创建、聚焦、关闭）
- `"filesystem"` — 文件系统访问
- `"clipboard"` — 剪贴板读写
- `"network"` — 网络请求

**Child WebView 隔离**：
- 子 WebView 默认 bridge 隔离
- 信任的子 WebView 需显式设置 `bridge_enabled: true`
- 子 WebView 的导航受主窗口策略约束

## 五、app.zon 清单配置

所有应用元数据集中在 `app.zon`：

```zig
.{
    .id = "com.example.my-app",           // 唯一标识
    .name = "my-app",
    .web_engine = "system",                // system | chromium
    .permissions = .{ "window" },          // 全局权限
    .capabilities = .{ "webview", "js_bridge" },

    .bridge = .{
        .commands = .{
            .{ .name = "native.ping", .origins = .{ "zero://inline", "zero://app" } },
        },
    },

    .security = .{
        .navigation = .{
            .allowed_origins = .{ "zero://app", "http://127.0.0.1:5173" },
            .external_links = .{ .action = .deny },
        },
    },

    .windows = .{
        .{ .label = "main", .title = "My App", .width = 960, .height = 640 },
    },
}
```

## 六、WebView 分层架构

每个桌面窗口可以包含**多层 WebView**：

```zig
pub const WebViewOptions = struct {
    window_id: WindowId,
    label: []const u8,          // 唯一标识，如 "preview"
    url: []const u8,
    frame: geometry.RectF,       // 位置和尺寸
    layer: i32 = 0,             // 层级（支持叠加）
    transparent: bool = false,   // 透明背景
    bridge_enabled: bool = false, // 是否启用 bridge
};
```

`browser` 示例展示了如何用分层 WebView 实现标签页式浏览器。

## 七、架构层次总结

| 层次 | 职责 | 关键文件 |
|------|------|----------|
| **App** | 用户代码入口，定义 Web 来源和生命周期钩子 | `examples/*/main.zig` |
| **Runtime** | 事件循环、窗口管理、bridge 路由 | `src/runtime/root.zig` |
| **Bridge** | JS↔Zig 通信、策略检查、命令分发 | `src/bridge/root.zig` |
| **Platform** | WebView 抽象、平台服务实现 | `src/platform/{macos,linux,windows}/` |
| **Security** | Origin 检查、权限授予、导航策略 | `src/security/root.zig` |
| **Embed** | C ABI 供移动端嵌入 | `src/embed/root.zig` |

## 八、设计原则

1. **最小化运行时** — 使用平台 WebView，不捆绑浏览器
2. **安全第一** — WebView 视为不可信，所有跨边界调用都需策略检查
3. **清晰边界** — Platform 抽象隐藏平台差异，Runtime 统一事件模型
4. **移动可嵌入** — C ABI 设计支持 iOS/Android 原生嵌入
