# Bridge 双向通信机制与核心调用流程

## 一、整体架构

```
┌─────────────────────────────────────────────────────────────────────┐
│                          WebView (JavaScript)                          │
│                                                                          │
│   window.zero.invoke(cmd, payload)                                       │
│        │                                                                 │
│        │  构造 {id, command, payload}                                  │
│        │  存入 pending.set(id, {resolve, reject})                       │
│        │  post() → 发送到 Native                                        │
│        │                                                                 │
│   window.zero._complete(response) ←── Native 调用此方法                │
│        │                                                                 │
│        │  取出 pending.get(id)                                         │
│        │  resolve(result) 或 reject(error)                               │
│        │                                                                 │
│   window.zero.on(event, callback)  ←── Native 可推送事件               │
│   window.zero._emit(name, detail) ←── Native 调用触发监听器             │
│                                                                          │
└─────────────────────────────────────────────────────────────────────┘
                                │
                    ┌───────────┴───────────┐
                    │                       │
              WKScriptMessageHandler    CEF V8Handler
                    │                       │
                    ▼                       ▼
┌─────────────────────────────────────────────────────────────────────┐
│                          Native (Objective-C)                          │
│                                                                          │
│   didReceiveScriptMessage(message)                                      │
│        │                                                                 │
│        │  解析 JSON → command/payload                                  │
│        ▼                                                                 │
│   BridgeDispatcher.dispatch()                                           │
│        │                                                                 │
│        │  1. 大小检查 (≤1MB)                                         │
│        │  2. 解析 JSON                                                 │
│        │  3. Origin 策略检查                                            │
│        │  4. Permission 检查                                           │
│        │  5. 查找 Handler                                              │
│        │  6. 调用 Handler                                              │
│        │                                                                 │
│   completeBridgeWithResponse(responseJSON)                              │
│        │                                                                 │
│        ▼                                                                 │
│   [webView evaluateJavaScript:"window.zero._complete(...)"]            │
│                                                                          │
└─────────────────────────────────────────────────────────────────────┘
```

## 二、用餐厅比喻理解

```
你 (JavaScript)                    服务员 (Bridge)                   厨房 (Zig Handler)
   │                                   │                               │
   │  我要一份宫保鸡丁！               │                               │
   │ ───────────────────────────────►│                               │
   │                                   │                               │
   │  好的，给您编号 #1，请稍等        │                               │
   │ ◄────────────────────────────────│                               │
   │                                   │                               │
   │                                   │  做宫保鸡丁                   │
   │                                   │ ────────────────────────────►│
   │                                   │                               │
   │                                   │  做好了！                     │
   │                                   │ ◄────────────────────────────│
   │                                   │                               │
   │  #1 好了，请慢用！               │                               │
   │ ◄────────────────────────────────│                               │
```

| 概念 | 点餐比喻 |
|------|----------|
| `invoke()` | 点菜，拿号牌 |
| `pending` Map | 等餐区，存着所有等的人 |
| `ID` | 餐号牌，防止拿错餐 |
| `_complete()` | 服务员叫号 |
| `resolve()` | 叫你取餐 |
| `Promise` | 你在等餐区等着 |

## 三、JS → Native 调用流程

### JavaScript 端

```javascript
// 1. 调用 invoke
const result = await window.zero.invoke("native.ping", { source: "test" });

// 内部实现
function invoke(command, payload) {
    // 生成唯一 ID
    const id = String(nextId++);

    // 构造请求
    const envelope = JSON.stringify({
        id: id,
        command: command,
        payload: payload
    });

    // 返回 Promise，存入 pending 表
    return new Promise((resolve, reject) => {
        pending.set(id, { resolve, reject });
        post(envelope);  // 发送到 Native
    });
}
```

### Native 端 (AppKit)

```objc
// 1. 接收消息
- (void)userContentController:(WKUserContentController *)controller
      didReceiveScriptMessage:(WKScriptMessage *)message {
    // 2. 解析 JSON
    NSDictionary *dict = [NSJSONSerialization JSONObjectWithData:message.body ...];
    NSString *command = dict[@"command"];
    NSDictionary *payload = dict[@"payload"];

    // 3. 分发给 BridgeDispatcher
    NSString *response = BridgeDispatcher.dispatch(
        command,
        payload,
        source.origin
    );

    // 4. 返回响应
    [self completeBridgeWithResponse:response windowId:windowId webViewLabel:label];
}
```

## 四、Native → JS 回调流程

### Native 端

```objc
// Native 处理完成后，调用此方法
- (void)completeBridgeWithResponse:(NSString *)response
                         windowId:(uint64_t)windowId
                      webViewLabel:(NSString *)webViewLabel {
    // 构建 JS 调用
    NSString *script = [NSString stringWithFormat:
        @"window.zero&&window.zero._complete(%@);", response];

    // 执行 JS
    [webView evaluateJavaScript:script completionHandler:nil];
}
```

### JavaScript 端

