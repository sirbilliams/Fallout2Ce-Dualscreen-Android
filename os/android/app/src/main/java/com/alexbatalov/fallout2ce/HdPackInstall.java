package com.alexbatalov.fallout2ce;

import android.content.Context;
import android.net.Uri;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

// Dustlight HD art packs (Nexus Fallout 2 mod 193): a zip holding, under hd/,
// one folder per FRM with pack.json (version, tiers, layout, rotations) and
// albedo_<N>x.dds (BC7), plus hd/pack_<N>x.json and hd/fonts. One zip is one
// tier (2x, 3x or 4x). Installed into <game>/hd/<N>x/ (the same layout, hd/
// stripped); only one tier is kept. No Android classes beyond the Context
// entry point, so the rest runs on a desktop JVM.
final class HdPackInstall {
    interface Progress extends GameImport.Progress { }

    static final String DIR = "hd";
    static final int PACK_VERSION = 1;
    private static final String TMP = ".tmp-";
    private static final String OLD = ".old-";
    private static final long MARGIN = 64L << 20;
    private static final Pattern ALBEDO = Pattern.compile("albedo_(\\d)x\\.dds");
    private static final Pattern MARKER = Pattern.compile("pack_(\\d)x\\.json");
    private static final Pattern PART = Pattern.compile("-part\\d+(?=[-_.]|$)", Pattern.CASE_INSENSITIVE);

    private HdPackInstall() { }

    static final class Problem extends IOException {
        final int message;
        final long neededMb;

        Problem(int message, long neededMb) {
            super("hd pack: " + message);
            this.message = message;
            this.neededMb = neededMb;
        }
    }

    // The zip is another tier than the one installed (and switching was not
    // allowed): the caller asks the player.
    static final class TierMismatch extends IOException {
        final int installed;
        final int incoming;

        TierMismatch(int installed, int incoming) {
            super("hd tier " + incoming + " vs installed " + installed);
            this.installed = installed;
            this.incoming = incoming;
        }
    }

    // A pack.json or pack zip that is not the Dustlight schema.
    static final class Invalid extends IOException {
        Invalid(String text) {
            super(text);
        }
    }

    static final class Result {
        final int tier;
        final int packs;
        final long bytes;

        Result(int tier, int packs, long bytes) {
            this.tier = tier;
            this.packs = packs;
            this.bytes = bytes;
        }
    }

    // What a validated zip will unpack: [files] with the path inside hd/.
    static final class Plan {
        int tier;
        int packs;
        long bytes;
        final List<GameImport.Entry> files = new ArrayList<>();
        final Map<GameImport.Entry, String> dest = new HashMap<>();
    }

    // The path of an entry below hd/ ("hd/x" or "wrapper/hd/x"), null when it
    // is not under hd/ (docs, the engine).
    static String hdRelative(String path) {
        String[] parts = path.split("/", -1);
        int at = parts.length > 1 && parts[0].equals("hd") ? 1
            : parts.length > 2 && parts[1].equals("hd") ? 2 : -1;
        if (at < 0) return null;
        StringBuilder out = new StringBuilder();
        for (int i = at; i < parts.length; i++) out.append(i > at ? "/" : "").append(parts[i]);
        return out.toString();
    }

    // Cheap test on the listing alone: a pack.json next to an albedo_<N>x.dds
    // below hd/. The full check is `validate`.
    static boolean looksLikeHdPack(List<GameImport.Entry> entries) {
        Set<String> dirs = new HashSet<>();
        for (GameImport.Entry entry : entries) {
            String rel = hdRelative(entry.path);
            if (rel != null && rel.endsWith("/pack.json")) dirs.add(rel.substring(0, rel.length() - 9));
        }
        if (dirs.isEmpty()) return false;
        for (GameImport.Entry entry : entries) {
            String rel = hdRelative(entry.path);
            if (rel == null) continue;
            int slash = rel.lastIndexOf('/');
            if (slash > 0 && dirs.contains(rel.substring(0, slash + 1)) && ALBEDO.matcher(rel.substring(slash + 1)).matches()) {
                return true;
            }
        }
        return false;
    }

