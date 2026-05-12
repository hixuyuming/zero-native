const OVERLAY_LABEL = "browser-page";

const form = document.querySelector("#browser-form");
const address = document.querySelector("#address");
const backButton = document.querySelector("#back");
const forwardButton = document.querySelector("#forward");
const reloadButton = document.querySelector("#reload");
const status = document.querySelector("#status");
const emptyState = document.querySelector("#empty-state");

let overlay = null;
let currentUrl = "";
let history = [];
let historyIndex = -1;
let resizeHandle = 0;

function setStatus(message) {
  status.textContent = message;
}

function normalizeUrl(value) {
  const trimmed = value.trim();
  if (!trimmed) return "https://example.com";
  if (/^[a-z][a-z0-9+.-]*:\/\//i.test(trimmed)) return trimmed;
  return `https://${trimmed}`;
}

function overlayFrame() {
  const toolbar = document.querySelector(".toolbar");
  const height = Math.ceil(toolbar.getBoundingClientRect().height);
  return {
    x: 0,
    y: height,
    width: Math.max(1, Math.floor(window.innerWidth)),
    height: Math.max(1, Math.floor(window.innerHeight - height)),
  };
}

function updateHistoryButtons() {
  backButton.disabled = historyIndex <= 0;
  forwardButton.disabled = historyIndex < 0 || historyIndex >= history.length - 1;
}

function remember(url) {
  if (history[historyIndex] === url) return;
  history = history.slice(0, historyIndex + 1);
  history.push(url);
  historyIndex = history.length - 1;
  updateHistoryButtons();
}

async function ensureOverlay(url) {
  const frame = overlayFrame();
  if (!overlay) {
    overlay = await window.zero.webviews.create({
      label: OVERLAY_LABEL,
      url,
      frame,
    });
    emptyState.hidden = true;
  } else {
    await overlay.setFrame(frame);
    await overlay.navigate(url);
  }
}

async function navigateTo(url, options = {}) {
  const target = normalizeUrl(url);
  address.value = target;
  setStatus(`Loading ${target}`);
  try {
    await ensureOverlay(target);
    currentUrl = target;
    if (options.record !== false) remember(target);
    setStatus(`Showing ${target}`);
  } catch (error) {
    const code = error && error.code ? `${error.code}: ` : "";
    setStatus(`${code}${error.message || "Navigation failed"}`);
  }
}

function scheduleResize() {
  if (resizeHandle) cancelAnimationFrame(resizeHandle);
  resizeHandle = requestAnimationFrame(async () => {
    resizeHandle = 0;
    if (!overlay) return;
    try {
      await overlay.setFrame(overlayFrame());
    } catch (error) {
      setStatus(error.message || "Failed to resize page WebView");
    }
  });
}

form.addEventListener("submit", (event) => {
  event.preventDefault();
  navigateTo(address.value);
});

backButton.addEventListener("click", () => {
  if (historyIndex <= 0) return;
  historyIndex -= 1;
  updateHistoryButtons();
  navigateTo(history[historyIndex], { record: false });
});

forwardButton.addEventListener("click", () => {
  if (historyIndex >= history.length - 1) return;
  historyIndex += 1;
  updateHistoryButtons();
  navigateTo(history[historyIndex], { record: false });
});

reloadButton.addEventListener("click", () => {
  navigateTo(currentUrl || address.value, { record: false });
});

window.addEventListener("resize", scheduleResize);
window.addEventListener("DOMContentLoaded", () => navigateTo(address.value));
