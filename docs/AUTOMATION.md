# Automation 实现原理

Automation 是一个**双进程文件-based IPC** 系统：

```
┌─────────────────────────────────────────────────────────────────────┐
│                         ZERO-NATIVE APP (进程 1)                      │
│                                                                      │
│   Runtime 初始化时传入 Automation Server                             │
│         │                                                           │
│         ▼                                                           │
│   ┌─────────────────────────────────────────────────────────┐        │
│   │  Server.publish(Input)  ──→  写入文件                    │        │
│   │    • snapshot.txt      ←  运行时状态                     │        │
│   │    • windows.txt       ←  窗口列表                       │        │
│   │    • accessibility.txt ←  无障碍信息                      │        │
│   └─────────────────────────────────────────────────────────┘        │
│                                                                      │
│   Runtime 每帧检查 command.txt                                      │
│         │                                                           │
│         ▼                                                           │
│   ┌─────────────────────────────────────────────────────────┐        │
│   │  Server.takeCommand()  ←──  读取命令                    │        │
│   │    • "bridge {...}"    →  解析命令                      │        │
│   │    • "reload"          →  执行 reload                   │        │
│   │    • "wait"            →  返回等待                      │        │
│   └─────────────────────────────────────────────────────────┘        │
│         │                                                           │
│         ▼                                                           │
│   BridgeResponse 写入 bridge-response.txt                           │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘

                              文件系统
                    .zig-cache/zero-native-automation/
                              ↕↕↕↕↕↕↕↕

┌─────────────────────────────────────────────────────────────────────┐
│                    CLI / AI AGENT (进程 2)                           │
│                                                                      │
│   zero-native automate wait                                          │
│         │                                                           │
│         ▼                                                           │
│   轮询 snapshot.txt，直到 ready=true                                 │
│                                                                      │
│   zero-native automate bridge '{"id":"1",...}'                      │
│         │                                                           │
│         ▼                                                           │
│   写入 command.txt → 等待 bridge-response.txt                       │
│                                                                      │
│   zero-native automate snapshot                                      │
│         │                                                           │
│         ▼                                                           │
│   直接读取并打印 snapshot.txt 内容                                   │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

## 核心文件

| 文件 | 方向 | 内容 |
|------|------|------|
| `command.txt` | CLI → App | 命令：`reload`、`wait frame`、`bridge {...}` |
| `bridge-response.txt` | App → CLI | Bridge 响应 JSON |
| `snapshot.txt` | App → CLI | App 名、ready 状态、frame/command 计数 |
| `windows.txt` | App → CLI | 窗口列表 |
| `accessibility.txt` | App → CLI | 无障碍信息 |

## 协议格式 (`protocol.zig`)

```zig
// CLI 写入 command.txt
"bridge {\"id\":\"1\",\"command\":\"native.ping\",\"payload\":{}}\n"
"reload\n"
"wait\n"
```

## Server 端 (`server.zig`)

```zig
pub const Server = struct {
    io: std.Io,
    directory: []const u8 = ".zig-cache/zero-native-automation",
    title: []const u8 = "zero-native",

    // App 调用：发布运行时快照
    pub fn publish(self: Server, input: snapshot.Input) !void {
        // 写入 snapshot.txt, windows.txt, accessibility.txt
    }

    // App 调用：发布 bridge 响应
    pub fn publishBridgeResponse(self: Server, response: []const u8) !void {
        // 写入 bridge-response.txt
    }

    // App 调用：读取 CLI 命令
    pub fn takeCommand(self: Server, buffer: []u8) !?protocol.Command {
        // 读取 command.txt，解析命令，写入 "done" 标记
    }
};
```

## CLI 端 (`automation.zig`)

```zig
// zero-native automate wait
fn waitForFile(...) {
    while (attempts < 50) {
        // 读取 snapshot.txt
        // 检查是否包含 "ready=true"
        // 没有就 sleep 100ms 后重试
    }
}

// zero-native automate bridge '{"id":"1",...}'
fn sendCommand(allocator, io, "bridge", json)  // 写入 command.txt
waitForFile(..., "bridge-response.txt", "")   // 等待响应
```

## 时序图

```
App 启动 (automation=true)
    │
    ├─→ createDir(".zig-cache/zero-native-automation")
    │
    ├─→ Runtime.publish(snapshot)  → snapshot.txt (ready=true)
    │
    │   [帧循环]
    │   ├─→ takeCommand() ← command.txt (可能为空/"done")
    │   │
    │   └─→ 如果有 bridge 命令
    │         ├─→ 分发到 Bridge Dispatcher
    │         ├─→ Handler 执行
    │         └─→ publishBridgeResponse() → bridge-response.txt
    │

CLI: zero-native automate wait
    │
    └─→ 轮询 snapshot.txt 直到 ready=true

CLI: zero-native automate bridge '{"id":"1",...}'
    │
    ├─→ 写入 command.txt
    └─→ 轮询 bridge-response.txt 直到有内容
```

## 为什么用文件而不是内存

- **进程间通信** — App 和 CLI 是两个独立进程，文件是最简单的共享媒介
- **持久化** — 即使一方崩溃，文件仍在，方便调试
- **简单** — 不需要 socket、pipe、共享内存等复杂机制
