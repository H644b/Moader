# Moader identity

Two offset game cartridges form the Moader mark. The front cartridge has a clipped shoulder and two contact cutouts. In the header, the mint mark sits to the left of the paper-colored lowercase wordmark. Both interfaces use the outlined lettering in the lockup. The square app icon uses a dark mark on mint.

| Asset | Role |
| --- | --- |
| [mark.svg](mark.svg) | Single-color cartridge geometry on a 64 × 64 view box. Set its color with `currentColor`. |
| [lockup.svg](lockup.svg) | Two-color header artwork with lettering outlined from Bricolage Grotesque. The browser serves this at `/lockup.svg`. |
| [store.svg](store.svg) | A 24 × 24 shopping bag with cartridge contacts. The Store control uses this silhouette in gray while inactive. |
| [hbmenu-source.svg](hbmenu-source.svg) | Square icon artwork with a `#17231D` mark on a `#9CF4CE` ground. |
| [hbmenu.jpg](hbmenu.jpg) | Opaque 256 × 256 RGB icon embedded in the foreground NRO. |
| [setup-icon.svg](setup-icon.svg), [setup-icon.png](setup-icon.png), and [setup-icon.ico](setup-icon.ico) | Rounded version of the same hbmenu artwork for the desktop Setup window, macOS Dock, Linux launcher and Windows icon. The square console icon stays unchanged. |
| [mark-alpha.hpp](mark-alpha.hpp) | Generated masks for the native renderer, including the 96 × 96 mark, two 348 × 69 lockup layers and the Store glyph. |
| [favicon.svg](favicon.svg) and [web/icon.svg](../web/icon.svg) | Square browser icon exports. The web interface also uses this artwork when a cover is unavailable. |

DM Sans supplies interface text. The native renderer uses Noto Sans as its fallback for missing characters. The wordmark’s shape and spacing come from the vector paths. [Bricolage Grotesque license](Bricolage-OFL.txt) · [DM Sans license](../native/assets/DMSans-OFL.txt) · [Noto Sans license](../native/assets/NotoSans-OFL.txt)

## Regenerate exports

1. Edit the source SVG for the asset you are changing. For a cartridge shape change, carry the same paths into the mark, lockup and hbmenu source.
2. With Python, Pillow and `rsvg-convert` available, run `python3 branding/export.py` from the repository root.
3. Inspect the exported JPEG, browser icon and native lockup at their display sizes. The two native lockup layers should register exactly, including the contact cutouts. The exporter also updates the SHA-256 records. [Export recipe](export.py) · [Checksums](checks.json)
