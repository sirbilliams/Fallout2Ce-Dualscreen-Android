package com.alexbatalov.fallout2ce;

import java.io.*;
import java.nio.channels.FileLock;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;
import java.util.zip.*;

// Pure installer logic. Paths come from GameRoots; top is ALWAYS getExternalFilesDir(null), never active cwd.
final class GameInstall {
    static boolean supported(String id) {
        return "ettu".equals(id) || "sonora".equals(id) || "nevada".equals(id) || "resurrection".equals(id) || "yesterday".equals(id);
    }

    // Games whose own release ships master.dat and critter.dat (kept in the game
    // folder; mirrors GameCatalogEntry.ownDats in src/game_catalog.cc). Et Tu uses
    // the player's Fallout 1 files and Resurrection (fores*.dat) the player's
    // Fallout 2 files, so an archive's dats never replace those.
    static boolean ownDats(String id) {
        return "sonora".equals(id) || "nevada".equals(id);
    }

    // Marker present but the game's own dats are missing (installed by a build
    // that dropped them): needs a repair, not "Installed".
    static boolean needsRepair(File top, String id) {
        if (!ownDats(id)) return false;
        try {
            File target = target(top, id);
            return new File(target, ".installed").isFile() && !hasOwnDats(target);
        } catch (IOException e) { return false; }
    }

    private static boolean hasOwnDats(File target) {
        return new File(target, "master.dat").isFile() && new File(target, "critter.dat").isFile();
    }

    // Game installer archives and status files live here whichever game is active.
    static File downloadsFolder(File top) {
        return new File(top, "mod-downloads");
    }

    private static boolean validZip(File file) {
        try (ZipFile zip = new ZipFile(file)) {
            return zip.entries().hasMoreElements();
        } catch (IOException | RuntimeException e) { return false; }
    }

    // A complete archive of this game already in [folder] (so Repair need not
    // download again): the expected asset name with exactly the expected size
    // when those are known, otherwise any game-<id>-*.zip that opens as a zip.
    // Newest first; null when none.
    static File reusableArchive(File folder, String id, String assetName, long assetSize) {
        File[] files = folder == null ? null : folder.listFiles();
        if (files == null) return null;
        Arrays.sort(files, (a, b) -> Long.compare(b.lastModified(), a.lastModified()));
        String prefix = "game-" + id + "-";
        for (File f : files) {
            String n = f.getName();
            if (!f.isFile() || !n.startsWith(prefix) || !n.toLowerCase(Locale.ROOT).endsWith(".zip")) continue;
            if (assetName != null && assetSize > 0) {
                if (n.equals(prefix + assetName) && f.length() == assetSize) return f;
                continue;
            }
            if (validZip(f)) return f;
        }
        return null;
    }

    // Makes [archive] a finished, not yet installed download of this game in the folder's index.
    static void adoptArchive(File folder, String id, File archive) throws IOException {
        final String file = archive.getName();
        DownloadIndex.update(folder, items -> {
            DownloadIndex.Item item = DownloadIndex.find(items, file);
            if (item == null) {
                item = new DownloadIndex.Item(-1, file, id + " game installer", "game:" + id, DownloadIndex.DONE, false);
                items.add(item);
            }
            item.id = -1;
            item.source = "game:" + id;
            item.status = DownloadIndex.DONE;
            item.installed = false;
        });
    }

    static String repo(String id) throws IOException {
        if ("ettu".equals(id)) return "rotators/Fo1in2";
        if ("sonora".equals(id)) return "NovaRain/Fallout-Sonora-English";
        if ("nevada".equals(id)) return "HogLord69/fallout-english-localization";
        throw new IOException("No GitHub release for this game");
    }

    static final long NEVADA_SIZE = 1029036403L;
    static final String NEVADA_SHA256 = "3ebbd3c292be210e157a18e33a59b7fe5dd95f079fd71f4493774274bc155a60";
    static final String NEVADA_URL = "https://github.com/HogLord69/fallout-english-localization/releases/download/v1.0/Fallout-Nevada-English.zip";

    static String releaseApi(String id) throws IOException {
        return "https://api.github.com/repos/" + repo(id)
            + ("nevada".equals(id) ? "/releases/tags/v1.0" : "/releases/latest");
    }

