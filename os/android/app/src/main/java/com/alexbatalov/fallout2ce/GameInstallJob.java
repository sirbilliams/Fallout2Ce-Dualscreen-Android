package com.alexbatalov.fallout2ce;

import android.app.job.JobParameters;
import android.app.job.JobService;
import java.io.*;

// JobScheduler owns extraction, rather than a short-lived broadcast receiver.
// Stopped jobs interrupt extraction and retry; completed roots are idempotent.
public class GameInstallJob extends JobService {
    private Thread worker;
    private volatile boolean stopped;
    @Override public boolean onStartJob(JobParameters params) {
        stopped = false;
        worker = new Thread(() -> {
            boolean retry = false;
            try {
                for (DownloadIndex.Row row : ModDownloads.rows(this, ModDownloads.gameFolder(this))) {
                    String id = GameInstaller.game(row);
                    if (id == null || row.item.installed) continue;
                    if (row.state.active()) { retry = true; continue; }
                    if (row.state.kind != DownloadIndex.Kind.DOWNLOADED) continue;
                    // A failed archive is retried only by an explicit picker tap.
                    if (GameInstaller.status(this, id).startsWith("Install failed:")) continue;
                    try {
                        final int[] lastPercent = { -1 };
                        GameInstaller.status(this, id, "Installing 0%");
                        GameInstall.extract(GameRoots.topRoot(this), id,
                            new File(ModDownloads.gameFolder(this), row.item.file), (done, total) -> {
                                int percent = total == 0 ? 100 : (int) (100.0 * done / total);
                                if (lastPercent[0] != percent) {
                                    lastPercent[0] = percent;
                                    GameInstaller.status(this, id, "Installing " + percent + "%");
                                }
                            });
                        DownloadIndex.update(ModDownloads.gameFolder(this), items -> {
                            for (DownloadIndex.Item item : items) if (item.id == row.item.id) item.installed = true;
                        });
                        if (GameRoots.ETTU.equals(id) && EtTuUndat.needed(GameRoots.topRoot(this))) {
                            GameInstaller.status(this, id, "Installing: unpacking Fallout 1 data");
                            try {
                                EtTuUndat.run(GameRoots.topRoot(this), EtTuUndat.REAL);
                            } catch (IOException | RuntimeException e) {
                                // Not fatal for the install: the launch retries and reports it.
                                android.util.Log.w("GameInstallJob", "Et Tu unpack failed", e);
                            }
                        }
                        GameInstaller.status(this, id, "Installed");
                        GamePictures.afterInstall(this, id);
                    } catch (IOException | RuntimeException e) {
                        if (stopped || Thread.currentThread().isInterrupted()) {
                            retry = true;
                            break;
                        }
                        GameInstaller.status(this, id, "Install failed: " + e.getMessage());
                    }
                }
            } catch (IOException | RuntimeException e) {
                android.util.Log.w("GameInstallJob", "Install interrupted", e);
                retry = true;
            } finally { if (!stopped) jobFinished(params, retry); }
        }, "game-extract");
        worker.start();
        return true;
    }
    @Override public boolean onStopJob(JobParameters params) {
        stopped = true;
        if (worker != null) worker.interrupt();
        return true;
    }
}
