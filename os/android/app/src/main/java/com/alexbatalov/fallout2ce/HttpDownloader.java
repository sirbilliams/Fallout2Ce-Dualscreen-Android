package com.alexbatalov.fallout2ce;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.RandomAccessFile;
import java.net.HttpURLConnection;
import java.net.URL;

// FileDownloader over HttpURLConnection: HTTP Range on "<target>.part".
final class HttpDownloader implements FileDownloader {
    private volatile boolean cancelled;

    @Override
    public void cancel() {
        cancelled = true;
    }

    @Override
    public void start(String url, File target, long size, Listener listener) {
        cancelled = false;
        new Thread(() -> {
            try {
                if (!run(url, target, size, listener)) listener.stopped();
                else listener.finished(target);
            } catch (IOException | RuntimeException e) {
                listener.failed(e instanceof IOException ? (IOException) e : new IOException(e));
            }
        }).start();
    }

    static File partOf(File target) {
        return new File(target.getParentFile(), target.getName() + ".part");
    }

    // False when cancelled.
    private boolean run(String url, File target, long total, Listener listener) throws IOException {
        File folder = target.getParentFile();
        if (!folder.isDirectory() && !folder.mkdirs()) throw new IOException("can't make " + folder);
        File part = partOf(target);
        long have = part.length();
        if (have > total) { part.delete(); have = 0; }
        if (RpuRelease.resume(have, total) != RpuRelease.Resume.DONE) {
            HttpURLConnection connection = open(url);
            try {
                String range = RpuRelease.rangeHeader(have, total);
                if (range != null) connection.setRequestProperty("Range", range);
                int status = connection.getResponseCode();
                if (status == 416) {
                    // The partial file isn't ours: start again.
                    part.delete();
                    have = 0;
                    connection.disconnect();
                    connection = open(url);
                    status = connection.getResponseCode();
                    range = null;
                }
                if (status != 200 && status != 206) throw new IOException("HTTP " + status);
                if (range != null && !RpuRelease.rangeAccepted(status, connection.getHeaderField("Content-Range"), have, total)) {
                    if (status == 206) throw new IOException("unexpected range");
                    have = 0; // The server ignored Range and sends all of it.
                } else if (range == null && status == 206) {
                    throw new IOException("unexpected range");
                }
                try (InputStream in = connection.getInputStream(); RandomAccessFile out = new RandomAccessFile(part, "rw")) {
                    out.setLength(have);
                    out.seek(have);
                    byte[] buffer = new byte[65536];
                    long lastUpdate = 0;
                    int count;
                    while (!cancelled && (count = in.read(buffer)) != -1) {
                        out.write(buffer, 0, count);
                        have += count;
                        long now = System.currentTimeMillis();
                        if (now - lastUpdate > 250) {
                            lastUpdate = now;
                            listener.progress(have, total);
                        }
                    }
                    if (cancelled) return false;
                }
            } finally {
                connection.disconnect();
            }
        }
        if (part.length() != total) throw new IOException("incomplete download (" + part.length() + " of " + total + ")");
        if (!part.renameTo(target)) throw new IOException("can't finish " + target);
        return true;
    }

    static HttpURLConnection open(String url) throws IOException {
        HttpURLConnection connection = (HttpURLConnection) new URL(url).openConnection();
        connection.setConnectTimeout(20000);
        connection.setReadTimeout(30000);
        connection.setInstanceFollowRedirects(true);
        connection.setRequestProperty("User-Agent", "Fallout2CE-Android");
        connection.setRequestProperty("Accept", "application/octet-stream, application/vnd.github+json");
        return connection;
    }
}