    // Check the pinned repack before opening it, including reused downloads.
    static void verifyArchive(String id, File archive) throws IOException {
        if (!"nevada".equals(id)) return;
        if (archive.length() != NEVADA_SIZE) throw new IOException("Nevada archive size mismatch");
        verifySha256(archive, NEVADA_SHA256);
    }

    static void verifySha256(File archive, String expected) throws IOException {
        try {
            java.security.MessageDigest digest = java.security.MessageDigest.getInstance("SHA-256");
            try (InputStream in = new FileInputStream(archive)) {
                byte[] buffer = new byte[65536];
                int n;
                while ((n = in.read(buffer)) != -1) {
                    if (Thread.currentThread().isInterrupted()) throw new IOException("Install interrupted");
                    digest.update(buffer, 0, n);
                }
            }
            StringBuilder hex = new StringBuilder();
            for (byte b : digest.digest()) hex.append(String.format(Locale.ROOT, "%02x", b & 255));
            if (!hex.toString().equals(expected)) throw new IOException("Archive SHA-256 mismatch");
        } catch (java.security.NoSuchAlgorithmException e) { throw new IOException(e); }
    }

    static RpuRelease.Asset asset(String id, String json) throws IOException {
        try {
            Object parsed = new RpuRelease.Json(json).value();
            if (!(parsed instanceof Map)) throw new IOException("Invalid release");
            Map<?, ?> release = (Map<?, ?>) parsed;
            if (Boolean.TRUE.equals(release.get("draft")) || Boolean.TRUE.equals(release.get("prerelease")))
                throw new IOException("No stable release");
            Object assets = release.get("assets");
            if (!(assets instanceof List)) throw new IOException("Missing assets");
            RpuRelease.Asset best = null;
            for (Object item : (List<?>) assets) {
                if (!(item instanceof Map)) continue;
                Map<?, ?> a = (Map<?, ?>) item;
                Object name = a.get("name"), size = a.get("size"), url = a.get("browser_download_url");
                if (!(name instanceof String) || !(size instanceof Double) || !(url instanceof String)) continue;
                String n = ((String) name).toLowerCase(Locale.ROOT);
                boolean match = "ettu".equals(id) ? n.equals("fallout1in2.zip")
                    : "nevada".equals(id) ? n.equals("fallout-nevada-english.zip")
                    : "sonora".equals(id) && n.matches("fallout_sonora_[0-9].*_sfall\\.zip");
                long bytes = ((Double) size).longValue();
                if (!match || bytes <= 0 || !((String) url).startsWith("https://github.com/" + repo(id) + "/releases/download/")) continue;
                if ("nevada".equals(id) && (bytes != NEVADA_SIZE || !NEVADA_URL.equals(url)))
                    throw new IOException("Nevada release differs from the verified repack");
                if (best != null) throw new IOException("Ambiguous release assets");
                best = new RpuRelease.Asset((String) name, bytes, (String) url);
            }
            if (best == null) throw new IOException("Official release ZIP not found");
            return best;
        } catch (RuntimeException e) {
            throw new IOException("Invalid release JSON", e);
        }
    }

    static long needed(long archive, long extracted) throws IOException {
        if (archive <= 0 || extracted < 0 || archive > Long.MAX_VALUE - extracted - 67108864L)
            throw new IOException("Invalid archive size");
        return archive + extracted + 67108864L;
    }

    static boolean enoughSpace(long free, long archive, long extracted) throws IOException {
        return free >= needed(archive, extracted);
    }

    // Before downloading the central directory is unavailable. Reserve a generous
    // estimate; extraction checks the exact central-directory size again.
    static long preflight(long archive) throws IOException {
        if (archive <= 0 || archive > Long.MAX_VALUE / 5) throw new IOException("Invalid archive size");
        return needed(archive, archive * 4);
    }

    static long preflight(String id, long archive) throws IOException {
        if ("nevada".equals(id)) {
            if (archive != NEVADA_SIZE) throw new IOException("Unexpected Nevada archive size");
            return 2500000000L; // 1.03 GB ZIP + 1.40 GB data + staging margin.
        }
        return preflight(archive);
    }

    static File target(File top, String id) throws IOException {
        if (top == null || !supported(id)) throw new IOException("Invalid game root");
        File target = GameRoots.gameRoot(top.getCanonicalFile(), id);
        File games = target.getParentFile();
        if (!games.getCanonicalFile().equals(games) || !target.getCanonicalFile().equals(target))
            throw new IOException("Game roots must not be symlinks");
        return target;
    }

