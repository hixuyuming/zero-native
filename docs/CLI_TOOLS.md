# tools/zero-native/*.zig CLI 工具详解

CLI 是用 Zig 编写的独立程序，负责项目创建、打包、验证等任务。

## 文件概览

| 文件 | 行数 | 职责 |
|------|------|------|
| `main.zig` | 298 | CLI 主入口，路由所有子命令 |
| `automation.zig` | 102 | `automate` 子命令 |
| `skills.zig` | 253 | `skills` 子命令 |

---

## main.zig — CLI 主入口

### 命令路由总览

```
┌─────────────────────────────────────────────────────────────────────┐
│                    zero-native CLI 命令路由                           │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│   zero-native init [--frontend next] [path]                          │
│       └─→ tooling.templates.writeDefaultApp()                       │
│                                                                      │
│   zero-native doctor [--strict] [--manifest app.zon]                 │
│       └─→ tooling.doctor.run()                                      │
│                                                                      │
│   zero-native validate [app.zon]                                    │
│       └─→ tooling.manifest.validateFile()                            │
│                                                                      │
│   zero-native bundle-assets [app.zon] [assets] [output]             │
│       └─→ tooling.assets.bundle()                                  │
│                                                                      │
│   zero-native package [--target macos] [--output path] [...]        │
│       └─→ tooling.package.createPackage()                           │
│                                                                      │
│   zero-native dev [--manifest app.zon] [--binary path]               │
│       └─→ tooling.dev.run()                                        │
│                                                                      │
│   zero-native cef install|doctor|path [...]                          │
│       └─→ tooling.cef.run()                                        │
│                                                                      │
│   zero-native automate <wait|snapshot|bridge|reload|list>           │
│       └─→ automation_cli.run()                                      │
│                                                                      │
│   zero-native skills list|get                                        │
│       └─→ skills_cli.run()                                          │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

### 命令分发逻辑

```zig
pub fn main(init: std.process.Init) !void {
    const args = try init.minimal.args.toSlice(allocator);

    const command = args[1];  // 第一个参数是子命令名

    if (std.mem.eql(u8, command, "init")) {
        // ...
    } else if (std.mem.eql(u8, command, "doctor")) {
        try tooling.doctor.run(allocator, init.io, init.environ_map, args[2..]);
    } else if (std.mem.eql(u8, command, "validate")) {
        const result = try tooling.manifest.validateFile(allocator, init.io, path);
        tooling.manifest.printDiagnostic(result);
    } else if (std.mem.eql(u8, command, "package")) {
        // ...
    }
    // ...
}
```

### 辅助函数

```zig
// 解析 --flag value 格式的参数
fn flagValue(args: []const []const u8, name: []const u8) ?[]const u8

// 解析 --flag 布尔参数
fn flagBool(args: []const []const u8, name: []const u8) bool

// 解析位置参数（跳过 --flag）
fn positionalArg(args: []const []const u8) ?[]const u8

// 分割 "npm run dev" 字符串为 ["npm", "run", "dev"]
fn splitCommand(allocator, value: []const u8) [][]const u8

// 查找 CLI 可执行文件旁边的框架根目录
fn frameworkRootFromExecutable(allocator, io) ?[]const u8
```

---

## automation.zig — 运行时控制 CLI

实现 `zero-native automate <command>`：

```zig
// 命令列表
const commands = [_]struct{ name, fn }{
    .{ "list",     printFile("windows.txt") },
    .{ "snapshot",  printFile("snapshot.txt") },
    .{ "reload",   sendCommand("reload") },
    .{ "wait",     waitForFile("snapshot.txt", "ready=true") },
    .{ "bridge",   sendCommand("bridge", json) + waitForFile("bridge-response.txt") },
};
```

### 核心实现

```zig
// 发送命令到 running app
fn sendCommand(allocator, io, action, value) !void {
    const line = try protocol.commandLine(action, value, buffer);
    try cwd.createDirPath(io, automation_dir);
    try cwd.writeFile(io, .{
        .sub_path = "command.txt",
        .data = line,
    });
}

