package com.alexbatalov.fallout2ce;

import java.io.File;
import java.io.IOException;

// One file download, resumable. RpuActivity uses it through this interface
// only, so another implementation (e.g. Android's DownloadManager feeding a
// Downloads screen) can replace HttpDownloader. Callbacks come on any thread.
interface FileDownloader {
    interface Listener {
        void progress(long done, long total);

        // [target] is complete (size checked).
        void finished(File target);

        // Cancelled: the partial data is kept for the next start.
        void stopped();

        void failed(IOException error);
    }

    // Downloads [url] into [target] (a ".part" next to it while running;
    // resumes one left by an earlier run). [size] is the expected length.
    void start(String url, File target, long size, Listener listener);

    void cancel();
}
