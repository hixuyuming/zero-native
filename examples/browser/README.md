# Browser Example

A no-build vanilla HTML/CSS/JS example that uses the layered WebViews API for isolated page content.

Run it from this directory:

```sh
zig build run
```

Or from the repository root:

```sh
zig build run-browser
```

The root command uses this example's system-WebView default unless you explicitly pass `-Dweb-engine=chromium`.

The frontend lives in `frontend/` and is served directly as `zero://app/index.html`.

The page content runs in an isolated child WebView. Page WebViews do not receive `window.zero`; all navigation and sizing is controlled by the browser chrome through the handle returned by `window.zero.webviews.create()`.

This example allows all navigation origins so it can browse arbitrary pages. Treat that as demo-only policy, not a production template. The chrome page keeps a strict CSP; arbitrary page navigation is controlled by the native navigation policy. Linux Chromium/CEF currently does not support layered child WebViews; use the system WebView backend on Linux for this example. `main` WebView layering is best effort and may be ignored by a backend. Windows WebView support is still in progress and does not yet support the `main` WebView resizing this example uses.