```javascript
// Native 调用此方法
window.zero._complete = function(response) {
    const { id, ok, result, error } = response;

    // 取出 Promise 并唤醒
    const pending = pendingMap.get(id);
    pendingMap.delete(id);

    if (ok) {
        pending.resolve(result);   // Promise 解决
    } else {
        pending.reject(error);     // Promise 拒绝
    }
};
```

## 五、Obj-C 调用 Zig，Zig 调用 JS 详解

### 整体调用链

```
JavaScript
    │  window.zero.invoke()
    ▼
Obj-C (appkit_host.m)     ← WKScriptMessageHandler 收到 JS 消息
    │  receiveBridgeMessage()
    ▼
C 函数 (bridgeCallback)   ← 函数指针调用
    ▼
Zig (macos/root.zig)      ← 收到事件，处理请求
    │
    │  处理完成，调用返回
    ▼
C 函数 (respond_*)        ← 导出给 Obj-C 的 C 函数
    ▼
Obj-C (appkit_host.m)     ← completeBridgeWithResponse()
    │  evaluateJavaScript("_complete(...)")
    ▼
JavaScript
    │  _complete() → Promise.resolve
```

### 1. Obj-C 调用 Zig（C 函数指针做桥梁）

**Zig 导出 C 函数 (Zig → C)**

```zig
// src/platform/macos/root.zig

// Zig 注册一个回调函数（供 Obj-C 调用）
zero_native_appkit_set_bridge_callback(self.host, appkitBridgeCallback, &self.state);

// 这是 Zig 定义的回调类型（导出给 C 用）
const AppKitBridgeCallback = *const fn (
    context: ?*anyopaque,
    window_id: u64,
    webview_label: [*]const u8,
    message: [*]const u8,
    origin: [*]const u8
) callconv(.c) void;

// Zig 的回调实现
fn appkitBridgeCallback(...) callconv(.c) void {
    const state: *RunState = @ptrCast(@alignCast(context.?));
    // 把事件发给 Runtime
    state.emit(.{ .bridge_message = .{...}});
}
```

**Obj-C 调用这个 C 回调**

```objc
// src/platform/macos/appkit_host.m

// Obj-C 收到 JS 消息
- (void)userContentController:(WKUserContentController *)controller
       didReceiveScriptMessage:(WKScriptMessage *)message {

    // 调用 Zig 传过来的 C 函数指针
    self.bridgeCallback(
        self.bridgeContext,
        windowId,
        label.UTF8String,
        messageData.bytes,    // JSON 消息
        origin.UTF8String
    );
}
```

**C 头文件声明**

```c
// src/platform/macos/appkit_host.h

// 这是 Obj-C 和 Zig 之间的"电话线"类型
typedef void (*zero_native_appkit_bridge_callback_t)(
    void *context,
    uint64_t window_id,
    const char *webview_label,
    const char *message,
    size_t message_len,
    const char *origin,
    size_t origin_len
);

// Zig 导出的设置回调的函数
void zero_native_appkit_set_bridge_callback(
    zero_native_appkit_host_t *host,
    zero_native_appkit_bridge_callback_t callback,
    void *context
);
```

### 2. Zig 调用 JavaScript

**Zig 调用 C 函数 (Zig → C)**

```zig
// src/platform/macos/root.zig

// Zig 处理完请求后，调用这个函数返回给 Obj-C
fn completeWindowBridge(context: ?*anyopaque, window_id: u64, response: []const u8) !void {
    const self: *MacPlatform = @ptrCast(@alignCast(context.?));

    // 调用 C 导出函数
    zero_native_appkit_bridge_respond_window(
        self.host,
        window_id,
        response.ptr,
        response.len
    );
}
```

**C 函数调用 Obj-C 方法 (C → Obj-C)**

```c
// src/platform/macos/appkit_host.m

// 这是给 Zig 调用的 C 函数
void zero_native_appkit_bridge_respond_window(
    zero_native_appkit_host_t *host,
    uint64_t window_id,
    const char *response,
    size_t response_len
) {
    // C 和 Obj-C 之间的转换
    ZeroNativeAppKitHost *object = (__bridge ZeroNativeAppKitHost *)host;
    NSString *responseString = [[NSString alloc]
        initWithBytes:response
               length:response_len
             encoding:NSUTF8StringEncoding];

    // 调用 Obj-C 方法
    [object completeBridgeWithResponse:responseString windowId:window_id];
}
```

**Obj-C 执行 JavaScript (Obj-C → JS)**

```objc
// src/platform/macos/appkit_host.m

- (void)completeBridgeWithResponse:(NSString *)response
                         windowId:(uint64_t)windowId
                      webViewLabel:(NSString *)webViewLabel {
    WKWebView *webView = [self webViewForWindowId:windowId];

    // 构造 JS 调用字符串
    NSString *script = [NSString stringWithFormat:
        @"window.zero&&window.zero._complete(%@);",
        response];

    // 执行 JavaScript！
    [webView evaluateJavaScript:script completionHandler:nil];
}
```

