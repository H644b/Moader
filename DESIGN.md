# Interface design

Moader is a workbench for managing mods while keeping their requested settings separate from what the runtime has actually accepted.

## Layout

Both interfaces use the same structure:

- **Games, left:** choose an installed game's mod group. Names come from available installed-game metadata; Title IDs remain available and are the fallback.
- **Mods, center:** upload, inspect, enable/disable or remove a mod. Requested state and apply state are different columns.
- **Debug, right:** persistent runtime status and recent events. Opening Settings does not remove the debug context.

The Switch version uses controller focus and a fixed footer of available actions. The browser version supports pointer, keyboard and narrow viewports. The layouts adapt to their input methods rather than requiring an in-game overlay.

## State language

Use concrete descriptions of the observed state:

| State | Meaning |
| --- | --- |
| Disabled | The user has not requested this mod. |
| Pending | A request is saved, but the current runtime has not acknowledged it. |
| Accepted / future reads | The runtime accepted routing. The game may still have cached content. |
| Reopen required | This kind of change needs content to reopen or the game to relaunch. |
| Error | A specific operation failed; preserve its result and diagnostic context. |
| Disconnected | Current service state is unavailable. Label retained values as historical rather than displaying a green current-state claim. |

Do not replace evidence with an optimistic success toast. A library write, a runtime receipt and a visible game change are separate observations.

## Visual language

Use plain reading surfaces, fine borders, clear focus rings and a restrained teal action color. The dark debug panel gives status a stable place without making every event compete with the mod controls.

| Token | Light value |
| --- | --- |
| Canvas | `#eef2f3` |
| Surface | `#ffffff` |
| Main text | `#1e303a` |
| Secondary text | `#536772` |
| Border | `#dce4e8` |
| Action | `#175f79` |
| Selection | `#e8f2f6` |
| Debug background | `#182b35` |

The browser uses local system fonts. The console embeds Noto Sans under its included Open Font License. Use monospace for identifiers, paths and diagnostic data, not for the main reading interface.

## Motion and accessibility

Motion should explain a focus change, toggle or panel transition. Browser transitions generally last 160–220 ms; the native renderer eases selection and panel movement. Respect both the shared Reduce motion preference and the browser's system reduced-motion setting. Do not make navigation depend on an animation completing.

Keep controls readable and labeled, preserve keyboard/controller focus after operations, wrap long mod names and retain the full Title ID. Avoid horizontal overflow at narrow browser widths. Log following should preserve a user's position when they scroll up to read history.

## Settings

General settings expose game names/Title IDs, reduced motion, following the debug log and automatic web startup. Advanced contains port editing and diagnostic information/exports. Credits names the actual upstream projects and licenses.

Settings are shared on the Switch. A partial edit must preserve another client's unedited values. A changed saved web port applies the next time the listener starts.

## Logo and implementation

The [canonical M mark](branding/mark.svg) supplies the hbmenu icon, native header, browser header and favicon. See [branding](branding/README.md) before changing an export.

- Browser layout and motion: [`web/style.css`](web/style.css), [`web/app.js`](web/app.js).
- Native renderer and focus: [`native/ui.cpp`](native/ui.cpp), [`native/ui.hpp`](native/ui.hpp).
- Public example images: [`docs/images`](docs/images/README.md). Their sample libraries are explicitly synthetic.

Changing a screenshot or a mock does not establish console behavior. The tested scope belongs in [validation](docs/VALIDATION.md).
