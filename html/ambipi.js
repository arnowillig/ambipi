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
  intervalHandle = setInterval(() => updateImage(), 1000);
}
document.addEventListener("visibilitychange", () => {
  if (!document.hidden) updateImage(true);
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

// Periodically resync display/table state in case something else changed them
setInterval(() => {
  fetchDisplayState();
  fetchTableState();
  fetchGamewallState();
}, 5000);

// Kick off auto-refresh and initial state sync
startAutoRefresh();
updateImage(true);
fetchDisplayState();
fetchTableState();
fetchGamewallState();
