package com.alexbatalov.fallout2ce;

import android.app.ActivityManager;
import android.app.AlertDialog;
import android.content.Intent;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.BaseAdapter;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.ProgressBar;
import android.widget.TextView;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

// Mods > Downloads: every archive in mod-downloads with its state; install,
// cancel and delete (long-press selects several). Polls the DownloadManager
// about once a second while visible. Runs in the :import process.
public class DownloadsActivity extends ImmersiveActivity {
    private static final String TAG = "DownloadsActivity";

    private final Handler handler = new Handler(Looper.getMainLooper());
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private final Set<String> selected = new HashSet<>();
    private List<DownloadIndex.Row> rows = new ArrayList<>();
    private String signature = "";
    private boolean polling;
    private boolean installing;
    private boolean selecting;

    private Adapter adapter;
    private TextView empty;
    private TextView space;
    private TextView message;
    private Button deleteSelected;
    private Button selectDone;

    private final Runnable poll = new Runnable() {
        @Override public void run() {
            if (!polling) return;
            worker.execute(() -> {
                List<DownloadIndex.Row> fresh;
                try {
                    fresh = ModDownloads.rows(DownloadsActivity.this);
                } catch (RuntimeException e) {
                    Log.w(TAG, "refresh failed", e);
                    return;
                }
                runOnUiThread(() -> show(fresh));
            });
            handler.postDelayed(this, 1000);
        }
    };

