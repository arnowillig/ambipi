// ambipi.js
// Vanilla JS version of the original functionality, with enhancements:
// - Visibility-aware screenshot updates
// - Theme toggle (light/dark) persisted
// - Debounced slider network calls
// - Display/Table toggles with sync
// - Mode button active-state sync

/**
 * Convert hex color string to RGB object.
 * @param {string} hex - e.g. "#ff00aa"
 * @returns {{r:number,g:number,b:number}|null}
 */
function hexToRgb(hex) {
  const m = /^#?([a-f\d]{2})([a-f\d]{2})([a-f\d]{2})$/i.exec(hex);
  return m ? { r: parseInt(m[1], 16), g: parseInt(m[2], 16), b: parseInt(m[3], 16) } : null;
}

// Element references
const imgEl = document.getElementById("img");
const brislider = document.getElementById("bri");
const gammaSlider = document.getElementById("gamma");
const gammalabel = document.getElementById("gammavalue");
const brilabel = document.getElementById("brivalue");
const lastUpdated = document.getElementById("lastUpdated");
const updateStatus = document.getElementById("updateStatus");
const themeToggle = document.getElementById("themeToggle");
const themeText = document.getElementById("themeText");
const themeIcon = document.getElementById("themeIcon");
const displayToggle = document.getElementById("displayToggle");
const tableToggle = document.getElementById("tableToggle");
const displayState = document.getElementById("displayState");
const tableState = document.getElementById("tableState");
const gamewallToggle = document.getElementById("gamewallToggle");
const gamewallState = document.getElementById("gamewallState");
const swaprbToggle = document.getElementById("swaprbToggle");
const swaprbState = document.getElementById("swaprbState");
const hdrToggle = document.getElementById("hdrToggle");
const hdrState = document.getElementById("hdrState");
const vertexInfo = document.getElementById("vertexInfo");
const vertexConn = document.getElementById("vertexConn");
const previewToggle = document.getElementById("previewToggle");
const capresSelect = document.getElementById("capresSelect");
let previewOn = localStorage.getItem("ambi_preview") !== "off";

// Apply stored or system theme preference
let dark = (() => {
  const stored = localStorage.getItem("ambi_theme");
  const prefersDark = window.matchMedia("(prefers-color-scheme: dark)").matches;
  return stored ? stored === "dark" : prefersDark;
})();

function applyTheme() {
  if (dark) {
    document.body.setAttribute("data-theme", "dark");
    themeText.textContent = "Dark mode";
    themeIcon.textContent = "🌙";
  } else {
    document.body.setAttribute("data-theme", "light");
    themeText.textContent = "Light mode";
    themeIcon.textContent = "☀️";
  }
}
applyTheme();
themeToggle.addEventListener("click", () => {
  dark = !dark;
  localStorage.setItem("ambi_theme", dark ? "dark" : "light");
  applyTheme();
});

// Screenshot updating logic with throttling and visibility awareness
let intervalHandle = null;
let lastTimestamp = 0;
function updateImage(force = false) {
  if (!force && document.hidden) return; // conserve when tab not visible
  const now = Date.now();
  if (now - lastTimestamp < 900) return; // throttle ~1s
  lastTimestamp = now;

  const newImg = new Image();
  //updateStatus.textContent = "Updating...";
  newImg.onload = () => {
    imgEl.src = newImg.src;
    const d = new Date();
    /*
    lastUpdated.textContent = d.toLocaleTimeString(undefined, {
      hour: "2-digit",
      minute: "2-digit",
      second: "2-digit"
    });
    
    updateStatus.textContent = "Live";
    setTimeout(() => {
      if (updateStatus.textContent === "Live") updateStatus.textContent = "";
    }, 3500);
    */
  };
  newImg.onerror = () => {
    updateStatus.textContent = "Error";
  };
  newImg.src = "/api/screenshot.jpg?ts=" + now;
}

// Start periodic refresh (honoring visibility)
function startAutoRefresh() {
  if (intervalHandle) clearInterval(intervalHandle);
  if (!previewOn) return;
  intervalHandle = setInterval(() => updateImage(), 1000);
}
document.addEventListener("visibilitychange", () => {
  if (!document.hidden && previewOn) updateImage(true);
});
imgEl.addEventListener("click", () => updateImage(true));

