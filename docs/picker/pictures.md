# Runtime picker pictures

`GameCatalogEntry::pictureSource` is a creator project page or repository, never a bundled picture. Native calls `MainActivity.prepareGamePictures(id, creator, source)` when displaying a page. It schedules work off the UI/game thread. A previously cached picture remains usable offline; otherwise the picker keeps the styled title and existing Fallout 2 title art. Credits remain visible on every page.

Installer integration (after the game's files have been successfully committed):

```java
GamePictures.afterInstall(context, gameId, creator, pictureSource);
```

Call this with the catalog's descriptor, not an archive URL or a user-supplied URL. The installer lane owns that call. The picker also recognizes existing game data (`master.dat`, `mods/`, `data/`) when revisiting a page, so an older install can acquire its artwork. Fallout 2 artwork is stored in `games/fallout2/picker/` as required even though its game files remain at the top root.

- Preview: `<top>/picker-preview/<id>/art.rgba`, 512 KiB response limit and 480-pixel decoded edge.
- Installed: `<top>/games/<id>/picker/art.rgba`, 8 MiB response limit and 1920-pixel decoded edge; `credit.txt` includes creator, project source and resolved image URL.
- Page/README metadata: 1 MiB limit. Non-GitHub pages: OG image. GitHub sources: README API + README images only; GitHub's generated social cards (opengraph.githubassets.com, avatars) are never used as art. Without art the picker shows the plain themed panel. No external search or mirror is used.
- Only unmetered Wi-Fi/Ethernet automatically fetches. Offline/cellular visits retain the existing cache; later visits retry. Failures back off for a minute. Each request has a 25-second overall network budget, 6-second connection/read timeout and at most four redirects. HTTPS is required throughout.
- Decode PNG/JPEG with Android BitmapFactory, rejecting huge dimensions before allocating pixels. PRAW cache format: four bytes `PRAW`, big-endian unsigned 32-bit width/height, straight RGBA bytes. Native rejects malformed dimensions, missing/truncated/extra bytes and files exceeding the maximum size, then tries preview/original art.
- Writes publish with atomic renames in the destination directory. Credit is committed before artwork. Partial files are ignored; failed downloads do not replace an existing picture.

No build step fetches pictures. Test fixtures are metadata snippets; the tiny native image is generated at test runtime. Live sources can change, exceed the cap, return SVG/WebP, block automated requests, or lack useful OG metadata; all leave the title fallback available. Android decoding and actual creator-site responses still need device/network validation.

## Installed games: art from the game's own files

`GameArt` (JVM, no network) builds `<game>/picker/art.rgba` + `credit.txt` for installed games (not Fallout 2) from the game's own splash screens (`art/splash/splash*.rix`, RIX3 paletted; largest wins) or else `art/intrface/mainmenu.frm` + `color.pal`, read from loose `data/` files and the game's DAT2 archives (own `master.dat`/patches; Resurrection falls back to the player's Fallout 2 `master.dat`). `credit.txt` carries `Image: game-file:<entry>@<dat>`. It runs from `GamePictures.request` when `art.rgba` is missing or was fetched online earlier. Online README art is only for NOT-installed previews and is ranked: no badges/shields/icons/logos/social cards, width >= 640 and aspect 1.2-2.5, largest wins. Fallout 1 DAT1 (Et Tu base) is not read; Et Tu shows the plain panel unless it ships loose art.
