export type ZeroNativeJson =
  | null
  | boolean
  | number
  | string
  | ZeroNativeJson[]
  | { [key: string]: ZeroNativeJson };

export interface ZeroNativeInvokeError extends Error {
  code: "invalid_request" | "unknown_command" | "permission_denied" | "handler_failed" | "payload_too_large" | "internal_error" | string;
}

export interface ZeroNativeWindowInfo {
  id: number;
  label: string;
  title: string;
  open: boolean;
  focused: boolean;
  x: number;
  y: number;
  width: number;
  height: number;
  scale: number;
}

export interface ZeroNativeCreateWindowOptions {
  label?: string;
  title?: string;
  width?: number;
  height?: number;
  x?: number;
  y?: number;
  restoreState?: boolean;
  url?: string;
}

export interface ZeroNativeRect {
  x?: number;
  y?: number;
  width: number;
  height: number;
}

export interface ZeroNativeWebViewInfo {
  label: string;
  windowId: number;
}

export interface ZeroNativeCreateWebViewOptions {
  label?: string;
  windowId?: number;
  url: string;
  frame: ZeroNativeRect;
}

export interface ZeroNativeSetWebViewFrameOptions {
  label?: string;
  windowId?: number;
  frame: ZeroNativeRect;
}

export interface ZeroNativeNavigateWebViewOptions {
  label?: string;
  windowId?: number;
  url: string;
}

export interface ZeroNativeCloseWebViewOptions {
  label?: string;
  windowId?: number;
}

export interface ZeroNativeWebViewHandle extends ZeroNativeWebViewInfo {
  setFrame(frame: ZeroNativeRect): Promise<ZeroNativeWebViewInfo>;
  navigate(url: string): Promise<ZeroNativeWebViewInfo>;
  close(): Promise<ZeroNativeWebViewInfo>;
}

export interface ZeroNativeOpenFileOptions {
  title?: string;
  defaultPath?: string;
  allowDirectories?: boolean;
  allowMultiple?: boolean;
}

export interface ZeroNativeSaveFileOptions {
  title?: string;
  defaultPath?: string;
  defaultName?: string;
}

export interface ZeroNativeMessageDialogOptions {
  style?: "info" | "warning" | "critical";
  title?: string;
  message?: string;
  informativeText?: string;
  primaryButton?: string;
  secondaryButton?: string;
  tertiaryButton?: string;
}

export interface ZeroNativeApi {
  invoke<T = ZeroNativeJson>(command: string, payload?: ZeroNativeJson): Promise<T>;
  on<T = ZeroNativeJson>(name: string, callback: (detail: T) => void): () => void;
  off<T = ZeroNativeJson>(name: string, callback: (detail: T) => void): void;
  windows: {
    create(options?: ZeroNativeCreateWindowOptions): Promise<ZeroNativeWindowInfo>;
    list(): Promise<ZeroNativeWindowInfo[]>;
    focus(value: number | string): Promise<ZeroNativeWindowInfo>;
    close(value: number | string): Promise<ZeroNativeWindowInfo>;
  };
  webviews: {
    create(options: ZeroNativeCreateWebViewOptions): Promise<ZeroNativeWebViewHandle>;
    setFrame(options: ZeroNativeSetWebViewFrameOptions): Promise<ZeroNativeWebViewInfo>;
    navigate(options: ZeroNativeNavigateWebViewOptions): Promise<ZeroNativeWebViewInfo>;
    navigate(url: string): Promise<ZeroNativeWebViewInfo>;
    close(options?: ZeroNativeCloseWebViewOptions | string): Promise<ZeroNativeWebViewInfo>;
  };
  dialogs: {
    openFile(options?: ZeroNativeOpenFileOptions): Promise<string[] | null>;
    saveFile(options?: ZeroNativeSaveFileOptions): Promise<string | null>;
    showMessage(options?: ZeroNativeMessageDialogOptions): Promise<"primary" | "secondary" | "tertiary">;
  };
}

declare global {
  interface Window {
    zero: ZeroNativeApi;
  }
}

export {};
