# Game-card checks

## Choose the game's copy

Select a game, then open **Settings → General → Game copy** in the app or browser. Moader saves the choice per game.

| Choice | What Moader checks |
| --- | --- |
| **Check inserted card** (default) | The inserted card matches the game. |
| **Cartridge or game-key card** | The inserted card matches the game, including when the game data was downloaded. |
| **My digital copy** | The base game is installed on internal storage or SD. Use this for a copy you can play without the card. |

The digital setting checks installed game data. Ownership and licenses are handled by the console. A game-key card still needs to be inserted after download. [Nintendo game-key card guide](https://en-americas-support.nintendo.com/app/answers/detail/a_id/68415/p/172)

## When the card is absent

**Game unavailable** means Moader couldn't verify the selected copy. Open the mod's details for the reason. Your enable setting stays saved. Moader leaves that game's mods out of new routing until the check passes.

The resident checks again in the background, normally after about a second plus processing time. Insert the correct card or correct the copy setting, then check the apply state. If withdrawal of the old routing is pending or failed, the detail will say so. A game that already loaded an asset or executable change may still need to reload it.

## What the implementation checks

For physical copies, Moader resolves the routed title to its verified base owner and checks two consistent observations of the inserted card and its application list. This path uses NS card queries. Installed content and NCM lookups are required only on the digital path.

For digital copies, Moader checks complete NS content observations and verifies the base owner's Program content at index zero on built-in or SD storage. It repeats the content observation. Game Edition metadata must identify a verified base owner. An ACD index is separate from the executable Program index.

An incomplete query blocks new routing. A failed SD scan also withholds requested replacements until a complete scan succeeds. An older mount or file object may still use earlier routing. These checks cover Moader's managed library. The console continues to enforce launch, update and download requirements.

[Cartridge test results](VALIDATION.md#cartridge-correction-and-plugin-reload)
