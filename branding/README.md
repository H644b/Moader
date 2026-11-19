# Moader mark

[`mark.svg`](mark.svg) is the canonical Moader M. Use the same shape in every app icon and interface header.

| File | Used by |
| --- | --- |
| `mark.svg` | Canonical vector geometry |
| `hbmenu-source.svg` | Background and composition for the homebrew icon |
| `hbmenu.jpg` | The exact 256 × 256 RGB icon embedded in `Moader.nro` |
| `mark-alpha.hpp` | Alpha mask used by the native software renderer |
| `favicon.svg` | Browser favicon export |
| `../web/icon.svg` | Browser header and favicon served by the Switch |

The exported shapes share a `0 0 64 64` view box. The native mask is 96 × 96. The hbmenu icon adds a background while preserving the same M geometry. The mark is original project artwork.

## Regenerating assets

Use the checked-in exports for regular builds. When updating the artwork:

1. Read [`export.py`](export.py) and update the canonical geometry/colors there and in `mark.svg` consistently.
2. Install the Python dependencies used by the exporter in a local environment.
3. Run `python3 branding/export.py` from the repository root.
4. Inspect the generated light/dark and small-size previews locally. Verify the native alpha mask, header SVG and RGB JPEG still depict the same shape.
5. Rebuild the app and inspect both interfaces before shipping the change.

Generated review sheets and local design notes are not part of the public source. The public interface examples have their own [provenance notes](../docs/images/README.md).
