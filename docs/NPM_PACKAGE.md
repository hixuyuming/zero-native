# zero-native npm 包设计

## 为什么封装成 npm 包

**核心原因：npm 是分发预编译跨平台二进制最方便的方式。**

## 包结构

```
npm install -g zero-native
    │
    ▼
packages/zero-native/
├── bin/
│   ├── zero-native-darwin-x64           ← 预编译的 Zig 二进制
│   ├── zero-native-darwin-arm64
│   ├── zero-native-linux-x64
│   ├── zero-native-linux-arm64
│   ├── zero-native-linux-musl-x64       ← Alpine Linux (musl libc)
│   ├── zero-native-win32-x64.exe
│   └── zero-native.js                   ← Node.js wrapper (只有几百字节)
├── skill-data/                          ← AI skills 数据
├── skills/                              ← AI skills 源码
└── scripts/
    ├── postinstall.js                   ← npm install 时自动运行，下载对应平台二进制
    ├── sync-version.js                  ← 同步 package.json 版本到 Zig 代码
    └── ...
```

## Node.js wrapper

```javascript
// bin/zero-native.js (95 行)
import { spawn } from 'child_process';

const binaryPath = join(__dirname, getBinaryName());  // 根据平台选择二进制

const child = spawn(binaryPath, process.argv.slice(2), {
  stdio: 'inherit',
});
```

**本质就是一个加载器**：根据平台选择正确的预编译二进制，然后 `spawn` 执行。Node.js 本身不参与任何业务逻辑。

## postinstall.js 做了什么

`npm install` 时自动运行，从 GitHub Releases 下载对应平台的预编译二进制：

```javascript
// 下载地址格式
const DOWNLOAD_URL = `https://github.com/vercel-labs/zero-native/releases/download/v${version}/${binaryName}`;

// 支持的平台组合
// darwin-x64, darwin-arm64
// linux-x64, linux-arm64, linux-musl-x64  (musl = Alpine Linux)
// win32-x64
```

流程：
1. 检测当前平台 (`platform()` + `arch()`)
2. 判断 Linux 是否为 musl
3. 下载对应二进制到 `bin/`
4. 校验 SHA-256 checksum
5. 设置执行权限

## 为什么不用纯二进制分发

| 方案 | 问题 |
|------|------|
| 直接分发 .exe/.binary | 没有跨平台安装机制，用户不知道放哪里 |
| Homebrew/Linuxbrew | 需要维护 tap，跨平台支持麻烦 |
| npm | 前端开发者熟悉，`npm install -g` 就搞定，跨平台自动选二进制 |

## 关键设计点

1. **`bin` 字段** — `npm install -g` 自动创建 `zero-native` 命令
2. **postinstall 自动下载** — 用户不需要自己编译 Zig
3. **Node.js 只是 wrapper** — 实际执行的是预编译的 Zig 二进制，零运行时开销
4. **Skills 内嵌** — CLI 自带 skills，方便 AI agent 使用
5. **跨平台检测** — Linux musl (Alpine) vs glibc 分开

## 用户体验

```bash
# 安装（一条命令搞定）
npm install -g zero-native

# 使用
zero-native init my_app --frontend next
cd my_app && zig build run

# AI agent 使用
zero-native skills get core --full
```

## 本地开发

如果修改了 Zig CLI 代码，需要重新构建：

```bash
cd packages/zero-native
npm run build:native   # = zig build + 复制二进制到 bin/
```

## 发布流程

```bash
# 1. 版本已经通过 version:sync 同步到 Zig 代码
npm --prefix packages/zero-native run version:sync

# 2. GitHub Release CI 会：
#    - 构建所有平台二进制
#    - 上传到 GitHub Releases
#    - 发布 npm 包
```
