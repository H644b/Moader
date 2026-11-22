"use strict";
const $ = (id) => document.getElementById(id);
const states = {
  disabled: "Disabled",
  pending: "Pending",
  active: "Routing active",
  "reopen-content": "Reopen content",
  "relaunch-game": "Relaunch game",
  error: "Error",
  unavailable: "Game unavailable",
};
let token = "",
  state = { generation: 0, mods: [], runtime: { connected: false, detail: "" } },
  selectedTitle = "",
  selectedKey = "";
const preferenceDefaults = {
  gameNames: true,
  reducedMotion: false,
  webOnStart: false,
  webPort: 7712,
  followLog: true,
};
let preferences = { ...preferenceDefaults },
  settingsReady = false,
  settingsLoading = false,
  settingsSaving = false,
  settingsBaseline = null;
let settingsOpen = false,
  settingsTab = "general",
  settingsSubpage = "";
let libraryPage = "library",
  uploadReturn = "library";
let paired = false,
  connected = false,
  busy = false,
  refreshTask = null,
  events = [],
  confirmRemoval = null,
  upload = null;
try {
  token = sessionStorage.getItem("moader-pairing") || "";
} catch {}
const keyOf = (mod) => mod.titleId + "/" + mod.id;
const currentMod = () => state.mods.find((mod) => keyOf(mod) === selectedKey);
const inventoryAvailable = () => state.inventory?.ready !== false && !state.inventory?.error;
function node(tag, cls, text) {
  const el = document.createElement(tag);
  if (cls) el.className = cls;
  if (text !== undefined) el.textContent = text;
  return el;
}
function bytes(value, precision) {
  if (value < 1024) return value + " B";
  const unit = value < 1048576 ? 1024 : 1048576;
  return (value / unit).toFixed(precision ?? (value / unit < 10 ? 1 : 0)) + (unit === 1024 ? " KiB" : " MiB");
}
function kinds(mod) {
  return [mod.romfs && "RomFS", mod.exefs && "ExeFS"].filter(Boolean).join(" + ") || "No payload";
}
function counted(value, name) {
  return value + " " + name + (value === 1 ? "" : "s");
}
function remember(value) {
  token = value;
  try {
    if (value) sessionStorage.setItem("moader-pairing", value);
    else sessionStorage.removeItem("moader-pairing");
  } catch {}
}
function updateControls() {
  document
    .querySelectorAll(".mod-switch,#toggle-selected,#remove-selected,#show-upload,#empty-upload")
    .forEach((button) => {
      button.disabled = !connected || busy || !inventoryAvailable();
    });
  $("carousel-upload").disabled = !connected || busy || !inventoryAvailable();
  $("show-settings").disabled = busy || settingsSaving;
  $("game-copy").disabled = !connected || busy || settingsSaving || !inventoryAvailable();
  webStore.updateAction();
  updateSettingsControls();
}
const reduceMotion = matchMedia("(prefers-reduced-motion: reduce)");
const motionReduced = () => reduceMotion.matches || preferences.reducedMotion;
function syncMotion() {
  document.documentElement.classList.toggle("reduce-motion", preferences.reducedMotion);
  if (motionReduced()) document.getAnimations?.().forEach((animation) => animation.cancel());
}
reduceMotion.addEventListener("change", syncMotion);
function reveal(el) {
  el.getAnimations?.().forEach((animation) => animation.cancel());
  if (motionReduced() || typeof el.animate !== "function") return;
  el.animate(
    [
      { opacity: 0.35, transform: "translateY(-6px)", clipPath: "inset(0 0 12px 0)" },
      { opacity: 1, transform: "translateY(0)", clipPath: "inset(0 0 0 0)" },
    ],
    { duration: 220, easing: "cubic-bezier(.16,1,.3,1)" },
  );
}
function connection(ok, text) {
  connected = ok;
  const el = $("connection-status");
  el.className = "connection " + (ok ? "connected" : paired ? "lost" : "");
  if (el.textContent !== text)
    el.replaceChildren(node("span", "status-dot"), document.createTextNode(text));
  updateControls();
}
function message(text, error = false) {
  $("action-message").textContent = text;
  $("action-message").classList.toggle("error", error);
}
function showPair(error = "") {
  if (settingsOpen) showSettings(false, false);
  libraryPage = "library";
  syncPage();
  clearCovers();
  paired = false;
  $("store-button").hidden = true;
  webStore.disconnect();
  $("show-settings").hidden = true;
  $("pair-screen").hidden = false;
  $("workbench").hidden = true;
  $("disconnect").hidden = true;
  $("pair-error").textContent = error;
  connection(false, "Not paired");
}
async function api(path, options = {}) {
  const controller = new AbortController(),
    timer = setTimeout(() => controller.abort(), 45000);
  const abort = () => controller.abort();
  options.signal?.addEventListener("abort", abort, { once: true });
  if (options.signal?.aborted) abort();
  try {
    const response = await fetch("/api" + path, {
      method: options.method || "GET",
      body: options.body,
      headers: { Authorization: "Bearer " + token, ...options.headers },
      signal: controller.signal,
      cache: "no-store",
    });
    let data;
    try {
      data = await response.json();
    } catch {
      throw new Error("Moader returned an unreadable response. Refresh the connection.");
    }
    if (!response.ok) {
      const error = new Error(data.error || "The operation failed (HTTP " + response.status + ").");
      error.status = response.status;
      if (response.status === 401) {
        remember("");
        showPair(error.message);
      }
      throw error;
    }
    return data;
  } catch (error) {
    if (error.name === "AbortError")
      throw new Error("The request was interrupted. Check its state before trying again.");
    throw error;
  } finally {
    clearTimeout(timer);
    options.signal?.removeEventListener("abort", abort);
  }
}
async function sendUploadChunks(path, file, offset, signal, onProgress) {
  onProgress(offset);
  while (offset < file.size) {
    if (signal.aborted) throw new Error("Upload cancelled.");
    const chunk = file.slice(offset, offset + 1048576);
    const result = await api(path + "?offset=" + offset, { method: "PUT", body: chunk, signal });
    if (result.received !== offset + chunk.size) throw new Error("The Switch could not confirm the upload.");
    offset = result.received;
    onProgress(offset);
  }
}
function gameName(title) {
  const value = state.gameNames?.[title] || state.gameNames?.[title.toUpperCase()];
  return preferences.gameNames && typeof value === "string" ? value.trim() : "";
}
function normalizeSettings(value) {
  if (
    !value ||
    Object.keys(preferenceDefaults).some(
      (key) => typeof value[key] !== typeof preferenceDefaults[key],
    ) ||
    !Number.isInteger(value.webPort) ||
    value.webPort < 1024 ||
    value.webPort > 65535 ||
    value.webPort === 7713
  )
    throw new Error("This service returned incompatible settings. Update Moader and reconnect.");
  return Object.fromEntries(Object.keys(preferenceDefaults).map((key) => [key, value[key]]));
}
function draftSettings() {
  return Object.fromEntries(
    Object.keys(preferenceDefaults).map((key) => {
      const input = $("settings-form").elements.namedItem(key);
      return [key, key === "webPort" ? Number(input.value) : input.checked];
    }),
  );
}
function settingsChanges() {
  if (!settingsBaseline) return {};
  return Object.fromEntries(
    Object.entries(draftSettings()).filter(([key, value]) => value !== settingsBaseline[key]),
  );
}
function updateSettingsControls() {
  const blocked = !settingsReady || !connected || settingsLoading || settingsSaving;
  $("settings-form")
    .querySelectorAll("input")
    .forEach((input) => {
      input.disabled = blocked;
      if (input.type === "checkbox") {
        const stateLabel = input.closest("label").querySelector(".switch-state");
        stateLabel.textContent = input.checked ? "On" : "Off";
      }
    });
  const dirty = Object.keys(settingsChanges()).length > 0;
  $("settings-save").disabled = blocked || !dirty;
  $("settings-reset").disabled = !dirty || settingsLoading || settingsSaving;
  $("settings-save").textContent = settingsSaving ? "Saving…" : "Save settings";
}
function syncSettingsForm(force = false) {
  const dirty = Object.keys(settingsChanges()).length > 0;
  if (settingsReady && (force || !dirty)) {
    for (const [key, value] of Object.entries(preferences)) {
      const input = $("settings-form").elements.namedItem(key);
      if (key === "webPort") {
        input.value = value;
        input.setCustomValidity("");
      } else input.checked = value;
    }
    settingsBaseline = { ...preferences };
  }
  const changedElsewhere =
    settingsBaseline && JSON.stringify(settingsBaseline) !== JSON.stringify(preferences);
  $("settings-status").textContent = settingsLoading
    ? "Loading settings…"
    : settingsSaving
      ? "Saving on your Switch…"
      : !settingsReady
        ? "Settings are unavailable."
        : Object.keys(settingsChanges()).length
          ? changedElsewhere
            ? "Settings changed elsewhere. Your edits are kept; only your changes will be saved."
            : "You have unsaved changes."
          : "Saved on your Switch.";
  updateSettingsControls();
}
function applySettings(value) {
  preferences = normalizeSettings(value);
  settingsReady = true;
  syncMotion();
  syncSettingsForm();
}
function renderDiagnostics() {
  const runtime = state.runtime || {};
  $("settings-listener").textContent = location.host;
  $("settings-backend").textContent = runtime.backend || "Not reported";
  $("settings-runtime-connection").textContent = runtime.connected ? "Connected" : "Not connected";
  $("settings-result").textContent =
    runtime.result === undefined ? "Not reported" : String(runtime.result);
  $("settings-generation").textContent = String(state.generation);
  $("settings-applied-generation").textContent =
    runtime.appliedGeneration === undefined ? "Not reported" : String(runtime.appliedGeneration);
}
function renderGameSource() {
  const mod = state.mods.find((item) => item.titleId === selectedTitle);
  $("game-copy-setting").hidden = !mod || settingsSubpage !== "copy";
  $("open-game-copy").disabled = !mod;
  $("copy-row-title").textContent = mod
    ? gameName(selectedTitle) || selectedTitle.toUpperCase()
    : "Select a game in the library.";
  if (!mod) return;
  $("game-copy-title").textContent = gameName(selectedTitle) || selectedTitle.toUpperCase();
  $("game-copy").value = mod.gameSource || "unspecified";
  $("game-copy").disabled = !connected || busy || settingsSaving || !inventoryAvailable();
}
function settingsSubpageView(page) {
  settingsSubpage = page;
  $("settings-pane").dataset.subpage = page;
  $("session-evidence").hidden = page !== "session";
  $("settings-save-bar").hidden = !!page || settingsTab === "credits";
  renderGameSource();
}
function selectSettingsTab(tab, focus = false) {
  settingsTab = tab;
  settingsSubpageView("");
  for (const name of ["general", "advanced", "credits"]) {
    const active = name === tab,
      button = $("tab-" + name),
      panel = $("settings-" + name);
    button.setAttribute("aria-selected", String(active));
    button.tabIndex = active ? 0 : -1;
    panel.hidden = !active;
    if (active && focus) button.focus();
  }
  $("settings-save-bar").hidden = tab === "credits";
}
async function showSettings(show, restoreFocus = true) {
  settingsOpen = show;
  if (!show) settingsSubpageView("");
  syncPage();
  $("settings-pane").hidden = !show;
  $("library-content").hidden = show;
  $("show-settings").setAttribute("aria-expanded", String(show));
  if (!show) {
    if (restoreFocus) $("show-settings").focus({ preventScroll: true });
    return;
  }
  renderDiagnostics();
  syncSettingsForm();
  reveal($("settings-pane"));
  $("settings-title").focus({ preventScroll: true });
  $("settings-error").textContent = "";
  settingsLoading = true;
  syncSettingsForm();
  try {
    const loaded = await api("/settings");
    applySettings(loaded);
    state.settings = { ...preferences };
    renderGames();
  } catch (error) {
    $("settings-error").textContent = error.message;
  } finally {
    settingsLoading = false;
    syncSettingsForm();
  }
}
async function saveSettings(event) {
  event.preventDefault();
  if (!connected || settingsSaving || !settingsReady) return;
  const port = $("pref-web-port"),
    invalidPort = !port.validity.valid || Number(port.value) === 7713;
  port.setCustomValidity(
    Number(port.value) === 7713 ? "Port 7713 is reserved. Choose another port." : "",
  );
  if (invalidPort || !port.checkValidity()) {
    selectSettingsTab("advanced");
    port.focus();
    port.reportValidity();
    return;
  }
  const changes = settingsChanges();
  if (!Object.keys(changes).length) return;
  settingsSaving = true;
  $("settings-error").textContent = "";
  syncSettingsForm();
  try {
    if (refreshTask) await refreshTask;
    const query = new URLSearchParams(
      Object.entries(changes).map(([key, value]) => [
        key,
        typeof value === "boolean" ? (value ? "1" : "0") : String(value),
      ]),
    );
    const saved = await api("/settings?" + query, { method: "POST", body: "" });
    applySettings(saved);
    state.settings = { ...preferences };
    syncSettingsForm(true);
    renderGames();
    await refresh();
    $("settings-status").textContent = Object.hasOwn(changes, "webPort")
      ? "Saved. The port changes when you next restart the web GUI on the console."
      : "Settings saved on your Switch.";
  } catch (error) {
    $("settings-error").textContent = error.message;
    $("settings-status").textContent = "Couldn’t save settings. Your edits are kept.";
  } finally {
    settingsSaving = false;
    updateControls();
  }
}
function downloadLog() {
  const runtime = state.runtime || {};
  const content = [
    "Moader 4.0.0 debug log",
    new Date().toISOString(),
    "Listener: " + location.host,
    "Library generation: " + state.generation,
    "Runtime: " + JSON.stringify(runtime),
    "",
    ...events,
  ].join("\n");
  const url = URL.createObjectURL(new Blob([content], { type: "text/plain;charset=utf-8" }));
  const link = node("a", "");
  link.href = url;
  link.download = "moader-debug-" + new Date().toISOString().replace(/[:.]/g, "-") + ".txt";
  document.body.append(link);
  link.click();
  link.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
function updateGameNameDisclosure() {
  const heading = $("library-title");
  $("game-name-details").hidden =
    !gameName(selectedTitle) || heading.scrollHeight <= heading.clientHeight + 1;
}
new ResizeObserver(updateGameNameDisclosure).observe($("library-title"));
$("game-name-details").addEventListener("toggle", () => {
  if ($("game-name-details").open) reveal($("game-full-name"));
});
const coverCache = new Map();
let artSession = 0,
  artRunning = false;
function clearCovers() {
  ++artSession;
  for (const cached of coverCache.values()) if (cached.url) URL.revokeObjectURL(cached.url);
  coverCache.clear();
}
async function loadCovers() {
  if (artRunning || !token || !connected || settingsOpen) return;
  artRunning = true;
  const session = artSession;
  try {
    const deadline = Date.now();
    const visible = [...$("games").children]
      .filter((b) => Math.abs(Number(b.dataset.offset)) <= 2)
      .sort((a, b) => Math.abs(Number(a.dataset.offset)) - Math.abs(Number(b.dataset.offset)));
    for (const tile of visible) {
      if (session !== artSession) break;
      const id = tile.dataset.gameTitle,
        cached = coverCache.get(id);
      if (cached && (cached.url || cached.retry > deadline)) continue;
      try {
        const response = await fetch("/api/game-art?titleId=" + encodeURIComponent(id), {
          headers: { Authorization: "Bearer " + token },
          cache: "no-store",
          signal: AbortSignal.timeout(8000),
        });
        const imageType = response.headers.get("content-type")?.split(";")[0].trim().toLowerCase();
        if (!response.ok || !["image/jpeg", "image/png"].includes(imageType))
          throw Error();
        const blob = await response.blob();
        if (session !== artSession) break;
        const url = URL.createObjectURL(blob);
        const image = tile.querySelector(".cover-image");
        image.src = url;
        try {
          await image.decode();
        } catch (error) {
          URL.revokeObjectURL(url);
          throw error;
        }
        if (session !== artSession || !tile.isConnected) {
          URL.revokeObjectURL(url);
          continue;
        }
        coverCache.set(id, { url });
        tile.classList.add("has-art");
        if (id === selectedTitle) syncCoverContext();
      } catch {
        if (session === artSession) coverCache.set(id, { retry: Date.now() + 60000 });
      }
    }
  } finally {
    artRunning = false;
  }
}
function syncCoverContext() {
  const card = $("game-context"),
    art = coverCache.get(selectedTitle)?.url;
  const img = card.querySelector("img");
  if (art && img.getAttribute("src") !== art) img.src = art;
  img.hidden = !art;
  card.querySelector(".context-fallback").hidden = !!art;
  card.querySelector("h2").textContent = gameName(selectedTitle) || selectedTitle.toUpperCase();
  card.querySelector("p").textContent = $("cover-count").textContent;
}
function syncPage() {
  const page = settingsOpen ? "settings" : libraryPage;
  $("workbench").dataset.page = page;
  $("companion-label").hidden = !settingsOpen;
  $("page-crumb").textContent = {
    library: "",
    mods: "›   Mods",
    detail: "›   Mods   ›   Mod details",
    settings: "›   Settings",
    upload: "›   Upload mod",
    store: "›   Store",
  }[page];
  $("store-pane").hidden = page !== "store";
  $("store-button").setAttribute("aria-expanded", String(page === "store"));
  $("game-context").hidden = page !== "mods" && page !== "detail";
  $("runtime-notice").hidden = page === "library" || page === "settings" || page === "store";
  if (!settingsOpen) $("library-content").hidden = false;
}
function showPage(page) {
  if (settingsOpen) showSettings(false, false);
  if (busy && !$("upload-pane").hidden) return;
  if (page !== libraryPage) message("");
  libraryPage = page;
  confirmRemoval = null;
  syncPage();
  if (page !== "upload") $("upload-pane").hidden = true;
}
function stepGame(delta) {
  const titles = [...$("games").children].map((b) => b.dataset.gameTitle);
  if (!titles.length) return;
  const moveFocus = Boolean(document.activeElement?.closest(".game-button"));
  selectedTitle = titles[(titles.indexOf(selectedTitle) + delta + titles.length) % titles.length];
  selectedKey = "";
  confirmRemoval = null;
  render();
  if (moveFocus) $("games").querySelector(".game-button.active")?.focus({ preventScroll: true });
}
function renderGames() {
  const titles = [...new Set(state.mods.map((mod) => mod.titleId))].sort();
  if (!titles.includes(selectedTitle)) {
    if (libraryPage === "mods" || libraryPage === "detail") libraryPage = "library";
    selectedTitle = titles[0] || "";
  }
  for (const [id, cached] of coverCache)
    if (!titles.includes(id)) {
      if (cached.url) URL.revokeObjectURL(cached.url);
      coverCache.delete(id);
    }
  $("game-count").textContent = counted(titles.length, "game");
  $("games-empty").hidden = titles.length > 0;
  const existing = new Map(
    [...$("games").children].map((button) => [button.dataset.gameTitle, button]),
  );
  for (const [index, title] of titles.entries()) {
    const mods = state.mods.filter((mod) => mod.titleId === title);
    let button = existing.get(title);
    if (!button) {
      button = node("button", "game-button");
      button.type = "button";
      button.dataset.gameTitle = title;
      const text = node("span", "game-text");
      text.append(node("span", "game-name"), node("code", "", title.toUpperCase()), node("small"));
      const art = node("span", "cover-art");
      const fallback = node("span", "cover-fallback");
      const mark = node("img", "fallback-mark");
      mark.src = "/icon.svg";
      mark.alt = "";
      fallback.append(mark, node("span", "fallback-title"));
      const img = node("img", "cover-image");
      img.alt = "";
      art.append(fallback, img);
      button.append(art, text);
      button.addEventListener("click", () => chooseGame(title));
      $("games").append(button);
    }
    const name = gameName(title);
    button.querySelector(".game-name").textContent = name;
    button.querySelector(".game-name").hidden = !name;
    const label = name || title.toUpperCase();
    button.setAttribute("aria-label", "Open " + label);
    button.querySelector(".fallback-title").textContent = label;
    button.querySelector("code").hidden = !!name;
    const cached = coverCache.get(title)?.url;
    const img = button.querySelector(".cover-image");
    if (cached && img.getAttribute("src") !== cached) img.src = cached;
    button.classList.toggle("has-art", !!cached);
    let offset = index - titles.indexOf(selectedTitle);
    if (titles.length > 2) offset -= Math.round(offset / titles.length) * titles.length;
    button.dataset.offset = String(offset);
    button.dataset.distance = String(Math.abs(offset));
    button.hidden = Math.abs(offset) > 2;
    button.style.setProperty("--side", Math.sign(offset));
    button.tabIndex = title === selectedTitle ? 0 : -1;
    button.classList.toggle("active", title === selectedTitle);
    button.setAttribute("aria-pressed", String(title === selectedTitle));
    button.querySelector("small").textContent =
      counted(mods.length, "mod") +
      " · " +
      mods.filter((mod) => mod.requested).length +
      " requested";
    if ($("games").children[index] !== button)
      $("games").insertBefore(button, $("games").children[index] || null);
    existing.delete(title);
  }
  existing.forEach((button) => button.remove());
  // Keep the native select intact while it is open; only replace options when titles change.
  if ($("game-select").dataset.titles !== titles.join(",")) {
    $("game-select").replaceChildren();
    if (!titles.length) {
      const option = node("option", "", "No games yet");
      option.value = "";
      $("game-select").append(option);
    }
    for (const title of titles) {
      const option = node("option", "", title.toUpperCase());
      option.value = title;
      $("game-select").append(option);
    }
    $("game-select").dataset.titles = titles.join(",");
  }
  for (const option of $("game-select").options)
    if (option.value)
      option.textContent = gameName(option.value)
        ? gameName(option.value) + " · " + option.value.toUpperCase()
        : option.value.toUpperCase();
  $("game-select").value = selectedTitle;
  $("game-select").disabled = !titles.length;
  $("library-title").textContent = selectedTitle
    ? gameName(selectedTitle) || "Game mods"
    : "Your mod library";
  const name = gameName(selectedTitle);
  $("library-title").title = name || "";
  $("game-full-name").textContent = name;
  if ($("game-name-details").dataset.title !== selectedTitle) $("game-name-details").open = false;
  $("game-name-details").dataset.title = selectedTitle;
  updateGameNameDisclosure();
  $("title-id").textContent = selectedTitle
    ? selectedTitle.toUpperCase()
    : "Choose a game or upload a mod";
  $("copy-title").hidden = !selectedTitle;
  const index = titles.indexOf(selectedTitle),
    selected = state.mods.filter((m) => m.titleId === selectedTitle);
  $("open-game").textContent = selectedTitle
    ? name || selectedTitle.toUpperCase()
    : "Your library is empty";
  $("open-game").disabled = !selectedTitle;
  $("cover-count").textContent = selectedTitle
    ? counted(selected.length, "mod") +
      "   ·   " +
      selected.filter((m) => m.requested).length +
      " requested"
    : "";
  $("previous-game").hidden = $("next-game").hidden = titles.length < 2;
  $("carousel-stage").hidden = !titles.length;
  const dots = $("game-dots"),
    start = Math.max(0, Math.min(index - 4, titles.length - 9));
  const dotKey = titles.join(",") + ":" + index;
  if (dots.dataset.list !== dotKey) {
    dots.replaceChildren();
    dots.dataset.list = dotKey;
    if (titles.length > 1)
      for (const id of titles.slice(start, start + 9)) {
        const b = node("button", id === selectedTitle ? "current" : "");
        b.type = "button";
        b.setAttribute("aria-label", "Select " + (gameName(id) || id));
        b.setAttribute("aria-pressed", String(id === selectedTitle));
        b.onclick = () => {
          selectedTitle = id;
          selectedKey = "";
          render();
        };
        dots.append(b);
      }
  }
  syncCoverContext();
  syncPage();
  loadCovers();
}
function chooseGame(title) {
  if (settingsOpen) showSettings(false, false);
  showPage("mods");
  selectedTitle = title;
  selectedKey = "";
  confirmRemoval = null;
  $("search").value = "";
  render();
}
function renderMods() {
  const query = $("search").value.trim().toLocaleLowerCase();
  const all = state.mods.filter((mod) => mod.titleId === selectedTitle);
  if (libraryPage === "detail" && !all.some((mod) => keyOf(mod) === selectedKey)) {
    libraryPage = "mods";
    confirmRemoval = null;
    syncPage();
  }
  const mods = all.filter((mod) => (mod.name + " " + mod.id).toLocaleLowerCase().includes(query));
  if (!mods.some((mod) => keyOf(mod) === selectedKey)) selectedKey = mods[0] ? keyOf(mods[0]) : "";
  const existing = new Map([...$("mods").children].map((row) => [row.dataset.key, row]));
  $("empty-state").hidden = state.mods.length > 0 || !inventoryAvailable();
  $("no-results").hidden = !all.length || mods.length > 0;
  for (const [index, mod] of mods.entries()) {
    const key = keyOf(mod);
    let row = existing.get(key);
    if (!row) {
      row = node("li", "mod-row");
      row.dataset.key = key;
      const title = node("button", "mod-name");
      title.type = "button";
      title.dataset.modKey = key;
      title.dataset.modAction = "select";
      title.addEventListener("click", () => {
        selectedKey = key;
        showPage("detail");
        renderMods();
        renderInspector();
        $("inspector-title").focus({ preventScroll: true });
      });
      const info = node("div", "");
      info.append(title, node("div", "mod-meta"));
      const apply = node("div", "apply-control");
      apply.append(node("span", "mobile-row-label", "Apply state"), node("span", "state-label"));
      const toggle = node("button", "mod-switch");
      toggle.type = "button";
      toggle.setAttribute("role", "switch");
      toggle.dataset.modKey = key;
      toggle.dataset.modAction = "toggle";
      toggle.append(node("span", "switch-knob"));
      toggle.addEventListener("click", () => {
        const current = state.mods.find((item) => keyOf(item) === key);
        if (current) toggleMod(current);
      });
      const requested = node("div", "request-control");
      requested.append(node("span", "mobile-row-label", "Requested"), toggle);
      row.append(info, requested, apply);
      $("mods").append(row);
    }
    row.classList.toggle("selected", key === selectedKey);
    const title = row.querySelector(".mod-name");
    title.textContent = mod.name;
    title.setAttribute("aria-pressed", String(key === selectedKey));
    row.querySelector(".mod-meta").textContent =
      kinds(mod) + " · " + counted(mod.fileCount, "file");
    const status = row.querySelector(".state-label");
    status.className = "state-label " + (states[mod.applied] ? mod.applied : "error");
    status.textContent = states[mod.applied] || "Unknown";
    const toggle = row.querySelector(".mod-switch");
    toggle.setAttribute("aria-checked", String(mod.requested));
    toggle.setAttribute("aria-label", (mod.requested ? "Disable " : "Enable ") + mod.name);
    toggle.disabled = busy || !connected || !inventoryAvailable();
    if ($("mods").children[index] !== row)
      $("mods").insertBefore(row, $("mods").children[index] || null);
    existing.delete(key);
  }
  existing.forEach((row) => row.remove());
  $("library-summary").textContent = all.length
    ? counted(all.length, "mod") + " · " + all.filter((mod) => mod.requested).length + " requested"
    : "Library ready";
}
function renderInspector() {
  const mod = currentMod(),
    inspector = $("inspector");
  const changedSelection = inspector.dataset.key !== selectedKey;
  inspector.hidden = !mod;
  inspector.dataset.key = selectedKey;
  if (!mod) return;
  if (changedSelection) reveal(inspector);
  $("inspector-title").textContent = mod.name;
  $("inspector-state").className = "state-label " + (states[mod.applied] ? mod.applied : "error");
  $("inspector-state").textContent = states[mod.applied] || "Unknown";
  $("inspector-detail").textContent =
    mod.detail ||
    (mod.requested
      ? "The requested setting is saved."
      : "This mod is disabled. Its files stay in your library.");
  $("inspector-detail").hidden = mod.detail === state.runtime?.detail;
  $("mod-requested").textContent = mod.requested ? "On" : "Off";
  $("mod-kind").textContent = kinds(mod);
  $("mod-files").textContent = mod.fileCount;
  $("mod-size").textContent = bytes(mod.bytes);
  $("mod-id").textContent = mod.id;
  $("toggle-selected").textContent = mod.requested ? "Disable mod" : "Enable mod";
  $("toggle-selected").disabled = busy || !connected || !inventoryAvailable();
  $("remove-selected").disabled = busy || !connected || !inventoryAvailable();
  const showConfirm =
    confirmRemoval &&
    confirmRemoval.key === selectedKey &&
    confirmRemoval.generation === state.generation;
  const wasHidden = $("remove-confirm").hidden;
  $("remove-confirm").hidden = !showConfirm;
  if (showConfirm && wasHidden) reveal($("remove-confirm"));
  if (!showConfirm && !wasHidden && $("remove-confirm").contains(document.activeElement))
    $("remove-selected").focus({ preventScroll: true });
  $("remove-description").textContent =
    "Remove “" + mod.name + "” from this library? Its files stay on the SD card.";
}
function renderEvents() {
  const list = $("events"),
    atBottom = list.scrollHeight - list.scrollTop - list.clientHeight < 30;
  // Keep matching log entries and text selections stable during polling.
  const previous = [...list.children];
  for (const [index, text] of events.entries()) {
    if (previous[index]?.querySelector("p")?.textContent === text) continue;
    const item = node("li", "event" + (text.startsWith("Apply failed:") ? " error" : ""));
    item.append(node("p", "", text));
    if (previous[index]) previous[index].replaceWith(item);
    else list.append(item);
  }
  previous.slice(events.length).forEach((item) => item.remove());
  $("events-empty").hidden = events.length > 0;
  $("log-status").textContent = events.length ? "Recent service events" : "Waiting for events";
  if (preferences.followLog && atBottom) list.scrollTop = list.scrollHeight;
}
function render() {
  renderGames();
  renderMods();
  renderInspector();
  renderDiagnostics();
  renderGameSource();
  $("inventory-notice").hidden = inventoryAvailable();
  $("inventory-detail").textContent = inventoryAvailable()
    ? ""
    : (state.inventory?.ready
        ? "The SD scan failed. Your last complete library is shown. "
        : "The library could not be read from SD. ") +
      (state.inventory?.error || "") +
      " Check the SD card, then choose Refresh to retry. Mod changes are paused.";
  const runtime = state.runtime || {};
  const rejected = Number(runtime.result || 0) !== 0;
  $("runtime-notice").classList.toggle("ready", !!runtime.connected && !rejected);
  $("runtime-notice").classList.toggle("error", rejected);
  $("runtime-detail").textContent = rejected
    ? runtime.detail ||
      "The runtime rejected this catalogue (result " +
        runtime.result +
        "). Check the debug log before trying again."
    : runtime.connected
      ? runtime.detail || "Filesystem runtime connected. Check each mod’s apply state."
      : runtime.detail || "Filesystem runtime disconnected. You can still manage the library.";
  $("generation").textContent = "Generation " + state.generation;
}
async function refresh() {
  if (refreshTask) return refreshTask;
  refreshTask = (async () => {
    const [next, log] = await Promise.all([api("/state"), api("/events")]);
    if (!Array.isArray(next.mods) || !Array.isArray(log))
      throw new Error("This version of the Moader service is incompatible.");
    if (confirmRemoval && next.generation !== confirmRemoval.generation) confirmRemoval = null;
    const changed = JSON.stringify(state) !== JSON.stringify(next);
    state = next;
    if (next.settings) applySettings(next.settings);
    if (changed || !paired) render();
    if (JSON.stringify(events) !== JSON.stringify(log)) {
      events = log;
      renderEvents();
    }
    $("last-sync").textContent = new Date().toLocaleTimeString([], {
      hour: "2-digit",
      minute: "2-digit",
      second: "2-digit",
    });
    connection(true, "Connected · " + location.host);
    loadCovers();
    return state;
  })();
  try {
    return await refreshTask;
  } finally {
    refreshTask = null;
  }
}
async function pair() {
  const button = $("pair-form").querySelector("button");
  button.disabled = true;
  button.textContent = "Connecting…";
  $("pair-error").textContent = "";
  try {
    await refresh();
    paired = true;
    $("store-button").hidden = false;
    remember(token);
    $("pair-screen").hidden = true;
    $("workbench").hidden = false;
    $("disconnect").hidden = false;
    $("show-settings").hidden = false;
    connection(true, "Connected · " + location.host);
  } catch (error) {
    showPair(error.message);
  } finally {
    button.disabled = false;
    button.textContent = "Connect";
  }
}
async function mutate(action) {
  if (busy || !connected) return;
  busy = true;
  updateControls();
  renderMods();
  renderInspector();
  message("");
  try {
    await action();
    await refresh();
  } catch (error) {
    message(error.message, true);
    try {
      await refresh();
    } catch {}
  } finally {
    busy = false;
    updateControls();
    renderMods();
    renderInspector();
  }
}
async function toggleMod(mod) {
  const generation = state.generation;
  await mutate(async () => {
    await api(
      "/mod/enable?" +
        new URLSearchParams({
          titleId: mod.titleId,
          id: mod.id,
          enabled: mod.requested ? "0" : "1",
          generation,
        }),
      { method: "POST", body: "" },
    );
    message(
      mod.requested
        ? "Disable request saved. Check the mod’s apply state."
        : "Enable request saved. Check the mod’s apply state.",
    );
  });
}
async function copyText(text) {
  try {
    if (navigator.clipboard) {
      await navigator.clipboard.writeText(text);
      return true;
    }
  } catch {}
  const area = node("textarea", "clipboard-helper");
  area.value = text;
  document.body.append(area);
  area.select();
  let ok = false;
  try {
    ok = document.execCommand("copy");
  } catch {}
  area.remove();
  return ok;
}
function uploadPane(show) {
  if (!show && busy) return;
  if (show && libraryPage !== "upload") {
    uploadReturn = libraryPage;
    showPage("upload");
  }
  if (!show && libraryPage === "upload") {
    showPage(uploadReturn);
    render();
  }
  const wasHidden = $("upload-pane").hidden;
  $("upload-pane").hidden = !show;
  $("show-upload").setAttribute("aria-expanded", String(show));
  if (show) {
    if (!upload && selectedTitle) $("upload-title-id").value = selectedTitle;
    if (wasHidden) reveal($("upload-pane"));
    $("upload-pane").scrollIntoView({
      block: "nearest",
      behavior: motionReduced() ? "auto" : "smooth",
    });
    $("mod-file").focus({ preventScroll: true });
  } else if (!wasHidden) $("show-upload").focus({ preventScroll: true });
}
function setFile(file) {
  if (!file || busy || upload) return;
  $("file-label").textContent = file.name + " · " + bytes(file.size);
  if (!$("mod-name").value) $("mod-name").value = file.name.replace(/\.zip$/i, "").slice(0, 100);
}
function uploadInputs(disabled) {
  for (const id of ["mod-file", "mod-name", "upload-title-id"]) $(id).disabled = disabled;
  $("hide-upload").disabled = busy;
}
function newModId(name) {
  const random = new Uint32Array(2);
  crypto.getRandomValues(random);
  const suffix = [...random].map((value) => value.toString(16).padStart(8, "0")).join("");
  return (
    (name
      .toLowerCase()
      .replace(/[^a-z0-9_-]+/g, "-")
      .replace(/^-+|-+$/g, "")
      .slice(0, 29) || "mod") +
    "-" +
    suffix
  );
}
async function finishUpload() {
  selectedTitle = upload.title;
  selectedKey = upload.title + "/" + upload.id;
  $("search").value = "";
  libraryPage = "mods";
  syncPage();
  upload = null;
  $("upload-pane").hidden = true;
  $("show-upload").setAttribute("aria-expanded", "false");
  $("show-upload").focus({ preventScroll: true });
  $("upload-form").reset();
  $("file-label").textContent = "Choose a ZIP or drop it here";
  $("upload-progress").hidden = true;
  $("upload-cancel").hidden = true;
  $("upload-submit").textContent = "Upload to Switch";
  await refresh();
  message("Mod imported disabled. Review it before enabling.");
}
async function sendUpload(event) {
  event.preventDefault();
  if (busy || !connected) return;
  $("upload-error").textContent = "";
  const file = upload?.file || $("mod-file").files[0];
  if (!file || !/\.zip$/i.test(file.name) || file.size > 128 * 1048576) {
    $("upload-error").textContent = "Choose a ZIP file of at most 128 MiB.";
    return;
  }
  const title = $("upload-title-id").value.trim().toLowerCase(),
    name = $("mod-name").value.trim();
  if (!/^0[14][a-f0-9]{14}$/.test(title) || !name) {
    $("upload-error").textContent = "Enter a 16-character game Title ID and a mod name.";
    return;
  }
  busy = true;
  updateControls();
  uploadInputs(true);
  $("upload-submit").disabled = true;
  $("upload-progress").hidden = false;
  $("upload-cancel").hidden = false;
  renderMods();
  renderInspector();
  let committing = false;
  try {
    if (!upload) {
      const id = newModId(name);
      const result = await api(
        "/upload/begin?" + new URLSearchParams({ titleId: title, id, name, size: file.size }),
        { method: "POST", body: "" },
      );
      upload = { file, title, id, name, key: result.uploadId, offset: 0, cancelled: false };
    } else {
      const status = await api("/upload/" + upload.key);
      upload.offset = status.received;
      if (status.size !== upload.file.size || upload.offset > status.size)
        throw new Error("The upload session changed. Cancel it and start again.");
    }
    upload.controller = new AbortController();
    if (upload.cancelled) upload.controller.abort();
    await sendUploadChunks("/upload/" + upload.key, upload.file, upload.offset, upload.controller.signal, (offset) => {
      upload.offset = offset;
      const progress = Math.round((upload.offset / upload.file.size) * 100);
      $("progress-bar").value = progress;
      $("progress-text").textContent =
        bytes(upload.offset) + " of " + bytes(upload.file.size) + " · " + progress + "%";
    });
    committing = true;
    $("upload-cancel").disabled = true;
    $("progress-text").textContent = "Verifying and importing files on your Switch…";
    await api("/upload/" + upload.key, { method: "POST", body: "" });
    await finishUpload();
  } catch (error) {
    if (upload?.cancelled) {
      try {
        await api("/upload/" + upload.key, { method: "DELETE" });
      } catch {}
      upload = null;
      $("upload-error").textContent = "Upload cancelled.";
    } else if (committing && upload) {
      try {
        await refresh();
        if (state.mods.some((mod) => mod.titleId === upload.title && mod.id === upload.id)) {
          await finishUpload();
          return;
        }
      } catch {}
      $("upload-error").textContent =
        error.message + " Refresh the library before beginning another import.";
      try {
        await api("/upload/" + upload.key, { method: "DELETE" });
      } catch {}
      upload = null;
    } else {
      $("upload-error").textContent = error.message + " You can resume after reconnecting.";
    }
  } finally {
    busy = false;
    updateControls();
    uploadInputs(!!upload);
    $("hide-upload").disabled = false;
    $("upload-submit").disabled = false;
    $("upload-cancel").disabled = false;
    $("upload-submit").textContent = upload ? "Resume upload" : "Upload to Switch";
    $("upload-cancel").hidden = !upload;
    renderMods();
    renderInspector();
  }
}

$("home-crumb").addEventListener("click", () => {
  showPage("library");
  render();
});
document.querySelector(".brand").addEventListener("click", (event) => {
  if (paired) {
    event.preventDefault();
    showPage("library");
    render();
  }
});
$("back-library").addEventListener("click", () => {
  showPage("library");
  render();
});
$("back-mods").addEventListener("click", () => {
  showPage("mods");
  render();
});
$("open-game").addEventListener("click", () => {
  if (selectedTitle) chooseGame(selectedTitle);
});
$("previous-game").addEventListener("click", () => stepGame(-1));
$("next-game").addEventListener("click", () => stepGame(1));
$("carousel-upload").addEventListener("click", () => uploadPane(true));
let swipe = null;
$("carousel-stage").addEventListener("pointerdown", (e) => {
  swipe = { x: e.clientX, y: e.clientY, id: e.pointerId };
});
$("carousel-stage").addEventListener("pointercancel", () => {
  swipe = null;
});
$("carousel-stage").addEventListener("pointerup", (e) => {
  if (!swipe || swipe.id !== e.pointerId) return;
  const dx = e.clientX - swipe.x,
    dy = e.clientY - swipe.y;
  swipe = null;
  if (Math.abs(dx) > 52 && Math.abs(dx) > Math.abs(dy) * 1.5) {
    stepGame(dx < 0 ? 1 : -1);
    const block = (e) => {
      e.preventDefault();
      e.stopImmediatePropagation();
    };
    const stage = $("carousel-stage");
    stage.addEventListener("click", block, { once: true, capture: true });
    setTimeout(() => stage.removeEventListener("click", block, true), 150);
  }
});
document.addEventListener("keydown", (e) => {
  if (
    !paired ||
    settingsOpen ||
    busy ||
    e.altKey ||
    e.ctrlKey ||
    e.metaKey ||
    e.target.closest("input,select,textarea")
  )
    return;
  if (libraryPage === "library" && ["ArrowLeft", "ArrowRight"].includes(e.key)) {
    e.preventDefault();
    stepGame(e.key === "ArrowLeft" ? -1 : 1);
  }
  if (e.key === "Escape" && libraryPage !== "library") {
    e.preventDefault();
    if (libraryPage === "store" && webStore.back()) return;
    if (libraryPage === "upload") uploadPane(false);
    else {
      showPage(libraryPage === "detail" ? "mods" : "library");
      render();
    }
  }
});
$("show-settings").addEventListener("click", () => showSettings(!settingsOpen));
$("store-button").addEventListener("click", () => {
  showPage("store");
  if (libraryPage === "store") webStore.open();
});
$("close-settings").addEventListener("click", () => showSettings(false));
$("settings-back").onclick = () => {
  if (settingsSubpage) settingsSubpageView("");
  else showSettings(false);
};
$("open-session").onclick = () => settingsSubpageView("session");
$("open-game-copy").onclick = () => settingsSubpageView("copy");
$("settings-form").noValidate = true;
$("settings-form").addEventListener("submit", saveSettings);
$("settings-form").addEventListener("input", () => {
  $("pref-web-port").setCustomValidity("");
  $("settings-error").textContent = "";
  syncSettingsForm();
});
$("settings-reset").addEventListener("click", () => {
  syncSettingsForm(true);
  $("settings-error").textContent = "";
});
$("download-log").addEventListener("click", downloadLog);
for (const [index, name] of ["general", "advanced", "credits"].entries()) {
  $("tab-" + name).addEventListener("click", () => selectSettingsTab(name));
  $("tab-" + name).addEventListener("keydown", (event) => {
    let target = index;
    if (event.key === "ArrowRight") target = (index + 1) % 3;
    else if (event.key === "ArrowLeft") target = (index + 2) % 3;
    else if (event.key === "Home") target = 0;
    else if (event.key === "End") target = 2;
    else return;
    event.preventDefault();
    selectSettingsTab(["general", "advanced", "credits"][target], true);
  });
}
document.addEventListener("keydown", (event) => {
  if (event.key === "Escape" && settingsOpen) {
    event.preventDefault();
    if (settingsSubpage) settingsSubpageView("");
    else showSettings(false);
  }
});
webStore.bind();
$("pair-address").textContent = location.host;
$("pair-form").addEventListener("submit", (event) => {
  event.preventDefault();
  token = $("pair-code").value;
  if (/^[0-9]{6}$/.test(token)) pair();
});
$("disconnect").addEventListener("click", () => {
  remember("");
  showPair();
});
$("game-select").addEventListener("change", (event) => chooseGame(event.target.value));
$("search").addEventListener("input", () => {
  confirmRemoval = null;
  renderMods();
  renderInspector();
});
$("game-copy").addEventListener("change", async () => {
  const titleId = selectedTitle,
    source = $("game-copy").value,
    generation = state.generation;
  await mutate(async () => {
    await api("/game-source?" + new URLSearchParams({ titleId, source, generation }), {
      method: "POST",
      body: "",
    });
    message("Game copy saved. Check each mod’s apply state.");
  });
  renderGameSource();
});
$("clear-search").addEventListener("click", () => {
  $("search").value = "";
  renderMods();
  renderInspector();
});
$("refresh").addEventListener("click", async () => {
  $("refresh").disabled = true;
  $("refresh").setAttribute("aria-busy", "true");
  try {
    await api("/library/rescan", { method: "POST", body: "" });
    await refresh();
    message("Library refreshed from SD.");
  } catch (error) {
    message(error.message, true);
    try {
      await refresh();
    } catch {
      connection(false, "Connection lost");
    }
  } finally {
    $("refresh").disabled = false;
    $("refresh").removeAttribute("aria-busy");
  }
});
$("copy-title").addEventListener("click", async () =>
  message(
    (await copyText(selectedTitle.toUpperCase()))
      ? "Title ID copied."
      : "Select the Title ID and copy it manually.",
  ),
);
$("copy-log").addEventListener("click", async () =>
  message(
    (await copyText(events.join("\n")))
      ? "Debug log copied."
      : "Select the log text and copy it manually.",
  ),
);
$("show-upload").addEventListener("click", () => uploadPane(true));
$("empty-upload").addEventListener("click", () => uploadPane(true));
$("hide-upload").addEventListener("click", () => uploadPane(false));
$("mod-file").addEventListener("change", (event) => setFile(event.target.files[0]));
for (const type of ["dragenter", "dragover"])
  $("drop-zone").addEventListener(type, (event) => {
    event.preventDefault();
    if (!busy && !upload) $("drop-zone").classList.add("dragging");
  });
for (const type of ["dragleave", "drop"])
  $("drop-zone").addEventListener(type, (event) => {
    event.preventDefault();
    $("drop-zone").classList.remove("dragging");
  });
$("drop-zone").addEventListener("drop", (event) => {
  if (busy || upload) return;
  const files = event.dataTransfer.files;
  if (files.length) {
    const data = new DataTransfer();
    data.items.add(files[0]);
    $("mod-file").files = data.files;
    setFile(files[0]);
  }
});
$("upload-form").addEventListener("submit", sendUpload);
$("upload-cancel").addEventListener("click", async () => {
  if (!upload) return;
  upload.cancelled = true;
  upload.controller?.abort();
  if (!busy) {
    busy = true;
    $("upload-submit").disabled = true;
    $("upload-cancel").disabled = true;
    try {
      await api("/upload/" + upload.key, { method: "DELETE" });
    } catch {
    } finally {
      upload = null;
      busy = false;
      uploadInputs(false);
      $("upload-submit").textContent = "Upload to Switch";
      $("upload-submit").disabled = false;
      $("upload-cancel").hidden = true;
      $("upload-cancel").disabled = false;
      $("upload-error").textContent = "Upload cancelled.";
      renderMods();
      renderInspector();
    }
  }
});
$("toggle-selected").addEventListener("click", () => {
  const mod = currentMod();
  if (mod) toggleMod(mod);
});
$("remove-selected").addEventListener("click", () => {
  confirmRemoval = { key: selectedKey, generation: state.generation };
  renderInspector();
  $("cancel-remove").focus({ preventScroll: true });
});
$("cancel-remove").addEventListener("click", () => {
  confirmRemoval = null;
  renderInspector();
  $("remove-selected").focus({ preventScroll: true });
});
$("confirm-remove").addEventListener("click", async () => {
  const mod = currentMod(),
    confirmation = confirmRemoval;
  if (!mod || !confirmation) return;
  await mutate(async () => {
    await api(
      "/mod/remove?" +
        new URLSearchParams({
          titleId: mod.titleId,
          id: mod.id,
          generation: confirmation.generation,
        }),
      { method: "POST", body: "" },
    );
    confirmRemoval = null;
    selectedKey = "";
    message("Mod removed from the library. Its files remain in recovery storage.");
  });
});
async function poll() {
  if (paired && token && !busy && !settingsSaving)
    try {
      await refresh();
      webStore.pollGames();
    } catch {
      if (paired) connection(false, "Connection lost");
    }
  setTimeout(poll, 2500);
}
if (token) pair();
else showPair();
poll();