    static boolean isHdPack(Context context, Uri zip) {
        try (GameImport.Source source = new GameImport.ArchiveSource(context.getContentResolver(), zip, (done, total) -> { })) {
            return looksLikeHdPack(source.list());
        } catch (IOException | RuntimeException e) {
            return false;
        }
    }

    // Checks the zip's listing and every pack.json (read through [source]).
    // Throws Invalid / IOException for a bad path or schema; nothing is written.
    static Plan validate(GameImport.Source source) throws IOException {
        List<GameImport.Entry> entries = source.list();
        // Same path rules as a mod zip (absolute, "..", "\\", ":" ... rejected).
        ModInstall.detectRoot(entries);
        Plan plan = new Plan();
        Map<String, GameImport.Entry> byRel = new HashMap<>();
        List<GameImport.Entry> packJson = new ArrayList<>();
        Set<Integer> tiers = new HashSet<>();
        for (GameImport.Entry entry : entries) {
            if (entry.encrypted || entry.unsupported) throw new Invalid("unreadable entry " + entry.path);
            if (entry.size < 0) throw new IOException("invalid archive size");
            String rel = hdRelative(entry.path);
            if (rel == null) continue;
            if (byRel.put(rel, entry) != null) throw new Invalid("duplicate " + rel);
            plan.files.add(entry);
            plan.dest.put(entry, rel);
            if (plan.bytes > Long.MAX_VALUE / 2 - entry.size) throw new IOException("invalid archive size");
            plan.bytes += entry.size;
            String name = rel.substring(rel.lastIndexOf('/') + 1);
            Matcher albedo = ALBEDO.matcher(name);
            if (albedo.matches() && rel.indexOf('/') > 0) tiers.add(Integer.parseInt(albedo.group(1)));
            Matcher marker = MARKER.matcher(name);
            if (marker.matches() && rel.indexOf('/') < 0) tiers.add(Integer.parseInt(marker.group(1)));
            if (name.equals("pack.json") && rel.indexOf('/') > 0) packJson.add(entry);
        }
        if (packJson.isEmpty()) throw new Invalid("no pack.json");
        if (tiers.size() != 1) throw new Invalid("a pack zip holds exactly one tier, found " + tiers);
        plan.tier = tiers.iterator().next();
        if (plan.tier < 2 || plan.tier > 4) throw new Invalid("tier " + plan.tier);

        final Map<GameImport.Entry, ByteArrayOutputStream> bodies = new HashMap<>();
        source.read(packJson, new GameImport.EntrySink() {
            ByteArrayOutputStream current;

            public void begin(GameImport.Entry entry) {
                current = new ByteArrayOutputStream();
                bodies.put(entry, current);
            }

            public void data(byte[] buffer, int length) throws IOException {
                if (current.size() + length > (1 << 24)) throw new Invalid("pack.json too large");
                current.write(buffer, 0, length);
            }

            public void end(GameImport.Entry entry) { }
        });
        for (GameImport.Entry entry : packJson) {
            ByteArrayOutputStream body = bodies.get(entry);
            if (body == null) throw new Invalid("unread " + entry.path);
            String rel = plan.dest.get(entry);
            String dir = rel.substring(0, rel.length() - "pack.json".length());
            String dds = dir + "albedo_" + plan.tier + "x.dds";
            // The file the pack names must be in the zip (and not be empty).
            GameImport.Entry file = byRel.get(dds);
            if (file == null || file.size < 148) throw new Invalid("missing " + dds);
            validatePackJson(new String(body.toByteArray(), StandardCharsets.UTF_8), plan.tier, rel);
            plan.packs++;
        }
        // Every DDS has its pack.json.
        for (String rel : byRel.keySet()) {
            int slash = rel.lastIndexOf('/');
            if (slash > 0 && ALBEDO.matcher(rel.substring(slash + 1)).matches()
                && !byRel.containsKey(rel.substring(0, slash + 1) + "pack.json")) {
                throw new Invalid("no pack.json for " + rel);
            }
        }
        return plan;
    }

