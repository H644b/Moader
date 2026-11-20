# Community repositories and the modding page

A creator publishes a mod once on their own website. Players can find it by opening a shared link or by browsing that game's page in Moader. Both views read the same catalog, so the author, screenshots and release version come from the publisher.

Anyone can browse a publisher's public website today. The Store button on the console and in the paired browser is gray and disabled. Players can already download a publisher's ZIP on a phone or computer, then use **Upload mod** in the browser paired with their Switch. [Working example for the fictional game Orchard Field Notes](https://h644b.github.io/moader-mod-repo-template/#mod/010000000000F00D/orchard-palette) · [Current import instructions](../USAGE.md#upload-your-first-mod)

## 1. How a creator publishes

1. Copy the publisher template. In `repository.json`, name the workshop and list the games it supports. Use the exact 16-character Title ID shown in Moader for each edition.
2. Give each mod a stable ID. In `mods/<id>/mod.json`, enter the title, author, category, description, version, publication date and license. Add screenshots to `previews/` and the installable files to `payload/`. The description can name the game versions the author tested.
3. Publish to GitHub Pages or another HTTPS host. The template generates a storefront, a `moader.catalog.v1` feed at `catalog.json`, and a versioned ZIP with its byte size and SHA-256 checksum. The release page has a shareable URL and can link to additional hosts. [Publisher template](https://github.com/H644b/moader-mod-repo-template) · [Example catalog](https://h644b.github.io/moader-mod-repo-template/catalog.json)
4. To release an update, keep the Title ID and mod ID, increment the version, update the date and republish. The mod keeps its shareable page and the new ZIP gets its own filename.

## 2. How a repository gets discovered

- **Subscriptions.** The existing Store reader accepts one HTTPS catalog URL per line in `sdmc:/moader/store/sources.txt`. Once the Store opens, it can group releases from subscribed feeds by game Title ID. A failed feed leaves the other hosts available. [Store reader](../../native/store/README.md)
- **Proposed public directory.** A publisher submits their workshop name, site and feed URL by pull request. A maintainer checks that the feed opens and its release links resolve. Players can choose a listed workshop or paste another feed into a proposed **Store sources** setting. Publishers can share their sites as soon as they deploy them.
- **Ownership.** The directory points to publishers. Each publisher serves its own images and packages and can correct or withdraw a listing. Removing a source from discovery leaves installed copies under the player's control.

## 3. The proposed browsing flow

| View | What it shows | Player action |
| --- | --- | --- |
| Game library | The selected game's cover and **Installed mods** | Open the Store with that game selected. A game picker also lets someone browse for their first mod. |
| Per-game modding page | **Discover**, categories supplied by authors, and **Hosts**. Each card shows a creator preview, mod name, author and host. | Open a card or filter within that edition. If it has no listings, the page names the edition and offers **Change game** or **Add source**. |
| Mod detail | A screenshot gallery, description, author, version, date and source. The publisher's page also shows the license, ZIP size and checksum. | In the first console version, choose **Open on [host]**. On the public page, choose **Download ZIP**. Back returns to the same game, filter and card. |

## 4. How installation and updates should work

- **First release.** Enable browsing after console verification. A player opens the publisher's page, downloads the ZIP on their phone or computer, and imports it for the matching Title ID with **Upload mod**. It appears under **Installed mods**, disabled until the player enables it.
- **Proposed direct install.** Show the destination game and edition, mod version, download size and publishing host before confirmation. Download to temporary storage, verify the advertised size and SHA-256, then use Moader's existing ZIP validation and import rules. Success returns the player to the installed entry. A failed download or checksum leaves the library intact.
- **Proposed updates.** Record the source feed, Title ID, mod ID and installed version. When a feed refresh finds an update from that publisher, show both versions and let the player choose **Update**. If two workshops use the same mod ID, give them distinct local entries. Keep the existing copy until its replacement succeeds.
