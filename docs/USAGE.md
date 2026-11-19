# Use Moader

[Install Moader](INSTALLATION.md), then use the app or a browser to manage the same library on the Switch. To load mods in a game, make sure the backend is connected in **Settings → Advanced**.

## Connect a browser

1. Connect your Switch and your phone or computer to the same trusted local network.
2. Open Moader. Press **Y** if the web listener is off.
3. Press **ZR**. Open the address shown on the Switch. The default port is **7712**.
4. Enter all six digits of the pairing code, including leading zeros. Keep the code private.

The browser stays available when you leave the app with **+** if the resident service is running. In foreground mode, closing Moader stops its web listener.

## Upload your first mod

1. Open **Upload mod** in the browser and select a ZIP.
2. Enter the game's 16-character hexadecimal **Title ID** and a name for the mod. Use a mod made for that game and version.
3. Upload the ZIP. The mod starts disabled. Select the game, then [choose your game copy](#game-copy-and-cartridges) and enable the mod.

The ZIP should contain the mod files at its top level or inside one enclosing folder. Keep the game's original folder and file names inside `romfs/`.

```text
my-mod.zip
└── romfs/
    └── path/
        └── from-the-game/
            └── example.bin
```

| Put this in the ZIP | For this type of mod |
| --- | --- |
| `romfs/` or `romfs.bin` | Game assets, as loose files or a RomFS image |
| `exefs/` or `exefs.nsp` | Executable files and modules, including compatible additional subSDKs, as loose files or an ExeFS image |
| `exefs_patches/` | Binary `.ips` patches |

Use binary IPS files for patches. Moader can't compile text IPSwitch scripts. The ZIP limit is **128 MiB**, with up to **64 MiB** per expanded file and **256 MiB** expanded in total. Use regular files with unique SD paths and leave out unrelated extras. [ZIP troubleshooting](TROUBLESHOOTING.md#my-zip-is-rejected)

## SD card layout

A browser upload creates the folders for you. To install a mod by hand, put it under the game's lowercase Title ID on SD. Replace `YOUR_TITLE_ID` with the real ID.

```text
/moader/library/YOUR_TITLE_ID/
└── my-mod/
    ├── mod.meta       optional display name, plain text
    └── romfs/
        └── path/from-the-game/example.bin
```

Name the mod folder with up to 48 lowercase letters, digits, hyphens or underscores. Put other supported payloads alongside `romfs/` inside that folder. After copying, choose **Refresh** in the browser, then enable the mod. Leave the payload files in place while a game is using them.

## Enable, disable and remove

Select the mod and toggle it in the browser or press **A** in the app. For the first check, enable the mod with the game closed and then start the game. Use the apply state to see what Moader still needs.

| State | What to do |
| --- | --- |
| Disabled | The mod is off. Reopen content or relaunch the game if it still shows a previously loaded change. |
| Pending | Moader saved the request and is waiting for the filesystem runtime to confirm it. |
| Reopen content | Make the game load the affected content again. The menu or scene you need depends on the game. |
| Relaunch game | Close the game from HOME and start it again. |
| Game unavailable | Check the selected game copy and insert its card if required. |
| Error | Read the mod details and the debug log. |

### Changes while a game is open

An existing loose RomFS file can pick up a same-size replacement on a later read. Cached content must be loaded again. Added or resized files and full images can need a fresh mount, so relaunch the game if reopening the content doesn't work. Always relaunch for ExeFS modules and executable patches, including after disabling or removing them.

To remove a mod, choose **Remove from library → Remove mod** or press **X** twice. Moader hides the entry and keeps its files on SD so older open game files can finish reading. [How the runtime applies changes](RUNTIME.md)

## Game copy and cartridges

Select the game, then open **Settings → Game copy**. Leave it at **Check inserted card** or choose **Cartridge or game-key card** for a physical copy. Keep the correct card inserted, including when a game-key card has downloaded the game. Choose **My digital copy** for a copy you can play without a card.

If the game becomes unavailable, Moader keeps your enable settings and pauses new routing for that title. Read the details for the reason. [Game-copy checks](CARTRIDGES.md)

## Controller controls

| Control | Action |
| --- | --- |
| D-pad | Navigate |
| **L** / **R** | Change game |
| **A** | Toggle a mod or choose an item |
| **X**, then **X** | Confirm removal |
| **B** | Go back or cancel |
| **Y** | Toggle the web listener or reconnect |
| **ZR** | Show the web address and pairing code |
| **ZL** | Show the debug log |
| Press left stick | Open Settings |
| Press right stick | Show full mod details |
| **−** | Save a screenshot |
| **+** | Return to hbmenu |

## Settings

Press the left stick or open **Settings** in the browser. The app saves changes when you make them. In the browser, select **Save settings**.

| Setting | Effect |
| --- | --- |
| Show game names | Use installed names when available. Moader falls back to the Title ID. |
| Reduce motion | Reduce animation. |
| Follow latest events / Follow debug log | Keep the debug view near the latest entries. |
| Start web automatically | Turn the listener on when its host app or service starts. |
| Web port | Choose a port from 1024 to 65535, except 7713. Restart the listener with **Y** to apply it, then press **ZR** for the updated address. |

Use **Advanced** for runtime status, diagnostic exports and [service reload](PLUGIN_RELOAD.md). **Credits** lists the version, license and upstream projects.

## Debugging and screenshots

Press **ZL** in the app or open the browser debug panel. Library events show what you asked Moader to do. Native events can show what the filesystem accepted or read. Press **−** to save an app screenshot. Before sharing a report, [remove private details](TROUBLESHOOTING.md#reporting-a-problem).