    private int dp(int v) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics());
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(Color.rgb(16, 16, 16));

        LinearLayout bar = new LinearLayout(this);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        bar.setBackgroundColor(Color.rgb(24, 24, 24));
        bar.setPadding(dp(8), dp(4), dp(8), dp(4));
        Button back = new Button(this);
        back.setText(R.string.dl_back);
        back.setOnClickListener(v -> onBackPressed());
        TextView title = new TextView(this);
        title.setText(R.string.dl_title);
        title.setTextColor(Color.WHITE);
        title.setTextSize(18);
        title.setPadding(dp(16), 0, dp(16), 0);
        message = new TextView(this);
        message.setTextColor(Color.rgb(200, 200, 120));
        message.setSingleLine(true);
        message.setEllipsize(android.text.TextUtils.TruncateAt.END);
        deleteSelected = new Button(this);
        deleteSelected.setOnClickListener(v -> askDeleteSelected());
        selectDone = new Button(this);
        selectDone.setText(R.string.dl_select_done);
        selectDone.setOnClickListener(v -> endSelecting());
        bar.addView(back);
        bar.addView(title);
        Button getHd = new Button(this);
        getHd.setText(R.string.dl_get_hd);
        getHd.setOnClickListener(v -> startActivity(new Intent(this, NexusActivity.class)
            .putExtra(NexusActivity.EXTRA_URL, DustlightHd.NEXUS_FILES)
            .putExtra(NexusActivity.EXTRA_GUIDE, DustlightHd.GUIDE)));
        bar.addView(getHd);
        bar.addView(message, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        bar.addView(deleteSelected);
        bar.addView(selectDone);
        root.addView(bar, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        ListView list = new ListView(this);
        list.setDivider(null);
        list.setSelector(android.R.color.transparent);
        adapter = new Adapter();
        list.setAdapter(adapter);
        FrameHolder holder = new FrameHolder(this);
        holder.addView(list, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        empty = new TextView(this);
        empty.setText(R.string.dl_empty);
        empty.setTextColor(Color.LTGRAY);
        empty.setGravity(Gravity.CENTER);
        empty.setVisibility(View.GONE);
        holder.addView(empty, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        root.addView(holder, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        space = new TextView(this);
        space.setTextColor(Color.LTGRAY);
        space.setBackgroundColor(Color.rgb(24, 24, 24));
        space.setPadding(dp(16), dp(6), dp(16), dp(6));
        root.addView(space, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        setContentView(root);
        updateSelectionUi();
    }

    // A plain FrameLayout (kept as its own type for readability above).
    private static final class FrameHolder extends android.widget.FrameLayout {
        FrameHolder(android.content.Context c) { super(c); }
    }

    @Override
    protected void onResume() {
        super.onResume();
        polling = true;
        handler.post(poll);
    }

    @Override
    protected void onPause() {
        polling = false;
        handler.removeCallbacks(poll);
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        worker.shutdownNow();
        super.onDestroy();
    }

    @Override
    public void onBackPressed() {
        if (selecting) {
            endSelecting();
            return;
        }
        startActivity(new Intent(this, MainActivity.class));
        finish();
    }

    // --- list ---

    private void show(List<DownloadIndex.Row> fresh) {
        StringBuilder sig = new StringBuilder();
        Set<String> names = new HashSet<>();
        for (DownloadIndex.Row r : fresh) {
            names.add(r.item.file);
            sig.append(r.item.file).append('|').append(r.state.kind).append('|').append(r.state.percent)
                .append('|').append(r.state.reason).append('|').append(r.size / 1024).append(';');
        }
        selected.retainAll(names);
        rows = fresh;
        String now = sig.toString() + installing;
        if (!now.equals(signature)) {
            signature = now;
            adapter.notifyDataSetChanged();
        }
        empty.setVisibility(rows.isEmpty() ? View.VISIBLE : View.GONE);
        space.setText(getString(R.string.dl_space, DownloadIndex.formatSize(DownloadIndex.totalSize(rows))));
        if (selecting && selected.isEmpty()) endSelecting(); else updateSelectionUi();
    }

    private void refreshNow() {
        worker.execute(() -> {
            List<DownloadIndex.Row> fresh = ModDownloads.rows(this);
            runOnUiThread(() -> show(fresh));
        });
    }

    private String stateText(DownloadIndex.State s) {
        switch (s.kind) {
            case DOWNLOADING:
                return s.percent < 0 ? getString(R.string.dl_state_downloading_unknown)
                    : getString(R.string.dl_state_downloading, s.percent);
            case WAITING_NETWORK: return getString(R.string.dl_state_waiting_network);
            case WAITING_WIFI: return getString(R.string.dl_state_waiting_wifi);
            case INSTALLED: return getString(R.string.dl_state_installed);
            case FAILED: return getString(R.string.dl_state_failed, failure(s.reason));
            default: return getString(R.string.dl_state_downloaded);
        }
    }

    private String failure(int reason) {
        if (reason == DownloadIndex.ERROR_INSUFFICIENT_SPACE) return getString(R.string.dl_fail_space);
        if (reason >= 100 && reason < 600) return getString(R.string.dl_fail_http, reason);
        if (reason == 1004 || reason == 1005 || reason == 1008) return getString(R.string.dl_fail_network);
        if (reason == 0) return getString(R.string.dl_fail_unknown);
        return getString(R.string.dl_fail_other, reason);
    }

    private final class Adapter extends BaseAdapter {
        @Override public int getCount() { return rows.size(); }
        @Override public Object getItem(int i) { return rows.get(i); }
        @Override public long getItemId(int i) { return i; }

        @Override
        public View getView(int position, View convert, ViewGroup parent) {
            DownloadIndex.Row row = rows.get(position);
            LinearLayout line = new LinearLayout(DownloadsActivity.this);
            line.setOrientation(LinearLayout.HORIZONTAL);
            line.setGravity(Gravity.CENTER_VERTICAL);
            line.setPadding(dp(12), dp(8), dp(12), dp(8));
            boolean picked = selected.contains(row.item.file);
            line.setBackgroundColor(picked ? Color.rgb(48, 64, 96) : (position % 2 == 0 ? Color.rgb(28, 28, 28) : Color.rgb(34, 34, 34)));

            LinearLayout info = new LinearLayout(DownloadsActivity.this);
            info.setOrientation(LinearLayout.VERTICAL);
            TextView name = new TextView(DownloadsActivity.this);
            name.setText(row.item.file);
            name.setTextColor(Color.WHITE);
            name.setTextSize(16);
            name.setSingleLine(true);
            name.setEllipsize(android.text.TextUtils.TruncateAt.MIDDLE);
            TextView detail = new TextView(DownloadsActivity.this);
            detail.setText(DownloadIndex.formatSize(row.size) + "  ·  " + stateText(row.state));
            detail.setTextColor(row.state.kind == DownloadIndex.Kind.FAILED ? Color.rgb(240, 120, 110)
                : row.state.kind == DownloadIndex.Kind.INSTALLED ? Color.rgb(130, 210, 140) : Color.LTGRAY);
            detail.setTextSize(13);
            info.addView(name);
            info.addView(detail);
            if (row.state.active()) {
                ProgressBar bar = new ProgressBar(DownloadsActivity.this, null, android.R.attr.progressBarStyleHorizontal);
                bar.setMax(100);
                bar.setIndeterminate(row.state.percent < 0 || row.state.kind != DownloadIndex.Kind.DOWNLOADING);
                if (!bar.isIndeterminate()) bar.setProgress(row.state.percent);
                info.addView(bar, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(8)));
            }
            line.addView(info, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

            if (!selecting) {
                if (row.state.active()) {
                    line.addView(button(R.string.dl_cancel, v -> remove(row)));
                } else {
                    if (row.state.kind != DownloadIndex.Kind.FAILED) {
                        Button install = button(R.string.dl_install, v -> install(row));
                        install.setEnabled(!installing);
                        line.addView(install);
                    }
                    line.addView(button(R.string.dl_delete, v -> askDelete(row)));
                }
            }
            line.setOnClickListener(v -> {
                if (selecting) toggle(row);
            });
            line.setOnLongClickListener(v -> {
                if (!selecting) {
                    selecting = true;
                    selected.clear();
                }
                toggle(row);
                return true;
            });
            return line;
        }

        private Button button(int label, View.OnClickListener click) {
            Button b = new Button(DownloadsActivity.this);
            b.setText(label);
            b.setMinimumHeight(dp(48));
            b.setOnClickListener(click);
            return b;
        }
    }

    // --- selection ---

    private void toggle(DownloadIndex.Row row) {
        if (!selected.remove(row.item.file)) selected.add(row.item.file);
        signature = "";
        adapter.notifyDataSetChanged();
        if (selected.isEmpty()) endSelecting(); else updateSelectionUi();
    }

    private void endSelecting() {
        selecting = false;
        selected.clear();
        signature = "";
        adapter.notifyDataSetChanged();
        updateSelectionUi();
    }

    private void updateSelectionUi() {
        deleteSelected.setVisibility(selecting ? View.VISIBLE : View.GONE);
        selectDone.setVisibility(selecting ? View.VISIBLE : View.GONE);
        deleteSelected.setText(getString(R.string.dl_delete_selected, selected.size()));
    }

    // --- actions ---

    private void remove(DownloadIndex.Row row) {
        worker.execute(() -> {
            ModDownloads.remove(this, row);
            refreshNow();
        });
    }

    private void askDelete(DownloadIndex.Row row) {
        new AlertDialog.Builder(this)
            .setMessage(getString(R.string.dl_delete_q, row.item.file))
            .setPositiveButton(R.string.dl_delete, (d, w) -> remove(row))
            .setNegativeButton(R.string.dl_cancel, null)
            .show();
    }

    private void askDeleteSelected() {
        final List<DownloadIndex.Row> victims = new ArrayList<>();
        for (DownloadIndex.Row r : rows) if (selected.contains(r.item.file)) victims.add(r);
        if (victims.isEmpty()) return;
        new AlertDialog.Builder(this)
            .setMessage(getString(R.string.dl_delete_many_q, victims.size()))
            .setPositiveButton(R.string.dl_delete, (d, w) -> {
                worker.execute(() -> {
                    for (DownloadIndex.Row r : victims) ModDownloads.remove(this, r);
                    runOnUiThread(this::endSelecting);
                    refreshNow();
                });
            })
            .setNegativeButton(R.string.dl_cancel, null)
            .show();
    }

    private void install(DownloadIndex.Row row) {
        if (installing) return;
        String lower = row.item.file.toLowerCase(Locale.ROOT);
        if (lower.endsWith(".7z") || lower.endsWith(".rar")) {
            info(getString(R.string.dl_unsupported, row.item.file));
            return;
        }
        if (DustlightHd.classify(row.item.file).kind == DustlightHd.Kind.ENGINE) {
            // The Dustlight engine zip is the Windows exe: nothing to install here.
            info(getString(R.string.dl_hd_engine, row.item.file));
            return;
        }
        DustlightAuto.forget(row.item.file);
        installing = true;
        signature = "";
        adapter.notifyDataSetChanged();
        AlertDialog busy = new AlertDialog.Builder(this)
            .setMessage(getString(R.string.nexus_installing, row.item.title))
            .setCancelable(false).show();
        new Thread(() -> {
            String problem = null;
            String ok = null;
            try {
                if (!waitForGameToClose()) throw new IOException(getString(R.string.import_game_running));
                File file = new File(ModDownloads.folder(this), row.item.file);
                File gameDir = GameRoots.activeGameRoot(this);
                boolean rpu = DownloadIndex.RPU.equals(row.item.source);
                String modName = rpu && row.item.title != null && !row.item.title.trim().isEmpty()
                    ? row.item.title : row.item.file;
                // RPU never goes into Vanilla: it gets a profile of its own.
                ModProfile.Created profile = rpu && gameDir != null && ModProfile.isVanilla(gameDir)
                    ? ModProfile.createAndActivate(gameDir, "RPU") : null;
                if (!rpu && HdPackInstall.isHdPack(this, Uri.fromFile(file))) {
                    // Dustlight HD art pack: its own installer (files/hd/<tier>).
                    synchronized (DustlightAuto.lock()) {
                        DustlightHd.Status before = DustlightHd.status(gameDir);
                        HdPackInstall.Result hd = HdPackInstall.install(this, Uri.fromFile(file), row.item.file, gameDir,
                            new HdPackInstall.Progress() {
                                public void update(long done, long total, String path) { }
                                public boolean isCancelled() { return false; }
                            });
                        ok = getString(R.string.hd_done, hd.tier, hd.packs);
                        if (DustlightHd.enableIfNewlyComplete(gameDir, before)) ok += "\n" + getString(R.string.dl_hd_ready);
                    }
                } else {
                    ModInstall.Result result;
                    try {
                        result = ModInstall.install(this, Uri.fromFile(file), modName,
                            gameDir, new ModInstall.Progress() {
                                public void update(long done, long total, String path) { }
                                public boolean isCancelled() { return false; }
                            });
                    } catch (IOException | RuntimeException e) {
                        if (profile != null) ModProfile.rollback(gameDir, profile);
                        throw e;
                    }
                    if (profile != null) ok = getString(R.string.rpu_installed_profile, result.id, profile.name);
                    else if (rpu) ok = getString(R.string.rpu_installed, result.id);
                    else ok = getString(R.string.mod_installed, result.id);
                }
                ModDownloads.markInstalled(this, row.item.file);
            } catch (HdPackInstall.Problem p) {
                problem = p.message == R.string.mod_no_space ? getString(p.message, p.neededMb) : getString(p.message);
            } catch (HdPackInstall.Invalid e) {
                problem = getString(R.string.hd_invalid, e.getMessage());
            } catch (ModInstall.Problem p) {
                problem = p.message == R.string.mod_no_space ? getString(p.message, p.neededMb) : getString(p.message);
            } catch (NativeArchive.Failure f) {
                problem = getString(R.string.mod_unreadable);
            } catch (IOException | RuntimeException e) {
                Log.w(TAG, "install failed", e);
                problem = getString(R.string.import_failed, e.getMessage());
            }
            final String text = problem != null
                ? getString(R.string.nexus_install_failed, row.item.title, problem) : ok;
            runOnUiThread(() -> {
                installing = false;
                busy.dismiss();
                info(text);
                refreshNow();
            });
        }, "downloads-install").start();
    }

    private void info(String text) {
        new AlertDialog.Builder(this).setMessage(text).setPositiveButton(android.R.string.ok, null).show();
    }

    // The game (main process) is closed when this screen is opened from the
    // Mods screen; if it still runs (opened from a notification), wait a bit.
    private boolean waitForGameToClose() {
        ActivityManager manager = (ActivityManager) getSystemService(ACTIVITY_SERVICE);
        for (int attempt = 0; attempt < 30; attempt++) {
            boolean running = false;
            List<ActivityManager.RunningAppProcessInfo> processes = manager.getRunningAppProcesses();
            if (processes != null) {
                for (ActivityManager.RunningAppProcessInfo p : processes) {
                    running |= p.processName.equals(getPackageName());
                }
            }
            if (!running) return true;
            try {
                Thread.sleep(100);
            } catch (InterruptedException e) {
                return false;
            }
        }
        return false;
    }
}
