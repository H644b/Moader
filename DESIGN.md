---
name: Moader
description: A cover-led game library for managing mods on a Switch and in its paired browser.
colors:
  ink: "#121918"
  paper: "#F3F6ED"
  mint: "#9CF4CE"
  soft: "#B6C3BA"
  muted: "#9DAEA4"
  rule: "#35463E"
  panel: "#1D2C24"
  selected: "#22382E"
  disabled: "#738078"
  accepted: "#94DBB8"
  pending: "#F7C577"
  error: "#FFAA98"
  button-ink: "#14211B"
typography:
  display:
    fontFamily: "DM Sans, Noto Sans, sans-serif"
    fontSize: "51px"
    fontWeight: 650
    letterSpacing: "-1.8px"
  game-title:
    fontFamily: "DM Sans, Noto Sans, sans-serif"
    fontSize: "49px"
    fontWeight: 650
    letterSpacing: "-1.65px"
  mod-name:
    fontFamily: "DM Sans, Noto Sans, sans-serif"
    fontSize: "25px"
    fontWeight: 630
    letterSpacing: "-0.52px"
  console-body:
    fontFamily: "DM Sans, Noto Sans, sans-serif"
    fontSize: "22px"
    fontWeight: 475
  column-label:
    fontFamily: "DM Sans, Noto Sans, sans-serif"
    fontSize: "17px"
    fontWeight: 630
  browser-body:
    fontFamily: "DM Sans, system-ui, sans-serif"
    fontSize: "16px"
    lineHeight: 1.45
  browser-heading:
    fontFamily: "DM Sans, system-ui, sans-serif"
    fontSize: "clamp(32px, 3.55vw, 51px)"
    fontWeight: 650
    lineHeight: 1.1
    letterSpacing: "-0.045em"
rounded:
  field: "8px"
  button: "9px"
  cover: "10px"
  mod-row: "12px"
  panel: "13px"
  cover-ring: "15px"
spacing:
  console-inset: "64px"
  cover-ring-outset: "6px"
  browser-inset: "clamp(20px, 4.44vw, 64px)"
components:
  library-cover:
    width: "338px"
    height: "338px"
    rounded: "{rounded.cover}"
  opened-cover:
    width: "324px"
    height: "324px"
    rounded: "{rounded.cover}"
  focused-mod-row:
    height: "86px"
    backgroundColor: "{colors.selected}"
    textColor: "{colors.paper}"
    rounded: "{rounded.mod-row}"
  selected-settings-tab:
    backgroundColor: "{colors.selected}"
    textColor: "{colors.mint}"
  connection-panel:
    backgroundColor: "{colors.panel}"
    textColor: "{colors.paper}"
    rounded: "{rounded.panel}"
  store-disabled:
    textColor: "{colors.disabled}"
  browser-primary:
    backgroundColor: "{colors.mint}"
    textColor: "{colors.button-ink}"
    rounded: "{rounded.button}"
    padding: "8px 15px"
  browser-secondary:
    textColor: "{colors.paper}"
    rounded: "{rounded.button}"
    padding: "8px 15px"
  browser-field:
    backgroundColor: "{colors.panel}"
    textColor: "{colors.paper}"
    rounded: "{rounded.field}"
    padding: "8px 12px"
---

# Moader design system

## Overview

**The cartridge library.** Game covers carry the identity of each title. The selected game occupies the center of the library, with its name and mod counts below. Opening it moves the cover beside its installed mods, where it stays while the player inspects a change.

The mint stacked-cartridge mark and lowercase paper wordmark identify Moader on both surfaces. Use the square icon for hbmenu, the browser favicon and artwork fallbacks. [Identity artwork and export recipe](branding/README.md)

## Colors

Mint identifies focus, the current page indicator and primary actions. Paper carries names, headings and values. Soft green-gray carries descriptions and adjacent game names. Muted green-gray carries column labels, file counts and secondary controls.

Ink is the canvas. Fine rules divide controls and facts. A deep green selection fill identifies the current row or settings section. In the native palette, disabled gray belongs to unavailable controls, including Store. The browser uses its disabled button styling for the same state. The browser names the paper foreground `--ink` and the dark background `--canvas`. [Native palette](native/canvas.hpp) · [Browser palette](web/style.css)