    static boolean installed(File top, String id) {
        try {
            File target = target(top, id);
            return new File(target, ".installed").isFile() && (!ownDats(id) || hasOwnDats(target));
        } catch (IOException e) { return false; }
    }

    static void requireBase(File top, String id) throws IOException {
        if (top == null) throw new IOException("Game storage unavailable");
        if ("yesterday".equals(id)) {
            for (String name : new String[] { "master.dat", "critter.dat", "patch000.dat" })
                if (!new File(GameRoots.baseRoot(top, "fallout2"), name).isFile())
                    throw new IOException("Yesterday needs your Fallout 2 " + name + " in the shared base.");
        }
        if ("ettu".equals(id) && !GameRoots.baseRoot(top, "fallout1").isDirectory())
            throw new IOException("Et Tu needs your Fallout 1 files. Choose Add Fallout 1 files first.");
    }

    static String mapped(String id, String entry) throws IOException {
        if (!supported(id)) throw new IOException("Unknown game");
        String path = entry.replace('\\', '/').toLowerCase(Locale.ROOT);
        if (path.startsWith("/") || path.contains(":")) throw new IOException("Unsafe archive path");
        for (String part : path.split("/"))
            if (part.equals("..") || part.equals(".")) throw new IOException("Unsafe archive path");
        if ("ettu".equals(id)) {
            if (!path.startsWith("fallout1in2/")) return null;
            path = path.substring("fallout1in2/".length());
        }
        if ("sonora".equals(id) && path.matches("fallout_sonora_[a-z0-9_]+/.*"))
            path = path.substring(path.indexOf('/') + 1);
        if ("nevada".equals(id)) {
            if (!path.startsWith("fallout-nevada-english/")) return null;
            path = path.substring("fallout-nevada-english/".length());
            // Only runtime data; documentation, screenshots and Windows tools stay out.
            if (!(path.startsWith("data/") || path.startsWith("mods/")
                || path.startsWith("appearance/") || path.startsWith("sfall/")
                || path.matches("(master.dat|critter.dat|f2_res.dat|sfall.dat|f2_res.ini|ddraw.ini|config.cfg)"))) return null;
        }
        if ("yesterday".equals(id)) {
            if (!path.startsWith(YesterdayInstall.PREFIX)) throw new IOException("Expected fallouty-main archive folder");
            path = path.substring(YesterdayInstall.PREFIX.length());
            if (path.equals("sfall.dat")) return "mods/sfall.dat";
            if (path.startsWith("_tools/") || path.endsWith(".bat") || path.equals("patch000.dat")) return null;
        }
        String leaf = path.substring(path.lastIndexOf('/') + 1);
        boolean dat = leaf.equals("master.dat") || leaf.equals("critter.dat");
        if ((dat && !ownDats(id)) || leaf.equals(".installed")
            || leaf.endsWith(".exe") || leaf.endsWith(".dll")
            || ("nevada".equals(id) && (leaf.endsWith(".bat") || leaf.endsWith(".ico")))) return null;
        return path;
    }

    interface Progress { void update(long done, long total) throws IOException; }

