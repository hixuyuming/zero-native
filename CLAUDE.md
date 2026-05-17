# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

**Important:** Claude Code does not know zero-native from general model knowledge. When working with this codebase, **read the `core` skill first** using the command below. Do not rely on assumed knowledge.

## AI Agent Skills

This project includes a built-in skill system for AI agents. Claude Code should use this to get accurate project context:

```bash
# First: get the core skill with all reference documents
zero-native skills get core --full

# List all available skills
zero-native skills list

# Get the automation skill (for testing/WebView inspection)
zero-native skills get automation
```

The `core` skill provides detailed guidance on: App/Runtime patterns, app.zon configuration, bridge commands, security policy, frontend integration, web engines, packaging, and debugging. Always read it before explaining zero-native or making implementation changes.

## What is zero-native

zero-native is a Zig desktop app shell for modern web frontends. It renders web UI in a native WebView (WKWebView on macOS, WebKitGTK on Linux) or bundles Chromium via CEF for consistent cross-platform rendering. The native layer is Zig with a C ABI for embedding in mobile hosts.

## Build Commands

```bash
# Run all framework tests
zig build test

# Validate app.zon manifest
zig build validate

# Run examples (from repo root)
zig build run-webview    # WebView example with bridge, automation, security policy
zig build run-hello     # Minimal desktop shell with inline HTML
zig build run-browser   # Browser example with layered WebViews

# Development
zig build dev           # Run managed frontend dev server and native shell
zig build doctor        # Print platform diagnostics

# Packaging
zig build package       # Create local package artifact
zig build lib           # Build embeddable static library (libzero-native.a)
zig build cef-bundle    # Copy CEF framework into zig-out for local dev

# Test specific modules
zig build test-geometry
zig build test-assets
zig build test-app-dirs
zig build test-trace
zig build test-desktop

# WebView smoke tests (macOS)
zig build test-webview-system-link
zig build test-webview-cef-smoke
zig build test-package-cef-layout
```

## Architecture

### Core Concepts

- **`App`** — Zig object describing the application: name, WebView source, lifecycle hooks, and optional native services
- **`Runtime`** — Owns the event loop, windows, bridge dispatch, automation hooks, tracing, and platform services
- **`WebViewSource`** — What to load: inline HTML, a URL, or packaged frontend assets from a local app origin
- **`window.zero.invoke()`** — The JavaScript-to-Zig bridge with size limits, origin checking, permission checking, and routing to registered handlers

### Source Structure (`src/`)

- **`primitives/`** — Low-level building blocks: `geometry`, `assets`, `app_dirs`, `trace`, `app_manifest`, `diagnostics`, `platform_info`, `json`
- **`platform/`** — Platform-specific WebView implementations: `macos/`, `linux/`, `windows/`, with `root.zig` as the unified interface
- **`runtime/`** — Event loop, window management, command routing, and `App`/`Runtime` types
- **`bridge/`** — JavaScript-to-Zig bridge dispatch, policy enforcement, and command registration
- **`security/`** — Security policy: navigation policy, external link policy, permission grants
- **`embed/`** — C ABI entry points (`zero_native_app_create`, etc.) for embedding in iOS/Android hosts
- **`automation/`** — Automation server for external process control of running apps
- **`extensions/`** — Extension module registry for optional capabilities
- **`window_state/`** — Window state persistence and restore logic
- **`frontend/`** — Frontend asset handling and dev server integration
- **`tooling/`** — Build-time tooling: manifest validation, asset bundling, packaging

### `app.zon` — App Manifest

Most project-level behavior lives in `app.zon`:
- `.id`, `.name`, `.display_name`, `.version` — app metadata
- `.web_engine` — `"system"` (platform WebView) or `"chromium"` (bundled CEF)
- `.permissions` — required capabilities (e.g., `"window"`)
- `.capabilities` — enabled features (`"webview"`, `"js_bridge"`, `"native_module"`)
- `.security` — navigation origins, external link policy
- `.windows` — window configuration (label, title, dimensions, restore policy)
- `.cef` — Chromium Embedded Framework config (directory, auto-install)

### Web Engine Selection

- **System WebView** — Uses platform WebView (WKWebView on macOS, WebKitGTK on Linux). Smallest binaries, best startup time. Default for development.
- **Chromium/CEF** — Bundled Chromium for consistent cross-platform rendering. Use when you need pinned web platform or WebView feature parity across platforms.

### Bridge Security Model

The WebView is treated as untrusted. Bridge commands must be:
1. Registered in `app.zon` under `.bridge.commands`
2. Permitted via `.permissions` on the command
3. Originated from an allowed origin in `.security.navigation.allowed_origins`

## Examples (`examples/`)

- `hello` — Minimal shell with inline HTML
- `webview` — Bridge commands, window APIs, security policy, automation, optional CEF
- `browser` — Layered WebViews with browser-style controls
- `react`, `svelte`, `vue`, `next` — Framework projects with dev server workflows
- `ios`, `android` — Mobile embedding via C ABI (`libzero-native.a`)

## Release Process

Releases are manual, single-PR. Version lives in `packages/zero-native/package.json`.

```bash
# 1. Create release branch
git checkout -b prepare-v1.2.0

# 2. Bump version in package.json and run sync
npm --prefix packages/zero-native run version:sync

# 3. Write changelog entry in CHANGELOG.md
# Wrap new entry in <!-- release:start --> and <!-- release:end --> markers

# 4. Remove markers from previous release entry

# 5. Open PR, merge to main
# CI compares package.json version to npm; if different, publishes and creates GitHub release
```

## Prerequisites

- Zig 0.16.0+
- Node.js + npm (CLI package and generated frontend projects)
- pnpm (documentation site)
- macOS 11+ (WKWebView and CEF development)
- Linux: GTK4 + WebKitGTK 6 (system WebView development)

## Contributing

Sign commits cryptographically (`git commit -S`) so they show as **Verified** on GitHub. PRs go against `main`. See `CONTRIBUTING.md` for full guidance on local checks, WebView development, packaging, and automation workflows.
