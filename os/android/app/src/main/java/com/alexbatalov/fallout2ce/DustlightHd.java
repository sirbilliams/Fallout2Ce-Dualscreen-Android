package com.alexbatalov.fallout2ce;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

// Dustlight HD on Nexus (Fallout 2 mod 193), the pure part of the one-click
// flow: what a downloaded file is (art part / Windows engine zip / other),
// what is installed and how much is still missing. The release is
// <name>-<version>-art-<N>x[-partK].zip (independent parts of one tier, each
// unpacking under hd/) and <name>-<version>-engine.zip (Windows exe, not
// needed here). hd/pack_<N>x.json is in the first part only and says how many
// pack.json the whole tier holds. Parts merge into <game>/hd/<N>x/. No Android
// classes: runs on a desktop JVM.
final class DustlightHd {
    static final String NEXUS_FILES = "https://www.nexusmods.com/fallout2/mods/193?tab=files";
    static final String GUIDE = "dustlight";
    static final int RECOMMENDED_TIER = 2;
    // Written next to the art in the tier folder: scanned pack count and the
    // part zips merged so far.
    static final String PARTS_FILE = ".dustlight-parts";

    // Nexus may append "-<mod id>-<version>-<time>" after the uploaded name.
    private static final Pattern ART = Pattern.compile("-art-(\\d)x(?:-part(\\d+))?(?=[-_.]|$)", Pattern.CASE_INSENSITIVE);
    private static final Pattern ENGINE = Pattern.compile("-engine(?=[-_.]|$)", Pattern.CASE_INSENSITIVE);
    private static final Pattern PACKS = Pattern.compile("\"packs\"\\s*:\\s*(\\d+)");
    private static final Pattern MARKER = Pattern.compile("pack_(\\d)x\\.json");

    private DustlightHd() { }

    enum Kind { ART, ENGINE, OTHER }

    static final class Name {
        final Kind kind;
        final int tier;  // ART: 2..4, else 0
        final int part;  // ART: K of -partK, 0 when the name has none

        Name(Kind kind, int tier, int part) {
            this.kind = kind;
            this.tier = tier;
            this.part = part;
        }
    }

    // By file name alone. Matched by pattern, never by a hard-coded part count.
    static Name classify(String file) {
        if (file == null) return new Name(Kind.OTHER, 0, 0);
        String lower = file.toLowerCase(Locale.ROOT);
        if (!lower.endsWith(".zip")) return new Name(Kind.OTHER, 0, 0);
        Matcher art = ART.matcher(file);
        if (art.find()) {
            int tier = Integer.parseInt(art.group(1));
            if (tier >= 2 && tier <= 4) {
                return new Name(Kind.ART, tier, art.group(2) == null ? 0 : Integer.parseInt(art.group(2)));
            }
        }
        if (ENGINE.matcher(file).find()) return new Name(Kind.ENGINE, 0, 0);
        return new Name(Kind.OTHER, 0, 0);
    }

    // By content: an HD art pack (pack.json + albedo under hd/) or a part that
    // only carries hd/pack_<N>x.json.
    static boolean isHdContent(List<GameImport.Entry> entries) {
        if (HdPackInstall.looksLikeHdPack(entries)) return true;
        for (GameImport.Entry entry : entries) {
            String rel = HdPackInstall.hdRelative(entry.path);
            if (rel != null && rel.indexOf('/') < 0 && MARKER.matcher(rel).matches()) return true;
        }
        return false;
    }

    // What is on disk now.
    static final class Status {
        final int tier;            // 0 = nothing installed
        final int packs;           // pack.json files installed
        final int totalPacks;      // from pack_<N>x.json, -1 until part 1 is in
        final List<String> parts;  // zips merged so far

        Status(int tier, int packs, int totalPacks, List<String> parts) {
            this.tier = tier;
            this.packs = packs;
            this.totalPacks = totalPacks;
            this.parts = parts;
        }

        boolean complete() {
            return tier != 0 && totalPacks > 0 && packs >= totalPacks;
        }

        // Parts still to fetch: an estimate from the average pack count of the
        // parts in; 0 when complete, -1 when the total is not known yet.
        int moreParts() {
            if (tier == 0 || totalPacks < 0) return -1;
            int missing = totalPacks - packs;
            if (missing <= 0) return 0;
            int average = parts.isEmpty() || packs <= 0 ? 0 : (packs + parts.size() - 1) / parts.size();
            return average <= 0 ? 1 : (missing + average - 1) / average;
        }
    }

    static Status status(File game) {
        int tier = HdPackInstall.installedTier(game);
        if (tier == 0) return new Status(0, 0, -1, new ArrayList<>());
        File dir = HdPackInstall.tierDir(game, tier);
        int total = -1;
        File marker = new File(dir, "pack_" + tier + "x.json");
        if (marker.isFile() && marker.length() < (1 << 20)) {
            try {
                Matcher m = PACKS.matcher(new String(Files.readAllBytes(marker.toPath()), StandardCharsets.UTF_8));
                if (m.find()) total = Integer.parseInt(m.group(1));
            } catch (IOException | NumberFormatException e) {
                total = -1;
            }
        }
        int packs = -1;
        List<String> parts = new ArrayList<>();
        File list = new File(dir, PARTS_FILE);
        if (list.isFile()) {
            try {
                for (String line : Files.readAllLines(list.toPath(), StandardCharsets.UTF_8)) {
                    if (line.startsWith("packs ")) packs = Integer.parseInt(line.substring(6).trim());
                    else if (!line.isEmpty()) parts.add(line);
                }
            } catch (IOException | NumberFormatException e) {
                packs = -1;
            }
        }
        if (packs < 0) packs = countPacks(dir);
        return new Status(tier, packs, total, parts);
    }

    static int countPacks(File dir) {
        int count = 0;
        File[] children = dir.listFiles();
        if (children == null) return 0;
        for (File child : children) {
            if (child.isDirectory()) count += countPacks(child);
            else if (child.getName().equals("pack.json")) count++;
        }
        return count;
    }

    // Called by HdPackInstall after a zip is published into [tierDir]: rescans
    // the pack count and records the zip (a fresh install starts the list).
    static void record(File tierDir, String name, boolean merged) {
        Map<String, Boolean> parts = new LinkedHashMap<>();
        File list = new File(tierDir, PARTS_FILE);
        if (merged && list.isFile()) {
            try {
                for (String line : Files.readAllLines(list.toPath(), StandardCharsets.UTF_8)) {
                    if (!line.isEmpty() && !line.startsWith("packs ")) parts.put(line, true);
                }
            } catch (IOException ignored) {
            }
        }
        if (name != null && !name.trim().isEmpty()) parts.put(name.replace('\n', ' ').trim(), true);
        StringBuilder text = new StringBuilder("packs ").append(countPacks(tierDir)).append('\n');
        for (String part : parts.keySet()) text.append(part).append('\n');
        try {
            Files.write(list.toPath(), text.toString().getBytes(StandardCharsets.UTF_8));
        } catch (IOException ignored) {
            // The scan in status() covers a missing list.
        }
    }

    // After an install: when the tier just became complete, HD is switched on
    // ([hd] enabled=1, hd_art_dir=hd/<N>x). True when that happened now.
    static boolean enableIfNewlyComplete(File game, Status before) throws IOException {
        Status now = status(game);
        boolean wasComplete = before != null && before.complete() && before.tier == now.tier;
        if (wasComplete || !now.complete()) return false;
        HdSettings.setEnabled(game, true);
        return true;
    }
}