    static void extract(File top, String id, File archive, Progress progress) throws IOException {
        requireBase(top, id);
        File dest = target(top, id), games = dest.getParentFile();
        if (!games.isDirectory() && !games.mkdirs()) throw new IOException("Cannot create games folder");
        try (RandomAccessFile lockFile = new RandomAccessFile(new File(games, ".install-" + id + ".lock"), "rw");
             FileLock lock = lockFile.getChannel().lock(); ZipFile zip = new ZipFile(archive)) {
            if (installed(top, id)) return; // process restart after atomic publication
            boolean repair = needsRepair(top, id);
            if (dest.exists() && !repair) throw new IOException("Game folder already exists; leaving it untouched");
            long total = 0;
            Set<String> paths = new HashSet<>();
            Enumeration<? extends ZipEntry> entries = zip.entries();
            while (entries.hasMoreElements()) {
                ZipEntry e = entries.nextElement();
                String name = mapped(id, e.getName());
                if (name == null || name.isEmpty() || e.isDirectory()) continue;
                if (!paths.add(name) || e.getSize() < 0 || e.getSize() > Long.MAX_VALUE - total)
                    throw new IOException("Invalid ZIP entries");
                total += e.getSize();
            }
            if ("yesterday".equals(id)) YesterdayInstall.validate(paths);
            if (paths.stream().noneMatch(p -> p.endsWith(".dat") || p.startsWith("data/")))
                throw new IOException("ZIP contains no game data");
            if (total > Long.MAX_VALUE - 67108864L || games.getUsableSpace() < total + 67108864L)
                throw new IOException("Not enough free space to extract");
            File temp = stagingDir(games, id);
            try {
                long done = 0;
                StringBuilder installLog = new StringBuilder();
                byte[] buffer = new byte[65536];
                entries = zip.entries();
                while (entries.hasMoreElements()) {
                    ZipEntry e = entries.nextElement();
                    String name = mapped(id, e.getName());
                    if (name == null || name.isEmpty() || e.isDirectory()) continue;
                    File out = new File(temp, name);
                    if (!out.getCanonicalPath().startsWith(temp.getCanonicalPath() + File.separator))
                        throw new IOException("Unsafe ZIP entry");
                    File parent = out.getParentFile();
                    if (!parent.isDirectory() && !parent.mkdirs()) throw new IOException("Cannot create folder");
                    CRC32 crc = new CRC32();
                    long count = 0;
                    try (InputStream in = zip.getInputStream(e); OutputStream stream = new FileOutputStream(out)) {
                        int n;
                        while ((n = in.read(buffer)) != -1) {
                            if (Thread.currentThread().isInterrupted()) throw new IOException("Install interrupted; retry from picker");
                            count += n;
                            if (count > e.getSize()) throw new IOException("ZIP size mismatch");
                            stream.write(buffer, 0, n);
                            crc.update(buffer, 0, n);
                            done += n;
                            progress.update(done, total);
                        }
                    }
                    if (count != e.getSize() || crc.getValue() != e.getCrc()) throw new IOException("Damaged ZIP");
                    if ("yesterday".equals(id)) YesterdayInstall.skipPointer(out, name, installLog);
                }
                if ("nevada".equals(id)) {
                    if (!hasOwnDats(temp) || !new File(temp, "config.cfg").isFile())
                        throw new IOException("Nevada archive lacks required game files");
                    writeConfig(temp, "config.cfg");
                    nevadaDefaults(temp);
                }
                if ("yesterday".equals(id)) YesterdayInstall.prepare(temp, installLog);
                if (repair) {
                    repairInto(temp, dest);
                    return;
                }
                if (!"nevada".equals(id)) writeConfig(temp, "fallout2.cfg");
                Files.write(new File(temp, ".installed").toPath(), (id + "\n").getBytes(StandardCharsets.UTF_8));
                // Same filesystem; rename publishes the complete tree. No replacement.
                if (dest.exists() || !temp.renameTo(dest)) throw new IOException("Cannot publish game folder");
                shareTree(dest);
            } finally {
                // Only this invocation's private staging directory is removed.
                cleanTemp(temp);
            }
        }
    }

    // Repair: the staged tree is copied over the existing game folder. Only files
    // the player's folder lacks are added, plus the game's own dats (the reason for
    // the repair); saves, mods, fallout2.cfg and edited files stay untouched, and
    // nothing is deleted.
    static void repairInto(File staged, File dest) throws IOException {
        copyMissing(staged, dest, true);
        shareTree(dest);
    }

    private static void copyMissing(File from, File to, boolean top) throws IOException {
        File[] children = from.listFiles();
        if (children == null) return;
        for (File child : children) {
            File out = new File(to, child.getName());
            if (child.isDirectory()) {
                if (!out.isDirectory() && !out.mkdirs()) throw new IOException("Cannot create folder");
                copyMissing(child, out, false);
                continue;
            }
            String lower = child.getName().toLowerCase(Locale.ROOT);
            boolean dat = top && (lower.equals("master.dat") || lower.equals("critter.dat"));
            if (out.exists() && !dat) continue;
            File part = new File(to, child.getName() + ".repair-part");
            Files.copy(child.toPath(), part.toPath(), java.nio.file.StandardCopyOption.REPLACE_EXISTING);
            Files.move(part.toPath(), out.toPath(), java.nio.file.StandardCopyOption.REPLACE_EXISTING);
        }
    }

