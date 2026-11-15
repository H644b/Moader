# Credits and third-party code

Moader is independent homebrew software for kaiHEN. It is not affiliated with Nintendo. Game names in examples identify the software being discussed; game files, firmware dumps, keys and commercial mods are not distributed here.

Moader's original source is licensed under **GPL-2.0-only**. Upstream files keep their own notices, licenses and exceptions. The top-level license does not replace those notices.

[How kaiHEN is modified](docs/KAIHEN_CHANGES.md) explains the four changed files, their behavior, the exact upstream base and the published HEN binary hash.

| Component | Version or revision | Location and license |
| --- | --- | --- |
| [kaiHEN](https://github.com/ouncebrew/kaihen) | `e897f991524590833b82fbe8d63a3c6775fb8101` | Source in [`runtime/kaihen`](runtime/kaihen/). GPL-2.0-only with the upstream linking exception and separate permissive component notices. Moader's patch is [`software-builds/kaihen-mo.patch`](software-builds/kaihen-mo.patch). |
| [kaiHEN release](https://github.com/ouncebrew/kaihen/releases/tag/20261114c) | `20261114c` | Setup downloads the qualified RO, bootstrap, game loader and hbmenu from the official archive. Bundled component notices are retained in [`native/third_party/licenses`](native/third_party/licenses/) and [`runtime/kaihen/release/licenses`](runtime/kaihen/release/licenses/). Archive SHA-256: `63b91e5e7c6f411aad758758e5a1a3cbe1f855b4304d87916afe5319b9dfd967`. |
| [ns2jb-modloader](https://github.com/SebaaMG/ns2jb-modloader) | `63d46b27ea33a9bfeb43b497b8fb89498cf6c148` | Forked source in [`runtime/modloader`](runtime/modloader/), under its retained GPL-2.0 terms and bundled-component notices. Moader adds dynamic catalogues, lifetime ownership, receipts and closed debug snapshots. |
| [ns2jb](https://gitlab.com/kurumuz/ns2jb) | `28f03b5895e5569d8fbc35cbed84906b8f0a65b2` | External host dependency fetched from its upstream repository by setup. It is **not vendored in this repository or redistributed in the release kit**. No license file was found at this pinned revision; no redistribution license is inferred. |
| [civ7-fs-kaihen](https://github.com/s8n/civ7-fs-kaihen/tree/51a804cbce09216e1a42e65929644fd436b4c8bf) | `51a804cbce09216e1a42e65929644fd436b4c8bf` | Experimental `civ7-s8n` external host source, downloaded locally by setup using [`config/civ7-provider.json`](config/civ7-provider.json). Upstream sources are **not redistributed** in the repository or kit. No whole-tree license was identified at this revision; no redistribution permission is inferred. |
| [ounce-devkit](https://github.com/ouncebrew/ounce-devkit/releases/tag/devkit-20261114) | `devkit-20261114` | Supplies libounce, libkaihen and OunceVK for native builds. Individual library notices remain in the downloaded SDK/release. Archive SHA-256: `f24d5773b1ee1c2b19317f57e8ae54a60fcdcefe493a0e495297c241c4912f59`. |
| [devkitPro](https://devkitpro.org/) | Pinned packages | Compiler, tools and zlib used by the native build. Exact URLs and checksums are in [`tools/toolchain-packages.json`](tools/toolchain-packages.json). SDK packages are downloaded locally, not committed. |
| [stb_truetype](https://github.com/nothings/stb/blob/9f755b604db3492df2cbeb39bf84b81e3e3932e3/stb_truetype.h) | `9f755b604db3492df2cbeb39bf84b81e3e3932e3` | [`native/third_party/stb_truetype.h`](native/third_party/stb_truetype.h), MIT/public-domain dual license retained in the file. Used with the bundled trusted font. |
| [Noto Sans](https://github.com/notofonts/latin-greek-cyrillic) | Bundled Noto Sans Regular | [`native/assets/NotoSans-Regular.ttf`](native/assets/NotoSans-Regular.ttf), copyright 2022 The Noto Project Authors. The [SIL Open Font License](native/assets/NotoSans-OFL.txt) accompanies it. |
| [Atmosphère Zstandard fork](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libstratosphere/source/util/zstd.inc) | `6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8` | Source bundled by the modloader in `runtime/modloader/native/vendor/zstd/`, with its retained license. |
| [IZ2S / OunceCake](https://github.com/byizmysh/IZ2S-Platform) | Upstream modloader adaptation | Game-mount helpers in `runtime/modloader/lib/game-files/`; their [COPYING](runtime/modloader/lib/game-files/COPYING) notice is retained. |

The CivSwitch entrypoint research also credits [Gezine/CivSwitch](https://github.com/Gezine/CivSwitch/tree/f6b0f2bd285e7b32c0a1968181046f18edd79708), [HerXayah/civ7-fs](https://github.com/HerXayah/civ7-fs/tree/5e5665b92661f205d853ea4351a9edace7c9c6b5) and [cpypwn/mac2jb](https://github.com/cpypwn/mac2jb/tree/717022b8a91a713be41a02b5e131d3229dd8b00a). They are distinct projects/source profiles; credit does not imply that every session is admitted or Moader hardware-tested. See [the Civ7 guide](docs/CIV7.md) for the reviewed dates, requirements and support boundaries. Civ7 thread/bootstrap adaptations are separate from the disclosed four-file Moader HEN patch.

ZIP decoding uses devkitPro zlib on the Switch and system zlib in host tests. The Moader mark is original project artwork, shared by the app icon, favicon and both interface headers.

See [validation](docs/VALIDATION.md) for what was tested and [building](docs/BUILDING.md) for the source and tools needed to reproduce a build.
