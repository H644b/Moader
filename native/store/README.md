# Community Store

The v3.0.0 test NRO opens the native Store by touch or from the controller navigation bar. Each visit fetches the [publisher catalog](https://h644b.github.io/moader-mod-repo-template/catalog.json) over HTTPS. Players can subscribe to additional feeds by putting one HTTPS catalog URL per line in `sdmc:/moader/store/sources.txt`.

Listings, packaging and previews belong to the publisher. The native Store fetches previews on demand and opens the selected publisher page. Reopening the Store refreshes the feeds and retries a failed connection. The catalog client sends requests only while the Store is open.

The files in this source directory are design fixtures for host-side renderer captures. The console target builds independently of them. Publishers can use the [community repository template](https://github.com/H644b/moader-mod-repo-template) to generate a catalog, previews and ZIP downloads.