Runtime states have a colored dot and a text label. Green identifies **Accepted** on console or **Routing active** in the browser. Amber identifies **Pending**, **Reopen content** or **Relaunch game**. Coral identifies **Error** and **Game unavailable**. **Disabled** uses muted text. **Requested** shows the player’s choice separately. [Runtime state guide](docs/USAGE.md#enable-disable-and-remove)

## Typography

The native renderer embeds DM Sans at weights 475 for reading text, 630 for labels and 650 for display headings. It uses Noto Sans when a glyph is missing. The browser serves DM Sans locally and uses the system sans-serif fallback. Bricolage Grotesque appears as outlines in the wordmark. [Native font embedding](native/embed-font.py) · [Served web assets](tools/embed-web.py)

The game name is the largest centered text. Mod rows pair a strong name with a smaller line for content type and file count. Keep the complete Title ID available in details. Pairing codes retain leading zeros and appear in two groups of three on the console.

## Layout

Native measurements use a **1440 × 810** artboard. The production framebuffer is **1280 × 720**, with all coordinates scaled by 8/9. Place header and footer rules at y = 91 and y = 730, with a 64-unit inset on both sides. The lockup starts at (64, 30). The breadcrumb follows on the left and Store and Settings sit on the right. [Framebuffer setup](native/app.cpp) · [Header and library renderer](native/ui.cpp)

| View | Composition in artboard units |
| --- | --- |
| Game library | Center the 338-unit cover at (720, 373). Immediate neighbors are 259 units square at ±351 units from center. The next pair is 221 units square at ±647 and clips at the viewport edges. Place the selected title below the cover, then the two counts and page indicators. |
| Installed mods and details | Keep the 324-unit cover centered at (228, 305). A rule at x = 443 separates it from content beginning at x = 496. The list shows four rows at a time. Details retain the title context, Requested and Runtime, content, file count, package size and mod ID. |
| Console Settings | Start the 298-unit section list at x = 64. Put the selected game below it, a rule at x = 394 and section content at x = 448. Listener state, pairing and diagnostic history belong inside Settings. |

The browser caps its content at 1440 CSS pixels and uses fluid margins. On wide screens the opened game occupies the left 30.8% of the content. At 720 CSS pixels and narrower the cover becomes a 65-pixel thumbnail beside the game name above the mod controls. Settings sections become wrapping tabs. [Browser layout](web/style.css)

## Elevation & Depth

Scale, overlap and dimming establish depth in the carousel. On console, the selected artwork lightly tints the background. The browser uses a soft green radial background on the library. Adjacent covers are shaded by 18% and the next pair by 50%. Settings uses a flat ink background and tonal panels. A removal confirmation dims the console frame and brings a single bordered panel forward.

## Shapes

Preserve the square crop of game artwork and its rounded corners. The native focused cover has a 3.6-unit mint ring with a 6-unit outset and a 15-unit outer radius. The browser uses a 3-pixel ring at the same outset. Round arrows flank the covers. A short mint bar replaces the dot at the current position. [Cover geometry](native/ui.cpp) · [Browser carousel](web/style.css)

## Components

**Library navigation.** L/R, left/right or a horizontal swipe browses games. A opens the centered game, then opens the selected mod, then toggles the request from its details. B retraces that path. The selected cover moves left as neighboring covers fade. Reduced motion snaps directly to the destination. In the browser, cover buttons open games and arrow buttons, arrow keys and swipes change the selection. [Native input](native/navigation.cpp) · [Browser input](web/app.js)

**Mod list.** Keep Mod, Requested and Runtime in separate columns. The focused console row has a mint border, green fill and a chevron. The detail view pairs the requested value with the runtime response. Long detail text scrolls. Removal requires confirmation.

**Settings.** The console has General, Connection, Advanced, Controls and Credits. The left stick opens Settings, ZR opens Connection and ZL opens the debug log. The browser has General, Advanced and Credits, with Game copy under General and Current session under Advanced. Connection status and Disconnect sit in its Settings sidebar. Preferences save immediately on console and explicitly through **Save settings** in the browser. Per-game copy selection saves immediately on both. [Console settings](native/settings_screen.cpp) · [Browser settings](web/index.html)

**Store.** Keep the bag icon and label visible in gray and outside the focus order while the entry is inactive. The preview screens have a per-game catalog, creator previews and labeled community hosts. Players can use a publisher’s website to download a ZIP for browser upload. [Store presentation](native/store_screen.cpp) · [Community repository template](https://github.com/H644b/moader-mod-repo-template)

## Do's and Don'ts

- Keep unavailable cover art readable with a cartridge mark and the game name or Title ID.
- Keep the selected game and mod row when the player backs out of details.
- Keep listener status and pairing in Settings and give actionable library errors near the affected controls.
- Use the runtime’s reported result for apply status. A saved request confirms only the player’s choice.
- Identify renderer captures as host fixtures. Console input, networking and gameplay each require direct observation. [Validation record](docs/VALIDATION.md)