    // Plain mkdir keeps the default mode (createTempDirectory makes 0700, which
    // file managers and USB cannot read).
    private static File stagingDir(File games, String id) throws IOException {
        for (int i = 0; i < 20; i++) {
            File dir = new File(games, ".install-" + id + "-" + System.nanoTime());
            if (dir.mkdir()) return dir;
        }
        throw new IOException("Cannot create installer staging folder");
    }

    // android.system.Os access, isolated so a missing class (JVM unit tests)
    // only disables this path. Returns {mode, gid} or null when unavailable.
    static int[] osStat(File f) {
        try {
            android.system.StructStat st = android.system.Os.stat(f.getPath());
            return st == null ? null : new int[] { st.st_mode, st.st_gid };
        } catch (Throwable ignored) {
            return null;
        }
    }

    // Adds owner+group rwx to folders and rw to files (no symlinks followed).
    // On Android folders also get setgid and the games folder's group, so files
    // the game creates later inherit it (readable over adb/MTP).
    // Errors are ignored: some filesystems have no POSIX permissions.
    static void shareTree(File dir) {
        File parent = dir.getAbsoluteFile().getParentFile();
        int[] parentStat = parent == null ? null : osStat(parent);
        final int gid = parentStat == null ? -1 : parentStat[1];
        try {
            Files.walkFileTree(dir.toPath(), new java.nio.file.SimpleFileVisitor<java.nio.file.Path>() {
                @Override public java.nio.file.FileVisitResult preVisitDirectory(java.nio.file.Path p, java.nio.file.attribute.BasicFileAttributes a) {
                    share(p, true, gid);
                    return java.nio.file.FileVisitResult.CONTINUE;
                }
                @Override public java.nio.file.FileVisitResult visitFile(java.nio.file.Path p, java.nio.file.attribute.BasicFileAttributes a) {
                    if (a.isRegularFile()) share(p, false, gid);
                    return java.nio.file.FileVisitResult.CONTINUE;
                }
                @Override public java.nio.file.FileVisitResult visitFileFailed(java.nio.file.Path p, IOException e) {
                    return java.nio.file.FileVisitResult.CONTINUE;
                }
            });
        } catch (IOException | RuntimeException ignored) {
        }
    }

    private static boolean shareWithOs(java.nio.file.Path p, boolean dir, int gid) {
        if (osStat(p.toFile()) == null) return false;
        try {
            android.system.Os.chmod(p.toString(), dir ? 02770 : 0660);
        } catch (Throwable ignored) {
        }
        if (gid >= 0) {
            try {
                android.system.Os.chown(p.toString(), -1, gid);
            } catch (Throwable ignored) {
            }
        }
        return true;
    }

    private static void share(java.nio.file.Path p, boolean dir, int gid) {
        if (shareWithOs(p, dir, gid)) return;
        try {
            Set<java.nio.file.attribute.PosixFilePermission> perms =
                new HashSet<>(Files.getPosixFilePermissions(p, java.nio.file.LinkOption.NOFOLLOW_LINKS));
            perms.add(java.nio.file.attribute.PosixFilePermission.OWNER_READ);
            perms.add(java.nio.file.attribute.PosixFilePermission.OWNER_WRITE);
            perms.add(java.nio.file.attribute.PosixFilePermission.GROUP_READ);
            perms.add(java.nio.file.attribute.PosixFilePermission.GROUP_WRITE);
            if (dir) {
                perms.add(java.nio.file.attribute.PosixFilePermission.OWNER_EXECUTE);
                perms.add(java.nio.file.attribute.PosixFilePermission.GROUP_EXECUTE);
            }
            Files.setPosixFilePermissions(p, perms);
        } catch (IOException | RuntimeException ignored) {
        }
    }

    // Android only: true when a game folder or one of its direct children lacks
    // setgid (folders) / has a group other than the games folder's.
    private static boolean needsOsShare(File game, int gamesGid) {
        int[] st = osStat(game);
        if (st == null) return false;
        if ((st[0] & 02000) == 0 || (st[0] & 0070) != 0070 || st[1] != gamesGid) return true;
        File[] children = game.listFiles();
        if (children == null) return false;
        for (File c : children) {
            if (Files.isSymbolicLink(c.toPath())) continue;
            int[] cs = osStat(c);
            if (cs == null) continue;
            if (cs[1] != gamesGid) return true;
            if (c.isDirectory() && (cs[0] & 02000) == 0) return true;
        }
        return false;
    }

