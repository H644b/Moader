// The catalog is public. Only the local API receives the Switch's pairing code.
const webStore = (() => {
  const origin = "https://moader.timsfun.com";
  const feed = origin + "/catalog.json";
  const limit = 128 * 1024 * 1024;
  let releases = [],
    selected = null,
    detail = false,
    frame = 0,
    loaded = 0;
  let catalogTask = null,
    catalogError = "",
    generation = 0;
  let membership = new Map(),
    owned = false,
    checking = false,
    lookup = 0;
  let checkedAt = 0,
    checkedLibrary = null;
  let transfer = null,
    transferAbort = null;

  function fileUrl(value) {
    if (typeof value !== "string" || !value.startsWith(origin + "/files/")) return "";
    try {
      const url = new URL(value);
      return url.origin === origin &&
        url.pathname.startsWith("/files/") &&
        !url.username &&
        !url.password
        ? url.href
        : "";
    } catch {
      return "";
    }
  }
  function publicGet(url, options = {}) {
    return fetch(url, {
      mode: "cors",
      credentials: "omit",
      referrerPolicy: "no-referrer",
      redirect: "error",
      ...options,
    });
  }
  function parseGame(game, entries) {
    if (!/^0[14][a-f0-9]{14}$/i.test(game.titleId) || !Array.isArray(game.mods)) return;
    const titleId = game.titleId.toLowerCase();
    for (const mod of game.mods) {
      if (
        !mod ||
        typeof mod.title !== "string" ||
        !mod.title.trim() ||
        !/^[a-z0-9_-]{1,48}$/.test(mod.id)
      )
        continue;
      const key = titleId + "/" + mod.id;
      const data = mod.download || {};
      const downloadable =
        fileUrl(data.url) &&
        Number.isSafeInteger(data.bytes) &&
        data.bytes >= 22 &&
        data.bytes <= limit &&
        /^[0-9a-f]{64}$/i.test(data.sha256) &&
        new TextEncoder().encode(mod.title).length <= 120;
      entries.set(key, {
        ...mod,
        key,
        titleId,
        game: game.title || game.titleId,
        previews: Array.isArray(mod.previews) ? mod.previews.map(fileUrl).filter(Boolean) : [],
        package: downloadable ? { url: fileUrl(data.url), size: data.bytes, hash: data.sha256 } : null,
      });
    }
  }
  async function load() {
    if (catalogTask) return catalogTask;
    const session = generation;
    catalogError = "";
    catalogTask = (async () => {
      try {
        const entries = new Map(),
          visited = new Set();
        let url = feed;
        while (url && session === generation) {
          if (visited.has(url)) throw new Error("The catalog returned a repeated page.");
          visited.add(url);
          const response = await publicGet(url, { signal: AbortSignal.timeout(15000) });
          if (!response.ok) throw new Error("The catalog is unavailable.");
          const page = await response.json();
          if (page.format !== "moader.catalog.v1" || !Array.isArray(page.games))
            throw new Error("The catalog format is unavailable.");
          for (const game of page.games) parseGame(game, entries);
          url = page.next || "";
          if (
            url &&
            !(
              url.startsWith(feed + "?after=") &&
              /^[a-f0-9-]{36}$/i.test(url.slice((feed + "?after=").length))
            )
          )
            throw new Error("The catalog returned an invalid next page.");
        }
        if (session !== generation) return;
        releases = [...entries.values()].sort(
          (a, b) => a.game.localeCompare(b.game) || a.title.localeCompare(b.title),
        );
        loaded = Date.now();
        if (owned) await checkGames();
      } catch (error) {
        if (session === generation) catalogError = "Check your internet connection and try again.";
      } finally {
        if (session === generation) {
          catalogTask = null;
          renderBrowse();
        }
      }
    })();
    renderBrowse();
    return catalogTask;
  }
  async function checkGames() {
    const turn = ++lookup;
    const libraryGeneration = state.generation;
    const ids = [...new Set(releases.map((m) => m.titleId))];
    const result = new Map();
    checking = true;
    renderBrowse();
    try {
      for (let i = 0; i < ids.length; i += 64) {
        const data = await api("/store/games", {
          method: "POST",
          body: ids.slice(i, i + 64).join("\n"),
        });
        if (turn !== lookup) return;
        for (const [id, value] of Object.entries(data)) result.set(id, value);
      }
    } catch {
      /* A paired Library match can still be shown when system lookup fails. */
    }
    if (turn === lookup) {
      membership = result;
      checking = false;
      checkedAt = Date.now();
      checkedLibrary = libraryGeneration;
      renderBrowse();
    }
  }
  function pollGames() {
    if (
      owned &&
      libraryPage === "store" &&
      !detail &&
      !settingsOpen &&
      !document.hidden &&
      !checking &&
      !catalogTask &&
      (checkedLibrary !== state.generation || Date.now() - checkedAt >= 30000)
    )
      checkGames();
  }
  function hasGame(id) {
    return membership.get(id) === true || state.mods.some((m) => m.titleId === id);
  }
  function art(el, mod, n, thumbnail = false) {
    const label = node("span", "art-mark", thumbnail ? "M" : mod.title);
    label.setAttribute("aria-hidden", "true");
    el.replaceChildren(label);
    if (!mod.previews[n]) return;
    const img = document.createElement("img");
    img.alt = thumbnail ? "" : mod.title + " preview " + (n + 1);
    img.referrerPolicy = "no-referrer";
    img.loading = thumbnail ? "lazy" : "eager";
    img.addEventListener("error", () => img.remove(), { once: true });
    img.src = mod.previews[n];
    el.append(img);
  }
  function preview(mod) {
    selected = mod;
    art($("store-feature-art"), mod, 0);
    $("store-feature-art").setAttribute("aria-label", "View " + mod.title);
    $("store-feature-game").textContent = mod.game;
    $("store-feature-title").textContent = mod.title;
    $("store-feature-summary").textContent = mod.summary || "";
    for (const b of $("store-list").querySelectorAll("button"))
      b.setAttribute("aria-current", String(b.dataset.key === mod.key));
  }
  function openDetail(mod) {
    selected = mod;
    detail = true;
    frame = 0;
    $("store-browse").hidden = true;
    $("store-detail").hidden = false;
    $("store-detail-game").textContent = mod.game;
    $("store-category").textContent = mod.category || "";
    $("store-host").textContent = mod.sources?.[0]?.label ? "Hosted by " + mod.sources[0].label : "";
    $("store-date").textContent = (mod.published || "").slice(0, 10);
    $("store-detail-title").textContent = mod.title;
    $("store-author").textContent = mod.author ? "by " + mod.author : "";
    $("store-description").textContent = mod.description || mod.summary || "";
    $("store-version").textContent = mod.version || "—";
    $("store-size").textContent = mod.package ? bytes(mod.package.size, 1) : "Unavailable";
    $("store-license").textContent = mod.license || "See publisher";
    gallery();
    updateAction();
    $("store-detail-title").focus({ preventScroll: true });
  }
  function gallery() {
    art($("store-detail-art"), selected, frame);
    $("store-gallery").hidden = selected.previews.length < 2;
    $("store-frame").textContent = "Preview " + (frame + 1) + " of " + selected.previews.length;
  }
  function renderBrowse() {
    if (detail || !paired || libraryPage !== "store") return;
    $("store-browse").hidden = false;
    $("store-detail").hidden = true;
    const terms = $("store-search").value.trim().toLowerCase().split(/\s+/).filter(Boolean);
    const results = releases.filter(
      (m) =>
        (!owned || hasGame(m.titleId)) &&
        terms.every((t) => [m.game, m.title, m.author, m.category].join(" ").toLowerCase().includes(t)),
    );
    const status = $("store-check");
    status.textContent =
      owned && checking
        ? "Checking games on this Switch…"
        : owned && releases.some((m) => !hasGame(m.titleId) && membership.get(m.titleId) == null)
          ? "Some games could not be checked."
          : "";
    $("store-shelf").hidden = !results.length;
    $("store-empty").hidden = !!results.length;
    $("store-results-title").textContent = terms.length
      ? "Search results"
      : owned
        ? "On this Switch"
        : "All mods";
    $("store-count").textContent = counted(results.length, "mod");
    if (!results.length) {
      const waiting = catalogTask && !releases.length;
      $("store-empty-title").textContent = waiting
        ? "Loading the catalog…"
        : catalogError
          ? "Catalog unavailable"
          : checking
            ? "Checking games…"
            : terms.length
              ? "No matching mods"
              : owned
                ? "No mods for games on this Switch"
                : "No published mods";
      $("store-empty-hint").textContent =
        waiting || checking
          ? ""
          : catalogError ||
            (terms.length
              ? "Try a different game, mod or creator."
              : owned
                ? "Turn off the game filter to see all mods."
                : "");
      $("store-retry").hidden = !catalogError;
      $("store-list").replaceChildren();
      return;
    }
    const focusedKey = document.activeElement?.closest(".store-row")?.dataset.key;
    const rows = document.createDocumentFragment();
    for (const mod of results) {
      const li = document.createElement("li"),
        button = node("button", "store-row");
      button.type = "button";
      button.dataset.key = mod.key;
      const thumb = node("span", "store-thumb"),
        title = node("span");
      art(thumb, mod, 0, true);
      title.append(node("strong", "", mod.title), node("small", "", mod.game));
      button.append(thumb, title);
      button.onfocus = () => preview(mod);
      button.onpointerenter = (e) => {
        if (e.pointerType === "mouse") preview(mod);
      };
      button.onclick = () => openDetail(mod);
      li.append(button);
      rows.append(li);
    }
    $("store-list").replaceChildren(rows);
    preview(results.find((m) => m.key === selected?.key) || results[0]);
    if (focusedKey)
      Array.from($("store-list").querySelectorAll("button"))
        .find((b) => b.dataset.key === focusedKey)
        ?.focus({ preventScroll: true });
  }
  function updateAction() {
    if (
      transfer &&
      !transfer.running &&
      !transfer.error &&
      connected &&
      inventoryAvailable() &&
      !state.mods.some((m) => keyOf(m) === transfer.key)
    ) {
      transfer = null;
      $("store-transfer").hidden = true;
    }
    if (!detail || !selected) return;
    const exists = state.mods.some((m) => keyOf(m) === selected.key);
    const active = transfer?.running;
    const button = $("store-install");
    button.disabled =
      exists || busy || active || !!upload || !selected.package || !connected || !inventoryAvailable();
    button.textContent = exists
      ? "Installed"
      : active
        ? transfer.key === selected.key
          ? "Installing…"
          : "Another install is in progress"
        : !selected.package
          ? "Download unavailable"
          : !connected || !inventoryAvailable()
            ? "Library unavailable"
            : transfer?.error && transfer.key === selected.key
              ? "Retry installation"
              : "Download & install";
    $("store-install-hint").textContent = exists
      ? "Manage this mod in your Library."
      : upload
        ? "Finish or cancel the ZIP upload first."
        : button.disabled
          ? ""
          : "Adds the mod to your Library, switched off.";
  }
  function progress(text, percent = null, error = false) {
    $("store-transfer").hidden = false;
    $("store-transfer").dataset.error = String(error);
    $("store-transfer").dataset.running = String(!!transfer?.running);
    $("store-transfer-label").textContent = text;
    const bar = $("store-progress");
    bar.hidden = !transfer?.running;
    if (percent === null) bar.removeAttribute("value");
    else bar.value = percent;
  }
  async function install() {
    updateAction();
    if (!selected || $("store-install").disabled) return;
    const mod = selected,
      pkg = mod.package,
      session = generation;
    const controller = new AbortController();
    transferAbort = controller;
    transfer = { key: mod.key, running: true, error: false };
    busy = true;
    let uploadPath,
      committing = false;
    updateControls();
    progress("Downloading and verifying " + mod.title + "…");
    try {
      if (!("integrity" in Request.prototype))
        throw new Error("Use a browser that supports verified downloads.");
      const digest = btoa(String.fromCharCode(...pkg.hash.match(/../g).map((s) => parseInt(s, 16))));
      const response = await publicGet(pkg.url, {
        integrity: "sha256-" + digest,
        signal: controller.signal,
      });
      if (!response.ok) throw new Error("The catalog download is unavailable.");
      const blob = await response.blob();
      if (blob.size !== pkg.size) throw new Error("The download size does not match the catalog.");
      if (session !== generation || controller.signal.aborted) return;
      const begin = await api(
        "/upload/begin?" +
          new URLSearchParams({
            titleId: mod.titleId,
            id: mod.id,
            name: mod.title,
            size: pkg.size,
          }),
        { method: "POST", body: "", signal: controller.signal },
      );
      uploadPath = "/upload/" + encodeURIComponent(begin.uploadId);
      transfer.path = uploadPath;
      await sendUploadChunks(uploadPath, blob, 0, controller.signal, (offset) => {
        progress(
          "Sending " + mod.title + " to your Switch · " + bytes(offset) + " of " + bytes(blob.size),
          Math.floor((100 * offset) / blob.size),
        );
      });
      progress("Installing " + mod.title + "…");
      committing = true;
      transfer.path = null;
      await api(uploadPath, { method: "POST", body: "", signal: controller.signal });
      uploadPath = null;
      await refresh();
      if (session === generation)
        progress(mod.title + " installed. You can enable it in your Library.", 100);
    } catch (error) {
      if (session !== generation) return;
      if (committing) {
        try {
          await refresh();
        } catch {}
      }
      if (committing && state.mods.some((m) => keyOf(m) === mod.key))
        progress(mod.title + " is installed.", 100);
      else {
        transfer.error = true;
        progress(
          error instanceof TypeError
            ? "Download or verification failed. Check your connection and try again."
            : error.message,
          null,
          true,
        );
      }
    } finally {
      if (uploadPath && session === generation) {
        try {
          await api(uploadPath, { method: "DELETE" });
        } catch {}
      }
      if (session === generation) {
        transfer.running = false;
        transfer.path = null;
        $("store-transfer").dataset.running = "false";
        transferAbort = null;
        busy = false;
        $("store-progress").hidden = true;
        updateControls();
      }
    }
  }
  function back() {
    if (!detail) return false;
    detail = false;
    renderBrowse();
    Array.from($("store-list").querySelectorAll("button"))
      .find((b) => b.dataset.key === selected?.key)
      ?.focus();
    return true;
  }
  function open() {
    detail = false;
    renderBrowse();
    if (!loaded || Date.now() - loaded > 60000) load();
    else if (owned) checkGames();
  }
  function disconnect() {
    generation++;
    lookup++;
    if (transfer?.running) busy = false;
    transferAbort?.abort();
    transferAbort = null;
    transfer = null;
    releases = [];
    selected = null;
    detail = false;
    loaded = 0;
    catalogTask = null;
    catalogError = "";
    owned = false;
    checking = false;
    membership.clear();
    $("store-transfer").hidden = true;
    $("store-search").value = "";
    $("store-owned").setAttribute("aria-checked", "false");
  }
  function bind() {
    window.addEventListener("pagehide", () => {
      if (transfer?.path && token) {
        fetch("/api" + transfer.path, {
          method: "DELETE", keepalive: true, mode: "same-origin", headers: { Authorization: "Bearer " + token },
        }).catch(() => {});
      }
    });
    $("store-search").oninput = renderBrowse;
    $("store-owned").onclick = () => {
      owned = !owned;
      $("store-owned").setAttribute("aria-checked", String(owned));
      if (owned) checkGames();
      else {
        lookup++;
        checking = false;
        renderBrowse();
      }
    };
    $("store-feature-art").onclick = () => selected && openDetail(selected);
    $("store-back").onclick = back;
    $("store-retry").onclick = load;
    $("store-install").onclick = install;
    for (const [id, direction] of [
      ["store-prev", -1],
      ["store-next", 1],
    ]) {
      $(id).onclick = () => {
        frame = (frame + direction + selected.previews.length) % selected.previews.length;
        gallery();
      };
    }
  }
  return { bind, open, back, disconnect, updateAction, pollGames };
})();
