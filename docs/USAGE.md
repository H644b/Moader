# Use Moader

Open **Moader** from hbmenu. If **Game library** is empty, pair a browser and upload a mod. To play with mods, first [check the runtime connection](INSTALLATION.md#check-your-setup).

## Connect a browser

1. Connect your Switch and your phone or computer to the same trusted local network.
2. Open Moader and press **ZR** for **Settings → Connection**. If it says **Service disconnected**, press **Y** to reconnect.
3. If **Web listener is off** appears, press **Y** or choose **Start listener**. On your phone or computer, open the **Local address** shown on the Switch. The default port is **7712**.
4. Enter the six digits shown under **Pairing code**, including leading zeros. Keep the code private.

The browser stays available when you leave the app with **+** if the resident service is running. In foreground mode, closing Moader stops its web listener.

## Upload your first mod

1. Open **Upload mod** in the browser and select a ZIP.
2. Enter the game's 16-character hexadecimal **Title ID** and a name for the mod. Use a mod made for that game and version.
3. Select **Upload to Switch**. The mod starts disabled. Open the game in Moader, [choose your game copy](#game-copy-and-cartridges), and enable the mod.

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

For patches, use binary IPS files. The ZIP can be up to **128 MiB**. After extraction, each file can be up to **64 MiB** and the total can be up to **256 MiB**. Include only mod files and use a unique path for each file. [ZIP troubleshooting](TROUBLESHOOTING.md#my-zip-is-rejected)

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

## Browse the game library

Press **L** or **R** to browse games. Press **A** to open the centered game. In **Installed mods**, use **↑** and **↓** to choose a mod and **A** to open its details. Press **B** to go back.

In a browser, select a cover to open the game, then select a mod name for details. Each cover represents a game with mods on SD. Moader uses the installed game's artwork when it is available.

## Enable, disable and remove

1. On the console, open **Mod details** and press **A** to enable or disable the mod. In the browser, use its **Requested** switch.
2. Check the **Runtime** column in the browser or the apply state in mod details. **Requested** shows your saved choice. **Runtime** reports whether the filesystem has acknowledged it.
3. For the first check, enable the mod before starting the game. If the game is already open, follow the action shown in the apply state.

| State | What to do |
| --- | --- |
| Disabled | The mod is off. Reopen content or relaunch the game if it still shows a previously loaded change. |
| Pending | Moader saved the request and is waiting for the filesystem runtime to confirm it. |
| Accepted / Routing active | The filesystem accepted routing for future reads. The game can still be using cached content. |
| Reopen content | Make the game load the affected content again. The menu or scene you need depends on the game. |
| Relaunch game | Close the game from HOME and start it again. |
| Game unavailable | Check the selected game copy and insert its card if required. |
| Error | Read the mod details and the debug log. |

### Changes while a game is open

An existing loose RomFS file can pick up a same-size replacement on a later read. Cached content must be loaded again. Added or resized files and full images can need a fresh mount, so relaunch the game if reopening the content doesn't work. Always relaunch for ExeFS modules and executable patches, including after disabling or removing them.

To remove a mod, choose **Remove from library → Remove mod** or press **X** twice. Moader hides the entry and keeps its files on SD so older open game files can finish reading. [How the runtime applies changes](RUNTIME.md)

## Game copy and cartridges

Select the game, then open **Settings → General → Game copy**. Leave it at **Check inserted card** or choose **Cartridge or game-key card** for a physical copy. Keep the correct card inserted, including when a game-key card has downloaded the game. Choose **My digital copy** for a copy you can play without a card.

If the game becomes unavailable, Moader keeps your enable settings and pauses new routing for that title. Read the details for the reason. [Game-copy checks](CARTRIDGES.md)

## Controller controls

| Control | Action |
| --- | --- |
| D-pad | Browse games with **← / →**. Use **↑ / ↓** to choose a mod or scroll its details. In Settings, **←** moves to the section list and **→** returns to its controls. |
| **L** / **R** | Browse games in the library. In the Store, **R** toggles the game filter. |
| **X** in the Store | Search the catalog |
| **A** | Open the game, open the selected mod, then enable or disable it from its details. Select a control in Settings. |
| **X**, then **X** | Open and confirm removal of the selected mod |
| **B** | Go back or cancel |
| **Y** | Start or stop the web listener and show **Connection**. If the service is disconnected, retry the connection. |
| **ZR** | Open or close **Connection** to see the local address and pairing code |
| **ZL** | Open or close the session details and debug log |
| Press left stick | Open Settings |
| Press right stick | Open the game from the library or open the selected mod’s details from **Installed mods** |
| **−** | Save a screenshot |
| **+** | Return to hbmenu |

## Settings

Press the left stick on the console or open **Settings** in the browser. Console settings save as you change them. In the browser, select **Save settings** to apply preference edits, or **Discard edits** to restore their saved values. **Game copy** saves immediately on either interface.

| Setting | Effect |
| --- | --- |
| Show game names | Use installed names when available. Moader falls back to the Title ID. |
| Reduce motion | Reduce animation. |
| Follow debug log | Follow new entries until you scroll up to read earlier events. |
| Start web automatically | Turn the listener on when its host app or service starts. |
| Web listener port / Web port | In **Advanced**, choose a port from 1024 to 65535, except 7713. The port applies the next time the listener starts. Read its new address in **Connection**. |

On the console, **Connection** shows listener status and pairing, **Advanced** contains runtime details, exports and service reload, and **Controls** lists console and browser actions. The browser’s Settings sidebar shows its connection status and **Disconnect**. **Credits** identifies the project and its dependencies. [Service reload guide](PLUGIN_RELOAD.md)

### Update the app and resident plugin

Open **Settings → Updates** in the console app and choose **Check for updates**. You can also turn on **Automatically check for updates** to check at launch and every six hours while Moader is open. Moader looks for the latest stable official release. When a newer version is available, choose **Install** or **Later**.

Keep Moader open while it downloads and verifies both files, installs them on SD and restarts the resident plugin. If the update is interrupted, reopen Moader and use **Finish update**. After it reports **Update installed**, press **+** to exit and reopen Moader to run the new app. Updating these files does not start a backend on a new console boot. [Installation](INSTALLATION.md)

## Debugging and screenshots

Press **ZL** in the app or open **Settings → Advanced → Current session** in the browser for the debug log. Library events show what you asked Moader to do. Native events can show what the filesystem accepted or read. Choose **Download debug log** in the browser’s **Advanced** section to save a copy, or press **−** on the console to save an app screenshot. Remove private details before sharing a report. [Reporting a problem](TROUBLESHOOTING.md#reporting-a-problem)

## Browse and install from the Store

Choose **Store** in the console app or paired browser to browse community releases. Search by mod, game or creator; use **On this Switch** to narrow the list to games Moader can find from the console’s game records, play history, saves or Library. This filter helps browsing and does not verify ownership or game-version compatibility.

Open a release to read its description and previews, then choose **Install**. Moader checks the published package size and SHA-256 before importing it. The new mod appears disabled in your Library; choose the correct game copy and enable it there. If you remove a Store mod from the Library, you can install that release again. You can still import a publisher ZIP with **Upload mod**.

To publish your own mods, copy the repository template. It builds a static website, a game catalog at `catalog.json` and downloadable ZIPs with their sizes and SHA-256 checksums. The template includes instructions for GitHub Pages and other static hosts. [Moader mod repository template](https://github.com/H644b/moader-mod-repo-template)