// 轮询文件直到包含特定内容
fn waitForFile(allocator, io, name, marker) !void {
    while (attempts < 50) {
        const bytes = readFile(allocator, io, name) catch {
            try sleep(100ms);
            continue;
        };
        if (marker.len == 0 or contains(bytes, marker)) {
            std.debug.print("{s}", .{bytes});
            return;
        }
        try sleep(100ms);
    }
    return fail("timed out waiting for automation");
}
```

---

## skills.zig — AI Skills CLI

实现 `zero-native skills <list|get>`：

```zig
pub fn run(allocator, io, env_map, args) !void {
    // 1. 查找 skills 目录位置
    const root = try findPackageRoot(allocator, io, env_map);

    // 2. 发现所有 skills
    var skills = try discoverSkills(allocator, io, root);

    if (std.mem.eql(u8, command, "list")) {
        // 打印所有 skill 名称和描述
        try printSkillList(stdout, skills.items);
    } else if (std.mem.eql(u8, command, "get")) {
        if (include_supplementary) {
            // 打印 skill + references/ + templates/
            try printSkill(..., --full);
        }
    }
}
```

### 查找 skills 目录

```zig
fn findPackageRoot(allocator, io, env_map) ?[]const u8 {
    // 1. 先检查环境变量 ZERO_NATIVE_SKILLS_ROOT
    if (env_map.get("ZERO_NATIVE_SKILLS_ROOT")) |root| {
        if (hasSkillDirs(root)) return root;
    }

    // 2. 从 CLI 可执行文件路径向上搜索
    var dir = dirname(executablePath());
    while (true) {
        if (hasSkillDirs(dir)) return dir;
        dir = dirname(dir) orelse break;
    }
    return null;
}
```

### 发现 skills

```zig
fn discoverSkills(allocator, io, root) ![]SkillInfo {
    // 遍历 skill_dirs = ["skills", "skill-data"]
    for (skill_dirs) |dir_name| {
        // 递归查找所有 SKILL.md 文件
        // 解析 frontmatter 获取 name/description
    }
}
```

### SkillInfo 结构

```zig
const SkillInfo = struct {
    name: []const u8,        // "core", "automation"
    description: []const u8, // 简短描述
    dir: []const u8,         // skill 目录路径
    hidden: bool,            // 是否隐藏
};
```

---

## tooling 模块 — CLI 依赖的核心库

CLI 依赖 `src/tooling/` 中的模块：

| 模块 | 大小 | 职责 |
|------|------|------|
| `manifest.zig` | 24KB | 解析/验证 `app.zon` |
| `package.zig` | 43KB | 创建 macOS/Linux/Windows/iOS/Android 包 |
| `templates.zig` | 87KB | 生成项目模板（React/Vue/Svelte/Next/Vite） |
| `cef.zig` | 30KB | Chromium Embedded Framework 安装/配置 |
| `doctor.zig` | 16KB | 诊断平台环境 |
| `dev.zig` | 6KB | 管理前端 dev server |
| `assets.zig` | 6KB | 打包前端资源 |
| `web_engine.zig` | 5KB | 解析 web engine 配置 |

### 模块关系

```
main.zig
├── tooling.manifest  ──→ app.zon 解析和验证
├── tooling.templates ──→ 项目脚手架生成
├── tooling.package   ──→ 打包成 .app / .exe 等
├── tooling.cef       ──→ CEF 下载和配置
├── tooling.doctor    ──→ 环境诊断
├── tooling.dev       ──→ dev server 管理
├── tooling.assets    ──→ 资源打包
└── tooling.web_engine ──→ Web 引擎选择
```

---

## 总结

`tools/zero-native/` 是 **CLI 工具集**：

1. **main.zig** — 命令路由器，将 `zero-native <cmd>` 分发到对应实现
2. **automation.zig** — 运行时控制，文件 IPC 的 CLI 端
3. **skills.zig** — AI 知识库 CLI，用于给 AI agent 提供项目上下文
4. **tooling/** — 核心库，包含 app.zon 解析、项目模板、打包、诊断等

所有代码都是 **Zig**，编译成预置在 npm 包中的跨平台二进制，用户通过 `npm install -g zero-native` 安装后即可使用。
