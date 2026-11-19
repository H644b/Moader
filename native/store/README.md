# Community Store

The foreground app compiles a native per-game browser with category shelves, host filters, a game picker, creator galleries, and links to the original hosts. The Store control is disabled by `StoreAvailable` for this preview. The library, settings and refresh loop make no community requests while the Store is closed.

The bundled `catalog.json` is a curated snapshot of the listings used in the design. Each entry records the original creator, host page and preview URLs. `cache.json` maps those URLs to the compressed preview images in this directory. The snapshot contains listing materials. Game art in the installed library and picker comes from installed application metadata.

When Store access is enabled, the client reads one HTTPS catalog URL per line from `sdmc:/moader/store/sources.txt`, loads feeds in the background, and fetches images as a release becomes visible. Source URLs and catalog fields stay separate from the console’s pairing credentials. The browser opens the creator’s source page when a player chooses a host.

Authors can copy the [publisher template](https://github.com/H644b/moader-mod-repo-template) for the catalog format, example package, GitHub Pages workflow and self-hosting instructions. Its [live catalog](https://h644b.github.io/moader-mod-repo-template/catalog.json) is a working independent feed.