    // App start: repairs owner-only game folders of older installs.
    static void shareGames(File top) {
        File gamesDir = new File(top, "games");
        File[] games = gamesDir.listFiles();
        if (games == null) return;
        int[] gamesStat = osStat(gamesDir);
        for (File game : games) {
            if (game.getName().startsWith(".") || !game.isDirectory() || Files.isSymbolicLink(game.toPath())) continue;
            if (gamesStat != null) {
                if (!needsOsShare(game, gamesStat[1])) continue;
            } else {
                try {
                    if (Files.getPosixFilePermissions(game.toPath()).contains(java.nio.file.attribute.PosixFilePermission.GROUP_READ)) continue;
                } catch (IOException | RuntimeException e) {
                    continue;
                }
            }
            shareTree(game);
        }
    }

    // Defaults must be in ddraw.ini BEFORE the first launch: migration to
    // data/game.cfg is one-shot. Explicit shipped values (including -1 timers)
    // are retained. StartMonth/Day use sfall's zero-based convention.
    static void nevadaDefaults(File temp) throws IOException {
        File ini = new File(temp, "ddraw.ini");
        List<String> lines = ini.isFile() ? Files.readAllLines(ini.toPath(), StandardCharsets.ISO_8859_1) : new ArrayList<>();
        Map<String, String> missing = new LinkedHashMap<>();
        missing.put("ConfigFile", "config.cfg");
        missing.put("StartYear", "2140");
        missing.put("StartMonth", "9");
        missing.put("StartDay", "10");
        for (int i = 1; i <= 4; i++) missing.put("MovieTimer_artimer" + i, Integer.toString(36000 + (i - 1) * 30));
        boolean misc = false;
        int insert = -1;
        for (int i = 0; i < lines.size(); i++) {
            String line = lines.get(i).trim();
            if (line.startsWith("[")) {
                misc = line.equalsIgnoreCase("[Misc]");
                if (misc) insert = i + 1;
            }
            if (!misc) continue;
            int equals = line.indexOf('=');
            if (equals < 0) continue;
            String key = line.substring(0, equals).trim();
            missing.keySet().removeIf(k -> k.equalsIgnoreCase(key));
        }
        List<String> additions = new ArrayList<>();
        for (Map.Entry<String, String> entry : missing.entrySet()) additions.add(entry.getKey() + "=" + entry.getValue());
        if (insert < 0) {
            lines.add("[Misc]");
            insert = lines.size();
        }
        lines.addAll(insert, additions);
        Files.write(ini.toPath(), lines, StandardCharsets.ISO_8859_1);
    }

    static void writeConfig(File temp) throws IOException { writeConfig(temp, "fallout2.cfg"); }

    private static void writeConfig(File temp, String name) throws IOException {
        File cfg = new File(temp, name);
        List<String> lines = cfg.isFile() ? Files.readAllLines(cfg.toPath(), StandardCharsets.ISO_8859_1) : new ArrayList<>();
        StringBuilder out = new StringBuilder();
        boolean system = false;
        for (String line : lines) {
            String trimmed = line.trim().toLowerCase(Locale.ROOT);
            if (trimmed.startsWith("[")) system = trimmed.equals("[system]");
            if (system && trimmed.matches("(master_dat|critter_dat|master_patches|critter_patches|language)\\s*=.*")) continue;
            out.append(line).append('\n');
            if (trimmed.equals("[system]")) out.append(baseConfig());
        }
        if (lines.stream().noneMatch(l -> l.trim().equalsIgnoreCase("[system]")))
            out.append("\n[system]\n").append(baseConfig());
        Files.write(cfg.toPath(), out.toString().getBytes(StandardCharsets.ISO_8859_1));
    }

    private static String baseConfig() {
        return "master_dat=master.dat\ncritter_dat=critter.dat\nmaster_patches=data\ncritter_patches=data\nlanguage=english\n";
    }

    private static void cleanTemp(File file) throws IOException {
        if (!file.exists()) return;
        File[] children = file.listFiles();
        if (children != null) for (File child : children) cleanTemp(child);
        if (!file.delete()) throw new IOException("Could not clean installer staging folder");
    }
}