    // The Dustlight schema (fo2hd/pack.py build_one): version, tiers, layout
    // [w, h], rotations; [tier] must be listed in tiers.
    static void validatePackJson(String text, int tier, String where) throws IOException {
        Object root;
        try {
            root = new Json(text).parseAll();
        } catch (RuntimeException e) {
            throw new Invalid("bad json in " + where);
        }
        if (!(root instanceof Map)) throw new Invalid("not an object: " + where);
        Map<?, ?> map = (Map<?, ?>) root;
        Object version = map.get("version");
        if (!(version instanceof Double) || ((Double) version).intValue() != PACK_VERSION
            || ((Double) version) != Math.floor((Double) version)) {
            throw new Invalid("unsupported pack version in " + where);
        }
        Object list = map.get("tiers");
        boolean listed = false;
        if (list instanceof List) {
            for (Object t : (List<?>) list) listed |= t instanceof Double && ((Double) t).intValue() == tier;
        }
        if (!listed) throw new Invalid("tier " + tier + " not in " + where);
        Object layout = map.get("layout");
        if (!(layout instanceof List) || ((List<?>) layout).size() != 2) throw new Invalid("bad layout in " + where);
        for (Object n : (List<?>) layout) {
            if (!(n instanceof Double) || (Double) n <= 0) throw new Invalid("bad layout in " + where);
        }
        if (!(map.get("rotations") instanceof List)) throw new Invalid("no rotations in " + where);
    }

    static File tierDir(File gameDir, int tier) {
        return new File(new File(gameDir, DIR), tier + "x");
    }

    // The zip's file name says it is one part of a split tier (merged into the
    // tier already there when it is the same one).
    static boolean isPart(String name) {
        return name != null && PART.matcher(name).find();
    }

    static Result install(Context context, Uri zip, String name, File gameDir, Progress progress) throws IOException {
        return install(context, zip, name, gameDir, progress, true);
    }

    static Result install(Context context, Uri zip, String name, File gameDir, Progress progress, boolean allowSwitch) throws IOException {
        try (InputStream in = context.getContentResolver().openInputStream(zip)) {
            if (in == null || in.read() != 'P' || in.read() != 'K') throw new Problem(R.string.mod_unreadable, 0);
        }
        try (GameImport.Source source = new GameImport.ArchiveSource(context.getContentResolver(), zip, (done, total) -> {
            if (progress.isCancelled()) throw new GameImport.CancelledException();
            progress.update(done, total, "");
        })) {
            return install(source, name, gameDir, progress, allowSwitch);
        } catch (NativeArchive.Failure failure) {
            throw new Problem(R.string.mod_unreadable, 0);
        }
    }

    // Replaces tier N atomically (written next to it as .tmp-Nx, then renamed);
    // a part zip (-partK in [name]) is merged into the tier already there.
    static Result install(GameImport.Source source, String name, File gameDir, Progress progress) throws IOException {
        return install(source, name, gameDir, progress, true);
    }

