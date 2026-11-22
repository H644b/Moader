# Moader Setup in a backend kit

The [v4.0.0 backend kit](https://github.com/H644b/Moader/releases/download/v4.0.0/Moader-backend-v4.0.0.zip) includes **Moader Setup** and its pinned desktop runtimes. [Installation](INSTALLATION.md) also documents the command-line routes.

Extract the whole backend ZIP and open the outer `Moader-backend-...` folder. On macOS, open **Moader Setup.app**. On Linux, open **Moader Setup** in the file manager and choose Run if asked. On Windows, open **Moader Setup Windows/Moader Setup.exe** and choose a WSL2 Linux distribution in its picker. Keep the complete extracted kit together. Moader Setup opens its own desktop window. Its private local controller performs the computer work; the separate **Open Moader in browser** task opens the page hosted by the Switch.

The kit includes the matching app, resident plugin, HEN, RO, bootstrap, a Node 24 controller runtime and a pinned Python preparation helper. You do not need the separate app ZIP for a complete backend installation. The first host preparation fetches reviewed provider sources and, if needed, a Python distribution into the kit's state folder. Git is needed to fetch those sources. Seedsow preparation also needs clang and, on Linux, zstd. If an OS tool is missing, Setup names it. On macOS it opens the Command Line Tools installer. On Linux it can use a graphical software manager or, after an explicit click, the distribution's signed package manager with graphical administrator approval. In WSL2 it can install the named tools into the selected distribution through its signed package manager. A distribution without one of the supported package managers needs a supported desktop Linux or WSL2 environment before preparation can proceed. Nothing is installed globally by the Python helper.

## Choose a task

- **Set up for this boot**: choose PC-driven Seedsow, mac2jb, direct ns2jb, or experimental CivSwitch. Prepare the host, follow the Switch steps, then press the one installation button after the current session is qualified. A full backend installer copies the app and plugin too. A launcher reply or a prepared snapshot alone does not mean FS or in-game mod loading was verified.
- **Prepare later computer-free boots**: the Seedsow A/B route needs one exact qualified sparse FS capture and a completed PC-driven run receipt. Setup can find one in the same kit or import five named evidence files from an earlier completed kit. The public ZIP contains no capture. A newly created capture can still have an unsupported fingerprint. If so, use PC-driven setup. The GUI stages the checked A/B SD files, then uploads modules only after you confirm the active profile and a separate save backup.
- **Copy app and plugin files**: choose the mounted SD root in the app, review each destination, back up existing files on the card and read back both backups and writes. This does not update a running service or install the backend for a new boot. Do not choose a folder inside the SD card as the root.
- **Update a running service**: follow the existing checked reload action in the console app after copying a compatible plugin. Setup does not assume a remote reload endpoint.
- **Check connection**, **Open Moader in browser**, and **Export support report** remain available from the left task list or compact task selector.

The controller continues if the app window closes. Reopen the same launcher to reconnect. It keeps original journals. If a native action stops or the controller restarts without a verified result, Setup records the outcome as uncertain and blocks another native attempt on that boot. Keep the original reports, inspect the device state, and fully reboot before confirming a new boot record. Closing the window does not cancel a console operation.

The support export offers a short text summary and advanced JSON. It includes release identities, method and provider revisions, the first error, recent events and small relevant receipts. It excludes save data and binary captures. Address, credential and personal-path redaction is best effort; review the file before sharing it.

## Platform and release limits

Seedsow PC setup targets Apple Silicon macOS and Linux x86_64, including WSL2 x86_64. mac2jb is a macOS route. Direct ns2jb uses native macOS or Linux Node. CivSwitch remains experimental and its host-tested workflow still needs console acceptance. Windows uses a chosen WSL2 distribution; it is not a native Windows backend. A WSL distro must have working network access and a reachable address for the chosen provider. The graphical WSL picker and Linux arm64 runtime are packaged, but have no live Windows/Linux acceptance from this development host.

The macOS launcher in the v4.0.0 kit is unsigned and unnotarized. Distribution to a quarantined macOS download requires the project's normal signing and notarization release process. Setup does not ask you to disable platform security. CLI guides remain in [Installation](INSTALLATION.md) for advanced users.

## Verification boundary

Host tests and an extracted-kit launch can verify controller behavior and packaging without a Switch. They cannot establish FS acquisition, installation on console, service reload, save upload, or an in-game mod change. The current evidence and remaining hardware checks are listed in [Validation](VALIDATION.md).