// Initialize brightness from backend
fetch("/api/bri")
  .then(r => r.text())
  .then(text => {
    const val = parseInt(text);
    if (!isNaN(val)) {
      brislider.value = val;
      brilabel.textContent = val;
    }
  })
  .catch(() => {});

// Color picker handler
document.getElementById("color").addEventListener("input", (e) => {
  const col = hexToRgb(e.target.value);
  if (col) {
    fetch(`/api/col/${col.r}/${col.g}/${col.b}`).catch(() => {});
  }
});

// Mode buttons and active-state handling
const modeMap = {
  ambilight: "ambilight",
  rainbow: "rainbow",
  vegas: "vegas",
  knightrider: "knightrider",
  testpattern: "testpattern",
  white: "white",
  off: "off"
};

function setActiveMode(modeName) {
  const norm = modeName.toLowerCase();
  Object.keys(modeMap).forEach(id => {
    const btn = document.getElementById(id);
    if (!btn) return;
    if (modeMap[id].toLowerCase() === norm) {
      btn.classList.add("active");
      btn.setAttribute("aria-pressed", "true");
    } else {
      btn.classList.remove("active");
      btn.setAttribute("aria-pressed", "false");
    }
  });
}

// Wire clicks: update backend and UI
Object.keys(modeMap).forEach(id => {
  const btn = document.getElementById(id);
  if (!btn) return;
  btn.addEventListener("click", () => {
    const modeName = modeMap[id];
    fetch(`/api/mode/${modeName}`).catch(() => {});
    setActiveMode(modeName);
  });
});

// Poll backend every 5s for current mode to keep UI in sync.
async function refreshCurrentMode() {
  try {
    const res = await fetch("/api/mode");
    if (!res.ok) return;
    const raw = await res.text();
    const mode = raw.trim().toLowerCase();
    if (mode) setActiveMode(mode);
  } catch (e) {
    // silent fail
  }
}
setInterval(refreshCurrentMode, 5000);
refreshCurrentMode(); // initial sync

// Brightness preset buttons
document.querySelectorAll("button[data-bri]").forEach(btn => {
  btn.addEventListener("click", () => {
    const v = btn.getAttribute("data-bri");
    fetch(`/api/bri/${v}`).catch(() => {});
    brislider.value = v;
    brilabel.textContent = v;
  });
});

// Debounced brightness slider
let briTimeout = null;
brislider.addEventListener("input", () => {
  const v = brislider.value;
  brilabel.textContent = v;
  if (briTimeout) clearTimeout(briTimeout);
  briTimeout = setTimeout(() => {
    fetch(`/api/bri/${v}`).catch(() => {});
  }, 150);
});

// Debounced gamma slider
let gammaTimeout = null;
gammaSlider.addEventListener("input", () => {
  const v = gammaSlider.value;
  gammalabel.textContent = v;
  if (gammaTimeout) clearTimeout(gammaTimeout);
  gammaTimeout = setTimeout(() => {
    fetch(`/api/gamma/${v}`).catch(() => {});
  }, 150);
});

// Display / Table state sync and toggles

async function fetchDisplayState() {
  try {
    const res = await fetch("/api/display");
    if (!res.ok) throw new Error("bad");
    const raw = await res.text();
    const val = raw.trim();
    const on = val === "true";
    displayToggle.checked = on;
    displayState.textContent = on ? "On" : "Off";
  } catch (e) {
    displayState.textContent = "Error";
  }
}

async function fetchTableState() {
  try {
    const res = await fetch("/api/table");
    if (!res.ok) throw new Error("bad");
    const raw = await res.text();
    const val = raw.trim();
    const on = val === "true";
    tableToggle.checked = on;
    tableState.textContent = on ? "On" : "Off";
  } catch (e) {
    tableState.textContent = "Error";
  }
}

async function fetchGamewallState() {
  try {
    const res = await fetch("/api/gamewall");
    if (!res.ok) throw new Error("bad");
    const raw = await res.text();
    const val = raw.trim();
    const on = val === "true";
    gamewallToggle.checked = on;
    gamewallState.textContent = on ? "On" : "Off";
  } catch (e) {
    gamewallState.textContent = "Error";
  }
}