    // [allowSwitch] false: a zip of another tier than the installed one throws
    // TierMismatch (nothing written) instead of replacing that tier.
    static Result install(GameImport.Source source, String name, File gameDir, Progress progress, boolean allowSwitch) throws IOException {
        if (gameDir == null) throw new IOException("game storage unavailable");
        Plan plan = validate(source);
        int have = installedTier(gameDir);
        if (!allowSwitch && have != 0 && have != plan.tier) throw new TierMismatch(have, plan.tier);
        File root = new File(gameDir, DIR);
        if (!root.isDirectory() && !root.mkdirs()) throw new IOException("can't create " + root);
        cleanup(gameDir);
        long needed = plan.bytes + MARGIN;
        if (root.getUsableSpace() < needed) {
            throw new Problem(R.string.mod_no_space, needed / 1048576 + (needed % 1048576 == 0 ? 0 : 1));
        }
        File target = tierDir(gameDir, plan.tier);
        File temp = GameImport.targetFile(root, TMP + plan.tier + "x");
        if (temp.exists()) throw new IOException("can't clean interrupted hd install");
        boolean merge = (isPart(name) || DustlightHd.classify(name).kind == DustlightHd.Kind.ART) && target.isDirectory();
        try {
            if (!temp.mkdir()) throw new IOException("can't create " + temp);
            unpack(source, plan, temp, progress);
            if (progress.isCancelled()) throw new GameImport.CancelledException();
            if (merge) {
                merge(temp, target);
            } else {
                File old = new File(root, OLD + plan.tier + "x");
                GameFiles.deleteRecursively(old);
                if (target.exists() && !target.renameTo(old)) throw new IOException("can't replace " + target);
                if (!temp.renameTo(target)) {
                    if (old.exists()) old.renameTo(target);
                    throw new IOException("can't publish " + target);
                }
                GameFiles.deleteRecursively(old);
            }
            DustlightHd.record(target, name, merge);
            // One tier at a time.
            for (int other = 2; other <= 4; other++) {
                if (other != plan.tier) GameFiles.deleteRecursively(tierDir(gameDir, other));
            }
            return new Result(plan.tier, plan.packs, plan.bytes);
        } finally {
            GameFiles.deleteRecursively(temp);
        }
    }

    private static void unpack(GameImport.Source source, Plan plan, File temp, Progress progress) throws IOException {
        final long total = plan.bytes;
        final long[] done = { 0 };
        source.read(plan.files, new GameImport.EntrySink() {
            FileOutputStream out;
            boolean first;
            boolean dds;

            public void begin(GameImport.Entry entry) throws IOException {
                if (progress.isCancelled()) throw new GameImport.CancelledException();
                String rel = plan.dest.get(entry);
                File file = GameImport.targetFile(temp, rel);
                File parent = file.getParentFile();
                if (!parent.isDirectory() && !parent.mkdirs()) throw new IOException("can't create " + parent);
                out = new FileOutputStream(file);
                first = true;
                dds = rel.toLowerCase(Locale.ROOT).endsWith(".dds");
            }

            public void data(byte[] buffer, int length) throws IOException {
                if (first && dds) {
                    if (length < 4 || buffer[0] != 'D' || buffer[1] != 'D' || buffer[2] != 'S' || buffer[3] != ' ') {
                        throw new Invalid("not a DDS file");
                    }
                }
                first = false;
                out.write(buffer, 0, length);
                done[0] += length;
                progress.update(done[0], total, "");
            }

            public void end(GameImport.Entry entry) throws IOException {
                out.close();
                out = null;
            }
        });
    }

    // Moves [from]'s files into [to] (same folder tree, files replaced).
    private static void merge(File from, File to) throws IOException {
        File[] children = from.listFiles();
        if (children == null) return;
        for (File child : children) {
            File dest = new File(to, child.getName());
            if (child.isDirectory()) {
                if (!dest.isDirectory() && !dest.mkdirs()) throw new IOException("can't create " + dest);
                merge(child, dest);
            } else {
                if (dest.exists() && !dest.delete()) throw new IOException("can't replace " + dest);
                if (!child.renameTo(dest)) throw new IOException("can't move " + dest);
            }
        }
    }

