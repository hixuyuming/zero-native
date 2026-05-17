# dev server 管理设计

## 一、核心问题

开发时前端需要 HMR（热模块替换），但打包后的静态资源不支持这个。所以需要：
- **开发模式** → 指向本地 dev server（如 Vite、Next.js）
- **生产模式** → 指向打包后的静态资源

## 二、解决方案

`dev.zig` 实现了**同时启动 dev server 和 app** 的管理：

```
┌─────────────────────────────────────────────────────────────┐
│                    zero-native dev                            │
│                                                              │
│  1. 启动 dev server (Vite/Next.js 等)                       │
│     └─→ 等待就绪（轮询 HTTP 响应）                          │
│                                                              │
│  2. 设置环境变量                                             │
│     ZERO_NATIVE_FRONTEND_URL=http://127.0.0.1:5173/        │
│     ZERO_NATIVE_MODE=dev                                    │
│     ZERO_NATIVE_HMR=1                                       │
│                                                              │
│  3. 启动 app（读取环境变量决定加载源）                        │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

## 三、核心流程

```zig
pub fn run(allocator, io, options) !void {
    // 1. 从 app.zon 读取 dev 配置
    const dev = options.metadata.frontend.dev;

    // 2. 启动 dev server 子进程
    var dev_child = try std.process.spawn(io, .{
        .argv = dev.command,  // ["npm", "run", "dev"]
        .stdout = .inherit,
        .stderr = .inherit,
    });
    defer dev_child.kill();  // 退出时杀掉 dev server

    // 3. 等待 dev server 就绪
    try waitUntilReady(io, dev.url, dev.ready_path, dev.timeout_ms);

    // 4. 构造环境变量
    try env.put("ZERO_NATIVE_FRONTEND_URL", dev.url);
    try env.put("ZERO_NATIVE_MODE", "dev");
    try env.put("ZERO_NATIVE_HMR", "1");

    // 5. 启动 app（传入环境变量）
    var app_child = try std.process.spawn(io, .{
        .argv = &[1][]const u8{binary_path},
        .environ_map = &env,
    });

    // 6. 等待 app 退出
    _ = try app_child.wait(io);
}
```

## 四、等待 dev server 就绪

轮询 HTTP 请求，直到收到成功响应：

```zig
fn waitUntilReady(io, url, ready_path, timeout_ms) !void {
    while (waited_ms <= timeout_ms) {
        // 1. 解析 URL 获取 host/port
        const parts = try parseHttpUrl(url);

        // 2. TCP 连接
        if (connect(host, port)) |stream| {
            // 3. 发送 HTTP GET
            if (httpReady(stream, path)) {
                return;  // 就绪
            }
        }
        sleep(100ms);
    }
    return error.Timeout;
}

// 发送 HTTP 请求检查是否就绪
fn httpReady(stream, host, path) bool {
    // 发送：GET / HTTP/1.1\r\nHost: host\r\n\r\n
    // 成功响应：HTTP/1.1 2xx 或 3xx
    return startsWith(response, "HTTP/1.1 2") ||
           startsWith(response, "HTTP/1.0 2") ||
           startsWith(response, "HTTP/1.1 3") ||
           startsWith(response, "HTTP/1.0 3");
}
```

## 五、app.zon 中的 dev 配置

```zig
.{
    .frontend = .{
        .dist = "frontend/dist",      // 生产资源
        .entry = "index.html",
        .spa_fallback = true,
        .dev = .{
            .url = "http://127.0.0.1:5173/",   // Vite 默认
            .command = .{ "npm", "run", "dev", "--", "--host", "127.0.0.1" },
            .ready_path = "/",                   // 检查这个路径
            .timeout_ms = 30000,                  // 30 秒超时
        },
    },
}
```

## 六、环境变量作用

| 环境变量 | 值 | 作用 |
|---------|-----|------|
| `ZERO_NATIVE_FRONTEND_URL` | `http://127.0.0.1:5173/` | 告诉 Runtime 加载哪个 URL |
| `ZERO_NATIVE_MODE` | `dev` | 标记开发模式 |
| `ZERO_NATIVE_HMR` | `1` | 启用 HMR 支持 |

## 七、运行时 source 决策

```zig
// Runtime 通过 frontend.sourceFromEnv() 决定加载源
fn sourceFromEnv(env_map) WebViewSource {
    if (env_map.get("ZERO_NATIVE_FRONTEND_URL")) |url| {
        return WebViewSource.url(url);  // dev server URL
    }
    return WebViewSource.assets(.{   // 否则用打包资源
        .root_path = config.dist,
        .entry = config.entry,
    });
}
```

## 八、整体架构

```
┌─────────────────────────────────────────────────────────────────┐
│                         开发模式                                  │
│                                                                  │
│  zero-native dev                                                │
│       │                                                         │
│       ├─→ 启动 npm run dev (Vite)                              │
│       │     └─→ 等待 http://127.0.0.1:5173/ 就绪              │
│       │                                                         │
│       └─→ 启动 app binary                                       │
│             └─→ ZERO_NATIVE_FRONTEND_URL=http://...            │
│                   └─→ WebView 加载 dev server URL              │
│                                                                  │
│  WebView ←→ Vite (HMR)                                         │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│                         生产模式                                  │
│                                                                  │
│  zig build run                                                  │
│       │                                                         │
│       └─→ 启动 app binary                                        │
│             └─→ WebViewSource.assets({.root_path = "dist"})   │
│                   └─→ WebView 加载打包后的 index.html          │
│                                                                  │
│  WebView ←→ 静态资源 (zero://app 协议)                          │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

## 九、设计价值

| 特性 | 说明 |
|------|------|
| **HMR 支持** | 前端修改即时生效，无需重启 app |
| **自动等待** | dev server 启动慢，CLI 轮询等待就绪再启动 app |
| **环境隔离** | dev/prod 使用不同加载源，代码共享 |
| **进程管理** | app 退出时自动 kill dev server |
| **超时保护** | 避免 dev server 无限等待 |

## 十、总结

```
┌──────────────────────────────────────────────────────────────┐
│  问题：开发时需要 HMR，打包后不需要                          │
│  解法：dev 命令同时管理 dev server 和 app 生命周期          │
│                                                              │
│  流程：                                                      │
│  1. 启动 dev server (npm run dev)                           │
│  2. 轮询等待就绪 (HTTP 轮询)                                │
│  3. 设置环境变量 ZERO_NATIVE_FRONTEND_URL                   │
│  4. 启动 app，WebView 加载 dev server URL                   │
│  5. app 退出时自动 kill dev server                          │
└──────────────────────────────────────────────────────────────┘
```
