const std = @import("std");
const build_options = @import("build_options");
const zero_native = @import("zero-native");

pub const RunOptions = struct {
    app_name: []const u8,
    window_title: []const u8 = "",
    bundle_id: []const u8,
    icon_path: []const u8 = "assets/icon.icns",
    bridge: ?zero_native.BridgeDispatcher = null,
    builtin_bridge: zero_native.BridgePolicy = .{},
    security: zero_native.SecurityPolicy = .{},

    fn appInfo(self: RunOptions) zero_native.AppInfo {
        return .{
            .app_name = self.app_name,
            .window_title = self.window_title,
            .bundle_id = self.bundle_id,
            .icon_path = self.icon_path,
        };
    }
};

pub fn runWithOptions(app: zero_native.App, options: RunOptions, init: std.process.Init) !void {
    if (comptime std.mem.eql(u8, build_options.platform, "macos")) {
        try runMacos(app, options, init);
    } else if (comptime std.mem.eql(u8, build_options.platform, "linux")) {
        try runLinux(app, options, init);
    } else if (comptime std.mem.eql(u8, build_options.platform, "windows")) {
        try runWindows(app, options, init);
    } else {
        try runNull(app, options, init);
    }
}

fn runNull(app: zero_native.App, options: RunOptions, init: std.process.Init) !void {
    var null_platform = zero_native.NullPlatform.initWithOptions(.{}, webEngine(), options.appInfo());
    try runRuntime(app, options, init, null_platform.platform());
}

fn runMacos(app: zero_native.App, options: RunOptions, init: std.process.Init) !void {
    var mac_platform = try zero_native.platform.macos.MacPlatform.initWithOptions(zero_native.geometry.SizeF.init(960, 720), webEngine(), options.appInfo());
    defer mac_platform.deinit();
    try runRuntime(app, options, init, mac_platform.platform());
}

fn runLinux(app: zero_native.App, options: RunOptions, init: std.process.Init) !void {
    var linux_platform = try zero_native.platform.linux.LinuxPlatform.initWithOptions(zero_native.geometry.SizeF.init(960, 720), webEngine(), options.appInfo());
    defer linux_platform.deinit();
    try runRuntime(app, options, init, linux_platform.platform());
}

fn runWindows(app: zero_native.App, options: RunOptions, init: std.process.Init) !void {
    var windows_platform = try zero_native.platform.windows.WindowsPlatform.initWithOptions(zero_native.geometry.SizeF.init(960, 720), webEngine(), options.appInfo());
    defer windows_platform.deinit();
    try runRuntime(app, options, init, windows_platform.platform());
}

fn runRuntime(app: zero_native.App, options: RunOptions, init: std.process.Init, platform: zero_native.Platform) !void {
    var runtime = zero_native.Runtime.init(.{
        .platform = platform,
        .bridge = options.bridge,
        .builtin_bridge = options.builtin_bridge,
        .security = options.security,
        .automation = if (build_options.automation) zero_native.automation.Server.init(init.io, ".zig-cache/zero-native-automation", options.window_title) else null,
    });
    try runtime.run(app);
}

fn webEngine() zero_native.WebEngine {
    if (comptime std.mem.eql(u8, build_options.web_engine, "chromium")) return .chromium;
    return .system;
}