async function fetchSwaprbState() {
  try {
    const res = await fetch("/api/swaprb");
    if (!res.ok) throw new Error("bad");
    const raw = await res.text();
    const on = raw.trim() === "true";
    swaprbToggle.checked = on;
    swaprbState.textContent = on ? "On" : "Off";
  } catch (e) {
    swaprbState.textContent = "Error";
  }
}

async function fetchHdrState() {
  try {
    const res = await fetch("/api/hdr");
    if (!res.ok) throw new Error("bad");
    const raw = await res.text();
    const on = raw.trim() === "true";
    hdrToggle.checked = on;
    hdrState.textContent = on ? "On" : "Off";
  } catch (e) {
    hdrState.textContent = "Error";
  }
}

// --- HDFury Vertex (serial via FTDI) ---
function escHtml(s) {
  return String(s).replace(/[&<>"]/g, (c) =>
    ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
}
async function fetchVertexInfo() {
  if (!vertexInfo) return;
  try {
    const res = await fetch("/api/vertex/info");
    if (!res.ok) throw new Error("bad");
    const j = await res.json();
    const reachable = (j.ver && j.ver.length) || (j.input && j.input.length);
    if (!reachable) throw new Error("empty");
    if (vertexConn) {
      vertexConn.textContent = "● online";
      vertexConn.style.color = "#2faa4a";
    }
    const edid = j.edidmode
      ? escHtml(j.edidmode) + (j.edidtable ? " (Tab " + escHtml(j.edidtable) + ")" : "")
      : "—";
    vertexInfo.innerHTML =
      "<strong>FW:</strong> " + escHtml(j.ver || "—") + "<br>" +
      "<strong>Input:</strong> " + escHtml(j.input || "—") + "<br>" +
      "<strong>HDCP:</strong> " + escHtml(j.hdcp || "—") + "<br>" +
      "<strong>EDID:</strong> " + edid + "<br>" +
      "<strong>Autosw:</strong> " + escHtml(j.autosw || "—");
  } catch (e) {
    if (vertexConn) {
      vertexConn.textContent = "● offline";
      vertexConn.style.color = "#c9434a";
    }
    vertexInfo.textContent = "Nicht erreichbar (FTDI/Strom prüfen).";
  }
}

// Toggle handlers
displayToggle.addEventListener("change", () => {
  const target = displayToggle.checked ? "1" : "0";
  fetch(`/api/display/${target}`)
    .then(() => fetchDisplayState())
    .catch(() => {
      displayState.textContent = "Error";
    });
});
tableToggle.addEventListener("change", () => {
  const target = tableToggle.checked ? "1" : "0";
  fetch(`/api/table/${target}`)
    .then(() => fetchTableState())
    .catch(() => {
      tableState.textContent = "Error";
    });
});
gamewallToggle.addEventListener("change", () => {
  const target = gamewallToggle.checked ? "1" : "0";
  fetch(`/api/gamewall/${target}`)
    .then(() => fetchGamewallState())
    .catch(() => {
      gamewallState.textContent = "Error";
    });
});
swaprbToggle.addEventListener("change", () => {
  const target = swaprbToggle.checked ? "1" : "0";
  fetch(`/api/swaprb/${target}`)
    .then(() => fetchSwaprbState())
    .catch(() => {
      swaprbState.textContent = "Error";
    });
});
hdrToggle.addEventListener("change", () => {
  const target = hdrToggle.checked ? "1" : "0";
  fetch(`/api/hdr/${target}`)
    .then(() => fetchHdrState())
    .catch(() => {
      hdrState.textContent = "Error";
    });
});

// Vertex input buttons + hotplug + manual refresh
document.querySelectorAll("[data-vinput]").forEach((btn) => {
  btn.addEventListener("click", () => {
    const port = btn.getAttribute("data-vinput");
    if (vertexInfo) vertexInfo.textContent = "Schalte auf " + port + " …";
    fetch(`/api/vertex/set/input/${port}`)
      .then(() => setTimeout(fetchVertexInfo, 700))
      .catch(() => {});
  });
});
const vertexHotplugBtn = document.getElementById("vertexHotplug");
if (vertexHotplugBtn)
  vertexHotplugBtn.addEventListener("click", () => {
    fetch("/api/vertex/hotplug")
      .then(() => setTimeout(fetchVertexInfo, 1200))
      .catch(() => {});
  });
const vertexRefreshBtn = document.getElementById("vertexRefresh");
if (vertexRefreshBtn) vertexRefreshBtn.addEventListener("click", fetchVertexInfo);

// Periodically resync display/table state in case something else changed them
setInterval(() => {
  fetchDisplayState();
  fetchTableState();
  fetchGamewallState();
  fetchSwaprbState();
  fetchHdrState();
}, 5000);

// Kick off auto-refresh and initial state sync
// Web preview on/off (frontend-only: hides the image + stops refreshing,
// which also stops the screenshot requests / server-side work).
function applyPreviewState() {
  if (previewToggle) previewToggle.checked = previewOn;
  if (imgEl) imgEl.style.display = previewOn ? "" : "none";
  if (previewOn) {
    startAutoRefresh();
    updateImage(true);
  } else if (intervalHandle) {
    clearInterval(intervalHandle);
    intervalHandle = null;
  }
}
if (previewToggle) {
  previewToggle.addEventListener("change", () => {
    previewOn = previewToggle.checked;
    localStorage.setItem("ambi_preview", previewOn ? "on" : "off");
    applyPreviewState();
  });
}

// Capture resolution dropdown -> /api/capres/:w/:h (reopens the V4L2 device).
async function fetchCapRes() {
  if (!capresSelect) return;
  try {
    const res = await fetch("/api/capres");
    const v = (await res.text()).trim();   // e.g. "1280x720"
    if (v) capresSelect.value = v;
  } catch (e) {}
}
if (capresSelect) {
  capresSelect.addEventListener("change", () => {
    const p = capresSelect.value.split("x");
    fetch(`/api/capres/${p[0]}/${p[1]}`).catch(() => {});
  });
}

// Beamer (JMGO) power via ADB
const beamerOnBtn = document.getElementById("beamerOn");
const beamerOffBtn = document.getElementById("beamerOff");
if (beamerOnBtn) beamerOnBtn.addEventListener("click", () => fetch("/api/beamer/on").catch(() => {}));
if (beamerOffBtn) beamerOffBtn.addEventListener("click", () => fetch("/api/beamer/off").catch(() => {}));

// Garagenrollo / Shutter (Becker Centronic): open == DOWN, close == UP, halt == stop
async function fetchShutterState() {
  const el = document.getElementById("shutterInfo");
  if (!el) return;
  try {
    const res = await fetch("/api/shutter");
    const j = await res.json();
    const u = (j.units || []).find((x) => x.index === 1) || (j.units || [])[0];
    el.textContent = u ? `Einheit ${u.code} · #${u.increment}${u.configured ? "" : " (nicht gekoppelt!)"}` : "";
  } catch {
    el.textContent = "";
  }
}
const shutterOpenBtn = document.getElementById("shutterOpen");
const shutterCloseBtn = document.getElementById("shutterClose");
const shutterHaltBtn = document.getElementById("shutterHalt");
if (shutterOpenBtn) shutterOpenBtn.addEventListener("click", () => fetch("/api/shutter/open").then(() => setTimeout(fetchShutterState, 400)).catch(() => {}));
if (shutterCloseBtn) shutterCloseBtn.addEventListener("click", () => fetch("/api/shutter/close").then(() => setTimeout(fetchShutterState, 400)).catch(() => {}));
if (shutterHaltBtn) shutterHaltBtn.addEventListener("click", () => fetch("/api/shutter/halt").then(() => setTimeout(fetchShutterState, 400)).catch(() => {}));

applyPreviewState();
fetchCapRes();
fetchDisplayState();
fetchTableState();
fetchGamewallState();
fetchSwaprbState();
fetchHdrState();
fetchVertexInfo();
fetchShutterState();

// Vertex status is read over serial (slower) — refresh on its own gentle interval.
setInterval(fetchVertexInfo, 30000);
