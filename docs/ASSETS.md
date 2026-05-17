# zero-native Assets 资源管理设计

## 一、问题背景

WebView 加载本地资源时面临两个问题：

1. **文件系统隔离** — WebView 运行在沙盒中，无法直接读取 `file://` 路径
2. **路径安全** — 直接暴露本地路径存在路径遍历风险

## 二、解决方案

**打包 = 资源复制 + 安全验证 + 元数据生成**

```
源目录 (前端构建输出)          打包目录 (注入 App Bundle)
┌──────────────────────┐      ┌──────────────────────────┐
│ frontend/dist/        │      │ zig-out/assets/          │
│  ├── index.html      │      │  ├── index.html          │
│  ├── assets/         │      │  ├── assets/             │
│  │   ├── app.js      │ ──→ │  │   ├── app.js          │
│  │   └── icon.png    │ ──→ │  │   └── icon.png        │
│  └── manifest.json   │      │  └── asset-manifest.zon  │
└──────────────────────┘      └──────────────────────────┘
                                          ↑
                              生成资源清单（含 hash）
```

## 三、数据结构

### Asset 资源单元

```zig
pub const Asset = struct {
    id:          []const u8,      // 资源标识符："assets/icon.png"
    kind:        AssetKind,       // 资源类型
    source_path: []const u8,      // 源路径
    bundle_path: []const u8,      // 打包路径
    byte_len:    u64,             // 字节大小
    hash:        Hash,            // SHA-256 哈希
    media_type:  ?[]const u8,     // MIME 类型
};
```

### AssetKind 资源类型

| 类型 | 扩展名 |
|------|--------|
| image | png, jpg, webp, gif, svg, bmp |
| font | ttf, otf, woff, woff2 |
| text | txt, md, csv |
| json | json |
| binary | bin, dat |
| localization | strings, ftl, po, mo |
| audio | mp3, wav, ogg, flac, m4a |
| video | mp4, webm, mov, mkv |

### Manifest 资源清单

```zig
pub const Manifest = struct {
    assets: []const Asset,

    // 按 id 查找
    pub fn findById(self, id) ?Asset

    // 按 bundle_path 查找
    pub fn findByBundlePath(self, path) ?Asset

    // 验证清单合法性
    pub fn validate(self) ManifestError!void
};
```

## 四、安全验证

### 资源 ID 规范

```
✅ 合法："assets/app.js"，"icons/app.png"，"fonts/inter.woff2"
❌ 非法：
   "/assets/app.js"    — 绝对路径
   "assets//app.js"   — 空路径段
   "assets/./app.js"  — 当前目录
   "assets/../app.js" — 父目录
   "icons/app@2x.png" — @ 字符非法
```

### 验证规则

```zig
pub fn validate(self: Manifest) ManifestError!void {
    // 1. 每个 id 必须合法（非绝对路径、无特殊字符）
    // 2. 不能有重复 id
    // 3. 不能有重复 bundle_path
    // 4. 必须按 id 字典序排序
}
```

## 五、打包流程

```
zero-native bundle-assets [app.zon] [assets] [output]
                                            │        │
                                            │        └─→ zig-out/assets/
                                            └─→ assets/ (源目录)
```

### 核心逻辑 (`tooling/assets.zig`)

```zig
pub fn bundle(allocator, io, assets_dir, output_dir) !BundleStats {
    // 1. 遍历源目录
    var walker = try assets_dir.walk(allocator);
    while (try walker.next(io)) |entry| {
        // 2. 复制文件到输出目录
        const bytes = try readFile(allocator, io, source_path);
        try writeFilePath(io, output_path, bytes);

        // 3. 记录资源信息（自动推断类型和 MIME）
        try copied.append(allocator, .{
            .id = entry.path,                              // "assets/app.js"
            .kind = inferKind(entry.path),                // .text
            .source_path = source_path,
            .bundle_path = entry.path,
            .byte_len = bytes.len,
            .hash = sha256(bytes),                        // SHA-256
            .media_type = inferMediaType(entry.path),     // null (JS 无 MIME)
        });
    }

    // 4. 生成 asset-manifest.zon
    try writeManifest(allocator, io, output_dir, copied.items);
    return .{ .asset_count = copied.items.len };
}
```

### 生成的 manifest 示例

```zig
// zig-out/assets/asset-manifest.zon
.{
  .assets = .{
    .{
      .id = "assets/app.js",
      .bundle_path = "assets/app.js",
      .source_path = "frontend/dist/assets/app.js",
      .byte_len = 2048,
      .hash = "9f86d081884c7d659a2feaa0c55ad015...",
      .media_type = null
    },
    .{
      .id = "icon.png",
      .bundle_path = "icon.png",
      .source_path = "frontend/dist/icon.png",
      .byte_len = 1024,
      .hash = "abc123def456...",
      .media_type = "image/png"
    },
  }
}
```

## 六、运行时使用

### app.zon 配置

```zig
.{
    .frontend = .{
        .dist = "frontend/dist",        // 打包目录
        .entry = "index.html",         // 入口文件
        .spa_fallback = true,           // 未匹配路由返回 index.html
    },
}
```

### 加载资源

```zig
// 方式 1：直接加载打包资源
const source = WebViewSource.assets(.{
    .root_path = "frontend/dist",
    .entry = "index.html",
    .origin = "zero://app",  // 特殊协议，绕过 CORS
});

// 方式 2：开发模式用 dev server
const source = WebViewSource.url("http://127.0.0.1:5173/");
```

## 七、SPA Fallback

当 `spa_fallback = true` 时：

```
请求：zero://app/users/123
  ↓
查找文件：frontend/dist/users/123
  ↓ 不存在
返回：frontend/dist/index.html
```

这样前端路由（React Router、Vue Router 等）可以正常工作。

## 八、设计价值

| 能力 | 说明 |
|------|------|
| **内容寻址** | SHA-256 hash 可验证完整性 |
| **安全路径** | 绝对路径，空段、路径遍历均被拒绝 |
| **类型推断** | 自动识别 MIME 类型，无需手动标注 |
| **SPA 支持** | fallback 机制支持前端路由 |
| **清单验证** | Manifest 可验证打包完整性 |

## 九、总结

```
┌──────────────────────────────────────────────────────────────┐
│                        构建阶段                               │
│  frontend/dist/  ──→  zig-out/assets/  +  asset-manifest.zon  │
│                                                              │
│  • 复制所有文件                                               │
│  • 生成 SHA-256 哈希                                         │
│  • 推断资源类型和 MIME                                        │
│  • 验证路径安全性                                             │
└──────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────┐
│                        运行时阶段                            │
│  Runtime 通过 Platform 加载 index.html                        │
│  WebView 通过 zero://app 协议访问资源                        │
└──────────────────────────────────────────────────────────────┘
```
