# Skills 系统详解

zero-native 的 `skills` 是一套**内置的 AI Agent 知识库**，让 Claude Code 等 AI 工具能够快速理解 zero-native 的开发方式，而无需每次都去阅读大量源码。

## 目录结构

```
skill-data/
├── core/                          # 核心 skill（最重要）
│   ├── SKILL.md                   # 主 skill 文件（必须存在）
│   └── references/               # 补充参考文档
│       ├── app-model-runtime.md
│       ├── bridge-security-native-capabilities.md
│       ├── frontend-assets.md
│       ├── project-anatomy.md
│       └── web-engines-packaging-debugging.md
│
└── automation/                     # 自动化测试 skill
    └── SKILL.md
```

## SKILL.md 格式

每个 skill 的 `SKILL.md` 必须以 YAML frontmatter 开头：

```yaml
---
name: core                          # skill 名称（唯一标识）
description: Core zero-native guide # 简短描述，供 list 时显示
---

# 文档内容（Markdown）
...
```

## CLI 命令

```bash
# 列出所有 skills
zero-native skills list

# 获取某个 skill 内容
zero-native skills get core

# 获取 skill 及其所有参考文档
zero-native skills get core --full

# 获取所有 skills
zero-native skills get --all --full
```

## 工作原理

`skills.zig` 的核心逻辑：

### 1. 查找 skills 目录 (`skills.zig:84-99`)

- 先检查 `ZERO_NATIVE_SKILLS_ROOT` 环境变量
- 否则从 CLI 可执行文件路径向上逐级查找 `skills/` 或 `skill-data/` 目录

### 2. 自动发现 (`skills.zig:118-158`)

- 遍历 `skills/` 和 `skill-data/` 目录
- 查找所有 `SKILL.md` 文件
- 解析 frontmatter 获取 `name` 和 `description`
- 按名称排序返回

### 3. 打印内容 (`skills.zig:225-252`)

- `--full` 时：先输出主 SKILL.md，再递归输出 `references/` 和 `templates/` 子目录中的所有文件

## 用途

当人类问 Claude Code 问题时，Claude Code 可以调用：

```bash
zero-native skills get core --full
```

获取完整的项目指南，包括：
- 项目结构和文件职责
- App/Runtime 的 mental model
- app.zon 配置详解
- 常见开发任务的操作步骤
- WebView/Bridge/Security 的实现细节
- 打包和调试方法

这样 Claude Code 就能**在没有看过这个项目的情况下**，通过 skills 快速获得正确的开发上下文，而不是凭"记忆"胡说八道。

## skill 路由

`core/SKILL.md` 中定义了任务路由表：

| 任务类型 | 使用的参考文档 |
|---------|---------------|
| 项目创建、生成文件、构建步骤 | `references/project-anatomy.md` |
| App、Runtime、回调、嵌入、测试 | `references/app-model-runtime.md` |
| React/Vue/Svelte/Next/Vite、dev server、打包资源 | `references/frontend-assets.md` |
| Bridge 命令、权限、窗口、WebView、对话框 | `references/bridge-security-native-capabilities.md` |
| Web 引擎选择、CEF、打包、签名、doctor、日志 | `references/web-engines-packaging-debugging.md` |

## 使用示例

```bash
# 安装 CLI 后，AI agent 可以这样获取上下文：
zero-native skills get core --full

# 输出大致如下：
# [SKILL.md 主内容]
#
# ---
# # references/project-anatomy.md
# [该文件内容]
#
# ---
# # references/app-model-runtime.md
# [该文件内容]
# ...
```

## 设计原则

这就是为什么 CLAUDE.md 中说"*不要发明新的 app 布局，参考示例和 generated 代码*"—因为 skills 系统本身就告诉 AI 要遵循这个项目的既定模式。

核心原则：
1. **AI First** — skills 是给 AI 看的，不是给人看的（人应该看 docs/ 或 zero-native.dev）
2. **自包含** — 一个 `zero-native skills get <name> --full` 就能获得完整上下文
3. **既定模式** — AI 必须遵循项目的生成代码和示例，不允许凭想象"创新"