### 3. 为什么需要 C 函数指针做桥梁？

```
┌──────────────────────────────────────────────────────────────┐
│                      为什么需要 C 函数指针？                      │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  Zig 和 Obj-C 不能直接互相调用                                  │
│                                                              │
│  但它们都能调用 C！                                            │
│                                                              │
│  ┌─────────┐      ┌─────────┐      ┌─────────┐           │
│  │   Zig    │ ──── │    C    │ ──── │  Obj-C  │           │
│  └─────────┘      └─────────┘      └─────────┘           │
│       │                                    │                 │
│       │  导出函数指针                      │  声明并调用    │
│       └────────────────────────────────────┘                 │
│                                                              │
│  Zig 导出函数指针，Obj-C 保存，Obj-C 调用时传入消息            │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

## 六、完整异步调用时序

```
JS                              Native                           Handler
 │                                │                                │
 │  invoke("cmd", {a:1})        │                                │
 │ ─────────────────────────────►│                                │
 │                                │                                │
 │  返回 Promise (等餐)          │                                │
 │ ◄────────────────────────────│                                │
 │                                │                                │
 │                                │  解析 JSON                     │
 │                                │ ─────────────────────────────►│
 │                                │                                │
 │                                │  策略检查                      │
 │                                │  (origin + permission)        │
 │                                │                                │
 │                                │  Handler.invoke()             │
 │                                │ ─────────────────────────────►│
 │                                │                                │
 │                                │                    ┌───────────┘
 │                                │                    │
 │                                │        ┌──────────┘
 │                                │        │  返回 JSON 结果
 │                                │◄──────────────────────────────│
 │                                │                                │
 │  _complete({ok:true, result})  │                                │
 │ ◄────────────────────────────│                                │
 │                                │                                │
 │  Promise.resolve(result)       │                                │
 │ ─────────────────────────────►│                                │
```

## 七、安全策略

```
┌──────────────────────────────────────────────────────────────┐
│                    Bridge 安全检查                               │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  1. 消息大小限制                                             │
│     最大 1MB，超出 → payload_too_large                      │
│                                                              │
│  2. Origin 检查                                             │
│     调用来源必须匹配 allowed_origins                        │
│     zero://app / zero://inline / http://127.0.0.1:5173     │
│                                                              │
│  3. Permission 检查                                          │
│     命令声明所需权限，全局权限必须包含                        │
│     window / filesystem / clipboard / network                │
│                                                              │
│  4. 命令注册                                                 │
│     必须先在 app.zon 的 .bridge.commands 中声明              │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

## 八、app.zon 配置

```zig
.{
    .permissions = .{ "window" },       // 全局权限

    .bridge = .{
        .commands = .{
            // 无权限要求
            .{
                .name = "native.ping",
                .origins = .{ "zero://app", "zero://inline" }
            },
            // 需要 window 权限
            .{
                .name = "zero-native.window.create",
                .permissions = .{ "window" },
                .origins = .{ "zero://app" }
            },
        },
    },
}
```

## 九、事件推送机制

### Native 推送事件

```objc
// Native 主动发送事件
- (void)emitEventNamed:(NSString *)name
            detailJSON:(NSString *)detailJSON
               windowId:(uint64_t)windowId {
    NSString *script = [NSString stringWithFormat:
        @"window.zero&&window.zero._emit(%@, %@);",
        nameJSON, detailJSON];
    [webView evaluateJavaScript:script completionHandler:nil];
}
```

### JavaScript 监听

```javascript
// 方式 1: 通过 zero API
window.zero.on('windowFocused', (data) => {
    console.log('窗口聚焦:', data);
});

// 方式 2: 通过 DOM 事件
window.addEventListener('zero-native:windowFocused', (e) => {
    console.log('窗口聚焦:', e.detail);
});
```

## 十、总结

```
┌──────────────────────────────────────────────────────────────┐
│                      Bridge 双向通信                              │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  JS → Native:                                               │
│  window.zero.invoke() → postMessage → WKScriptMessageHandler │
│                                                              │
│  Native → JS:                                              │
│  evaluateJavaScript(_complete) → Promise.resolve()         │
│                                                              │
│  核心机制:                                                  │
│  • Promise + pending Map 实现异步回调                        │
│  • ID 配对请求和响应                                        │
│  • 策略检查确保安全                                         │
│  • C 函数指针做 Zig/Obj-C 桥梁                             │
│                                                              │
└──────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────┐
│  Obj-C 调用 Zig:                                              │
│  Obj-C 收到 JS 消息 → 调用 bridgeCallback 函数指针 → Zig 收到 │
│                                                              │
│  Zig 调用 JS:                                               │
│  Zig 调用 respond_window() → Obj-C 收到 → evaluateJavaScript │
│                                                              │
│  桥梁: C 函数指针 (因为 Zig 和 Obj-C 不能直接互调)           │
└──────────────────────────────────────────────────────────────┘
```