    // Leftovers of an interrupted install.
    static void cleanup(File gameDir) {
        File[] children = new File(gameDir, DIR).listFiles();
        if (children == null) return;
        for (File child : children) {
            String n = child.getName();
            if (n.startsWith(TMP)) {
                GameFiles.deleteRecursively(child);
            } else if (n.startsWith(OLD)) {
                // The replaced tier, put back when the new one never arrived.
                File back = new File(child.getParentFile(), n.substring(OLD.length()));
                if (!back.exists() && child.renameTo(back)) continue;
                GameFiles.deleteRecursively(child);
            }
        }
    }

    // The installed tier (2..4) or 0.
    static int installedTier(File gameDir) {
        for (int tier = 2; tier <= 4; tier++) {
            if (tierDir(gameDir, tier).isDirectory()) return tier;
        }
        return 0;
    }

    static long sizeOf(File folder) {
        long total = 0;
        File[] children = folder.listFiles();
        if (children == null) return 0;
        for (File child : children) total += child.isDirectory() ? sizeOf(child) : child.length();
        return total;
    }

    // Removes only <game>/hd/<N>x; true when it was there.
    static boolean remove(File gameDir, int tier) {
        File dir = tierDir(gameDir, tier);
        if (!dir.isDirectory()) return false;
        GameFiles.deleteRecursively(dir);
        return !dir.exists();
    }

    // Minimal JSON reader: objects as Map, arrays as List, numbers as Double.
    private static final class Json {
        private final String s;
        private int i;

        Json(String s) {
            this.s = s;
        }

        Object parseAll() {
            Object v = value(0);
            ws();
            if (i != s.length()) throw new IllegalArgumentException("trailing");
            return v;
        }

        private void ws() {
            while (i < s.length() && " \t\r\n".indexOf(s.charAt(i)) >= 0) i++;
        }

        private Object value(int depth) {
            if (depth > 64) throw new IllegalArgumentException("deep");
            ws();
            char c = s.charAt(i);
            if (c == '{') {
                i++;
                Map<String, Object> map = new HashMap<>();
                ws();
                if (s.charAt(i) == '}') { i++; return map; }
                while (true) {
                    ws();
                    String key = string();
                    ws();
                    expect(':');
                    map.put(key, value(depth + 1));
                    ws();
                    if (s.charAt(i) == ',') { i++; continue; }
                    expect('}');
                    return map;
                }
            }
            if (c == '[') {
                i++;
                List<Object> list = new ArrayList<>();
                ws();
                if (s.charAt(i) == ']') { i++; return list; }
                while (true) {
                    list.add(value(depth + 1));
                    ws();
                    if (s.charAt(i) == ',') { i++; continue; }
                    expect(']');
                    return list;
                }
            }
            if (c == '"') return string();
            if (s.startsWith("true", i)) { i += 4; return Boolean.TRUE; }
            if (s.startsWith("false", i)) { i += 5; return Boolean.FALSE; }
            if (s.startsWith("null", i)) { i += 4; return null; }
            int start = i;
            while (i < s.length() && "+-0123456789.eE".indexOf(s.charAt(i)) >= 0) i++;
            if (start == i) throw new IllegalArgumentException("value");
            return Double.valueOf(s.substring(start, i));
        }

        private void expect(char c) {
            if (s.charAt(i) != c) throw new IllegalArgumentException("expected " + c);
            i++;
        }

        private String string() {
            expect('"');
            StringBuilder out = new StringBuilder();
            while (true) {
                char c = s.charAt(i++);
                if (c == '"') return out.toString();
                if (c != '\\') { out.append(c); continue; }
                char e = s.charAt(i++);
                switch (e) {
                    case 'n': out.append('\n'); break;
                    case 't': out.append('\t'); break;
                    case 'r': out.append('\r'); break;
                    case 'b': out.append('\b'); break;
                    case 'f': out.append('\f'); break;
                    case 'u': out.append((char) Integer.parseInt(s.substring(i, i + 4), 16)); i += 4; break;
                    default: out.append(e);
                }
            }
        }
    }
}
