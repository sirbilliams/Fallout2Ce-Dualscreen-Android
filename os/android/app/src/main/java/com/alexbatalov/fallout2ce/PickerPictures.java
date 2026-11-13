package com.alexbatalov.fallout2ce;

import java.io.*;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.util.regex.*;

// Pure source/cache policy. No pictures or network access at build time.
final class PickerPictures {
    static final int PREVIEW_CAP = 512 * 1024;
    static final int FULL_CAP = 8 * 1024 * 1024;
    static final int PAGE_CAP = 1024 * 1024;

    static File directory(File top, String id, boolean installed) {
        if (top == null || !id.matches("fallout2|ettu|sonora|resurrection|nevada|olympus2207|megamod|yesterday"))
            throw new IllegalArgumentException("unknown game");
        return new File(top, installed ? "games/" + id + "/picker" : "picker-preview/" + id);
    }

    static File cached(File top, String id) {
        File full = new File(directory(top, id, true), "art.rgba");
        File preview = new File(directory(top, id, false), "art.rgba");
        return full.isFile() ? full : preview.isFile() ? preview : null;
    }

    /**
     * Art made before social cards were rejected: when the picture directory's
     * credit.txt "Image:" line is a GitHub social card, removes art.rgba and
     * credit.txt (a regenerable cache) so the picture is fetched again, or the
     * plain themed panel shows when no replacement exists. True when purged.
     */
    static boolean purgeSocialCard(File dir) {
        File art = new File(dir, "art.rgba"), credit = new File(dir, "credit.txt");
        if (!art.isFile() || !credit.isFile()) return false;
        try {
            for (String line : new String(java.nio.file.Files.readAllBytes(credit.toPath()), StandardCharsets.UTF_8).split("\\r?\\n")) {
                if (!line.startsWith("Image:")) continue;
                if (!isSocialCard(line.substring(6).trim())) return false;
                return art.delete() & credit.delete();
            }
        } catch (IOException e) { /* unreadable credit: keep the art */ }
        return false;
    }

    static boolean mayFetch(boolean connected, boolean wifiOrEthernet, boolean metered) {
        return connected && wifiOrEthernet && !metered;
    }

    static String readmeApi(String page) {
        URI uri = URI.create(page);
        if (!"github.com".equalsIgnoreCase(uri.getHost())) return null;
        String[] parts = uri.getPath().split("/");
        if (parts.length < 3 || !parts[1].matches("[A-Za-z0-9_.-]+") || !parts[2].matches("[A-Za-z0-9_.-]+")) return null;
        return "https://api.github.com/repos/" + parts[1] + "/" + parts[2] + "/readme";
    }

    // GitHub README API response; only download_url is used, never avatar URLs.
    static String readmeUrl(String json) {
        Matcher m = Pattern.compile("\\\"download_url\\\"\\s*:\\s*\\\"((?:\\\\.|[^\\\"\\\\])*)\\\"").matcher(json);
        if (!m.find()) return null;
        String value = m.group(1).replace("\\/", "/").replace("\\u0026", "&");
        return safeUrl("https://github.com/", value);
    }

    /** GitHub's generated repository social cards and avatars are not game art. */
    static boolean isSocialCard(String url) {
        try {
            String host = URI.create(url).getHost();
            return host != null && (host.equalsIgnoreCase("opengraph.githubassets.com")
                || host.equalsIgnoreCase("repository-images.githubusercontent.com")
                || host.equalsIgnoreCase("avatars.githubusercontent.com"));
        } catch (IllegalArgumentException e) { return true; }
    }

    static String safeUrl(String page, String value) {
        try {
            URI uri = URI.create(page).resolve(value.trim());
            String host = uri.getHost();
            if (!"https".equalsIgnoreCase(uri.getScheme()) || host == null || uri.getUserInfo() != null
                    || (uri.getPort() != -1 && uri.getPort() != 443)
                    || host.equalsIgnoreCase("localhost") || host.indexOf(':') >= 0
                    || host.matches("[0-9.]+") || host.endsWith(".local")) return null;
            return uri.toASCIIString();
        } catch (IllegalArgumentException e) { return null; }
    }

    static String imageUrl(String page, String text) {
        Matcher tags = Pattern.compile("<meta\\b[^>]*>", Pattern.CASE_INSENSITIVE).matcher(text);
        while (tags.find()) {
            String property = null, content = null;
            Matcher attrs = Pattern.compile("([\\w:-]+)\\s*=\\s*(['\"])(.*?)\\2", Pattern.DOTALL).matcher(tags.group());
            while (attrs.find()) {
                if (attrs.group(1).equalsIgnoreCase("property") || attrs.group(1).equalsIgnoreCase("name")) property = attrs.group(3);
                if (attrs.group(1).equalsIgnoreCase("content")) content = attrs.group(3);
            }
            if ("og:image".equalsIgnoreCase(property) && content != null) {
                String url = safeUrl(page, content.replace("&amp;", "&").replace("&#38;", "&"));
                if (url != null && !isSocialCard(url)) return url;
            }
        }
        // README image URLs, relative to the raw README URL returned by GitHub.
        Matcher md = Pattern.compile("!\\[[^\\]]*\\]\\(<?([^\\s)>]+)>?(?:\\s+[^)]*)?\\)").matcher(text);
        if (md.find()) return safeUrl(page, md.group(1));
        Matcher img = Pattern.compile("<img\\b[^>]*\\bsrc\\s*=\\s*['\"]([^'\"]+)['\"]", Pattern.CASE_INSENSITIVE).matcher(text);
        return img.find() ? safeUrl(page, img.group(1)) : null;
    }

    static byte[] readLimited(InputStream input, int cap) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buffer = new byte[8192];
        int n;
        while ((n = input.read(buffer, 0, Math.min(buffer.length, cap - out.size() + 1))) != -1) {
            if (n > cap - out.size()) throw new IOException("picture response exceeds cap");
            out.write(buffer, 0, n);
        }
        return out.toByteArray();
    }

    static String utf8(byte[] bytes) { return new String(bytes, StandardCharsets.UTF_8); }
}
