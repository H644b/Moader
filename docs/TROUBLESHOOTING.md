# Troubleshooting

Press **ZL** in Moader, or open **Settings → Advanced → Current session** in the paired browser. Keep the exact error message.

## Browser or app connection

- **Moader is missing from hbmenu.** Check that the file is at `switch/Moader.nro` on the SD card. Extract the console ZIP before copying. [Copy the console app](GETTING_STARTED.md#copy-or-update-the-console-app)
- **The page won't load.** Press **ZR** for **Connection**. If the web listener is off, select **Start listener**. Enter the displayed address on a device on the same local network. If you changed the port, restart the listener first. [Browser pairing](USAGE.md#connect-a-browser)
- **Pairing fails.** Enter all six digits, including leading zeros. After repeated failures, wait 30 seconds before trying again.
- **The page stops when I close Moader.** The app may be hosting the listener in foreground mode. To keep the page available during play, [set up the backend for this boot](GETTING_STARTED.md#start-the-backend-for-this-boot).
- **The app lost the service during a reload.** Keep the app open until it reconnects or shows an error. [Read the reload status and recovery steps](PLUGIN_RELOAD.md#errors-and-recovery).

## Library and mods

- **A working game shows the cartridge logo.** The carousel retries a missing icon after one minute while the game is in view. If the placeholder persists, press **ZL → Save report**. The `Game icon` line in `sdmc:/moader/app-diagnostics.txt` identifies which lookup or image decode failed. If the issue occurs only in a resident-hosted browser, check that the resident plugin matches the app build.
- **The list is stale or unavailable.** Check SD access and select **Refresh**. Moader keeps the last readable list during imports and scan failures. Changes pause after a failed scan until a complete scan succeeds. Keep your library folders and preferences in place.
- **A game shows up by Title ID.** Moader uses the ID when a game name is unavailable. Check the ID before uploading a mod or changing its settings.
- **Game unavailable.** Select the title and check **Settings → Game copy**. Insert the matching physical or game-key card, or choose **My digital copy** for a card-independent copy. Read the mod's detail for a failed availability check or an unconfirmed routing change. [Game-copy checks](CARTRIDGES.md)
- **Pending.** Moader saved the request and is waiting for the filesystem runtime. Check the runtime connection under **Settings → Advanced** and read the debug log. If you rebooted, the backend needs setup for the new boot.
- **The game looks unchanged.** Check the Title ID, game version and paths. Follow the [apply state](USAGE.md#enable-disable-and-remove). Cached assets may need a scene or content reload. Close and relaunch for executable changes.
- **Files remain on SD after removal.** Moader retains the payload for open game files. Leave it in place during the active boot.
## A mod caused a software error

If the app or browser still connects, disable the suspected mod and relaunch the game. If the backend is unavailable, save the error before deciding how to recover.

## My ZIP is rejected

Compare the archive with the [supported layout and size limits](USAGE.md#upload-your-first-mod). Put the payload at the top level or inside one enclosing folder. Leave out screenshots and other extras. Use binary IPS files for executable patches. If two enabled mods replace the same path, disable one and read the conflict details.

Browser imports use the running service's ZIP reader. If a supported ZIP still fails after updating the app, check that the running service matches its release. [Update Moader](GETTING_STARTED.md#updating)

## The setup helper rejects my input

Check that the backend kit matches your app release and that your computer meets the requirements for the method you chose. If setup still stops, keep the exact message and the log from that attempt. [Compare setup methods](GETTING_STARTED.md#start-the-backend-for-this-boot) · [Report a problem](#reporting-a-problem)

## Backend setup failed or timed out

Stop and keep the installation report in the run folder. A timed-out operation may already have taken effect. Leave the old session, journal and proof files in place. Avoid running the same command, starting a second install or issuing manual cleanup.

If the report mentions Loader restoration, keep that journal too. If the Switch 2 is frozen, Nintendo's first power-off step is to hold the physical **POWER** button for at least **20 seconds**. Power on normally after it shuts down. A new boot needs a fresh verified session. [Nintendo Switch 2 freeze support](https://en-americas-support.nintendo.com/app/answers/detail/a_id/68228/)

## Reporting a problem

[Open an issue](https://github.com/H644b/Moader/issues) with your Moader version, setup method, firmware if known, and the steps you took. Include the exact error or apply state, what you expected in the game, and what happened after reopening content or relaunching.

Share only the relevant lines or a cropped screenshot. Remove pairing codes, private addresses, personal paths, account details and session commands. Keep game files and saves out of the report.
