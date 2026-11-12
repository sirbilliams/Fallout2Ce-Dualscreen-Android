package com.alexbatalov.fallout2ce;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.graphics.Color;
import android.os.Handler;
import android.os.Looper;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

// The Dustlight HD guide under the in-app Nexus browser (guide mode only):
// which files to download, a live checklist, "art packs installed X of Y".
// It sits BELOW the page (the page shrinks, nothing is overlaid, so the Nexus
// download buttons stay clickable) and its body is capped at about a third of
// the screen height; collapsed it is one line. Polls about every 1.5 s, and
// each poll also installs finished parts (DustlightAuto) that the download
// receiver could not (game running) or missed.
final class DustlightBanner extends LinearLayout {
    private static final int BODY_PERCENT = 34;

    private final Activity activity;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private final TextView summary;
    private final TextView arrow;
    private final LinearLayout list;
    private final Capped body;
    private boolean expanded = true;
    private boolean polling;
    private boolean running;
    private boolean askedDelete;
    private boolean askedSwitch;

    private final Runnable poll = new Runnable() {
        @Override public void run() {
            if (!polling) return;
            if (!running) {
                running = true;
                worker.execute(() -> {
                    DustlightAuto.Report report = null;
                    try {
                        DustlightAuto.processPending(activity);
                        report = DustlightAuto.report(activity);
                    } catch (RuntimeException ignored) {
                        // The next poll tries again.
                    }
                    final DustlightAuto.Report shown = report;
                    handler.post(() -> {
                        running = false;
                        if (shown != null && polling) show(shown);
                    });
                });
            }
            handler.postDelayed(this, 1500);
        }
    };

    // ScrollView that never grows past [cap] pixels.
    private static final class Capped extends ScrollView {
        private final int cap;

        Capped(Context context, int cap) {
            super(context);
            this.cap = cap;
        }

        @Override
        protected void onMeasure(int widthSpec, int heightSpec) {
            super.onMeasure(widthSpec, MeasureSpec.makeMeasureSpec(cap, MeasureSpec.AT_MOST));
        }
    }

    DustlightBanner(Activity activity) {
        super(activity);
        this.activity = activity;
        setOrientation(VERTICAL);
        setBackgroundColor(Color.rgb(24, 24, 24));

        View line = new View(activity);
        line.setBackgroundColor(Color.rgb(90, 80, 40));
        addView(line, new LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 2));

        LinearLayout header = new LinearLayout(activity);
        header.setGravity(Gravity.CENTER_VERTICAL);
        header.setPadding(dp(12), dp(4), dp(12), dp(4));
        TextView title = new TextView(activity);
        title.setText(R.string.hd_guide_title);
        title.setTextColor(Color.WHITE);
        title.setTextSize(15);
        title.setPadding(0, 0, dp(16), 0);
        summary = new TextView(activity);
        summary.setTextColor(Color.rgb(200, 200, 120));
        summary.setSingleLine(true);
        summary.setEllipsize(android.text.TextUtils.TruncateAt.END);
        arrow = new TextView(activity);
        arrow.setTextColor(Color.WHITE);
        arrow.setTextSize(18);
        header.addView(title);
        header.addView(summary, new LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        header.addView(arrow);
        header.setOnClickListener(v -> setExpanded(!expanded));
        addView(header, new LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        int height = activity.getResources().getDisplayMetrics().heightPixels;
        body = new Capped(activity, height * BODY_PERCENT / 100);
        list = new LinearLayout(activity);
        list.setOrientation(VERTICAL);
        list.setPadding(dp(12), dp(2), dp(12), dp(8));
        body.addView(list, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        addView(body, new LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        setExpanded(true);
        summary.setText(R.string.hd_guide_none_short);
    }

    private int dp(int v) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics());
    }

    boolean isExpanded() {
        return expanded;
    }

    void setExpanded(boolean on) {
        expanded = on;
        body.setVisibility(on ? VISIBLE : GONE);
        arrow.setText(on ? "▾" : "▴");
    }

    void start() {
        polling = true;
        handler.post(poll);
    }

    void stop() {
        polling = false;
        handler.removeCallbacks(poll);
    }

    void destroy() {
        stop();
        worker.shutdownNow();
    }

    private TextView text(String s, int color, int sp) {
        TextView t = new TextView(activity);
        t.setText(s);
        t.setTextColor(color);
        t.setTextSize(sp);
        t.setPadding(0, dp(2), 0, dp(2));
        return t;
    }

    static String summaryLine(Context context, DustlightAuto.Report r) {
        DustlightHd.Status s = r.status;
        if (r.working != null) return context.getString(R.string.hd_guide_installing, r.working, r.workingPercent);
        if (s.complete()) return context.getString(R.string.hd_guide_ready);
        if (s.tier == 0) return context.getString(r.items.isEmpty() ? R.string.hd_guide_none_short : R.string.hd_guide_waiting);
        if (s.totalPacks < 0) return context.getString(R.string.hd_guide_packs_unknown, s.tier, s.packs);
        return context.getString(R.string.hd_guide_packs, s.tier, s.packs, s.totalPacks)
            + " - " + context.getString(R.string.hd_guide_more, s.moreParts());
    }

    static String stateOf(Context context, DustlightAuto.Item item) {
        if (item.name.kind == DustlightHd.Kind.ENGINE) return context.getString(R.string.hd_guide_state_engine);
        if (item.blocked) return context.getString(R.string.hd_guide_state_blocked, item.name.tier);
        if (item.failed) return context.getString(R.string.hd_guide_state_failed);
        switch (item.kind) {
            case DOWNLOADING: return context.getString(R.string.hd_guide_state_downloading, Math.max(0, item.percent));
            case WAITING_NETWORK: case WAITING_WIFI: return context.getString(R.string.hd_guide_state_waiting);
            case DOWNLOADED: return context.getString(R.string.hd_guide_state_downloaded);
            case INSTALLED: return context.getString(R.string.hd_guide_state_installed);
            default: return context.getString(R.string.hd_guide_state_failed);
        }
    }

    private void show(DustlightAuto.Report r) {
        summary.setText(summaryLine(activity, r));
        list.removeAllViews();
        list.addView(text(activity.getString(R.string.hd_guide_line1), Color.WHITE, 14));
        list.addView(text(activity.getString(R.string.hd_guide_line2), Color.LTGRAY, 14));
        DustlightHd.Status s = r.status;
        if (r.items.isEmpty() && s.tier == 0) {
            list.addView(text(activity.getString(R.string.hd_guide_none), Color.rgb(200, 200, 120), 13));
        }
        for (DustlightAuto.Item item : r.items) {
            boolean good = item.kind == DownloadIndex.Kind.INSTALLED && !item.blocked;
            String mark = item.name.kind == DustlightHd.Kind.ENGINE ? "– " : good ? "✓ " : "• ";
            list.addView(text(mark + item.file + " - " + stateOf(activity, item),
                good ? Color.rgb(140, 210, 140) : item.failed ? Color.rgb(230, 120, 100) : Color.LTGRAY, 13));
        }
        if (s.tier != 0) {
            list.addView(text(s.totalPacks < 0
                ? activity.getString(R.string.hd_guide_packs_unknown, s.tier, s.packs)
                : activity.getString(R.string.hd_guide_packs, s.tier, s.packs, s.totalPacks), Color.WHITE, 14));
            if (s.complete()) list.addView(text(activity.getString(R.string.hd_guide_ready), Color.rgb(140, 210, 140), 14));
            else if (s.totalPacks >= 0) list.addView(text(activity.getString(R.string.hd_guide_more, s.moreParts()), Color.rgb(200, 200, 120), 14));
            else list.addView(text(activity.getString(R.string.hd_guide_need_part1), Color.rgb(200, 200, 120), 14));
            if (s.fontFiles > 0) list.addView(text(activity.getString(R.string.hd_guide_fonts, s.tier, s.fontFiles), Color.LTGRAY, 13));
        }
        LinearLayout actions = new LinearLayout(activity);
        if (r.blockedTier != 0) {
            Button ask = new Button(activity);
            ask.setText(activity.getString(R.string.hd_guide_switch, r.blockedTier));
            ask.setOnClickListener(v -> askSwitch(activity, r.blockedTier, s.tier, null));
            actions.addView(ask);
        }
        if (r.deletableCount > 0) {
            Button del = new Button(activity);
            del.setText(activity.getString(R.string.hd_guide_delete, DownloadIndex.formatSize(r.deletableBytes)));
            del.setOnClickListener(v -> askDelete(activity, r.deletableBytes, null));
            actions.addView(del);
        }
        if (actions.getChildCount() > 0) list.addView(actions);
        if (r.blockedTier != 0 && !askedSwitch) {
            askedSwitch = true;
            askSwitch(activity, r.blockedTier, s.tier, null);
        }
        if (s.complete() && r.deletableCount > 0 && !askedDelete) {
            askedDelete = true;
            askDelete(activity, r.deletableBytes, null);
        }
    }

    // "Switch to Nx?" (tiers are never mixed); Switch installs the waiting parts.
    static void askSwitch(Activity activity, int incoming, int installed, Runnable after) {
        new AlertDialog.Builder(activity)
            .setMessage(activity.getString(R.string.hd_guide_switch_q, incoming, installed))
            .setPositiveButton(activity.getString(R.string.hd_guide_switch, incoming), (d, w) ->
                new Thread(() -> {
                    DustlightAuto.switchTier(activity, incoming);
                    if (after != null) activity.runOnUiThread(after);
                }, "hd-switch").start())
            .setNegativeButton(activity.getString(R.string.hd_guide_keep, installed), (d, w) -> {
                DustlightAuto.keepTier();
                if (after != null) after.run();
            }).show();
    }

    // "Delete the downloaded zips to free X?" Nothing is deleted without the tap.
    static void askDelete(Activity activity, long bytes, Runnable after) {
        new AlertDialog.Builder(activity)
            .setMessage(activity.getString(R.string.hd_guide_delete_q, DownloadIndex.formatSize(bytes)))
            .setNegativeButton(R.string.hd_guide_keep_zips, null)
            .setPositiveButton(R.string.nexus_delete, (d, w) ->
                new Thread(() -> {
                    DustlightAuto.deleteInstalledZips(activity);
                    if (after != null) activity.runOnUiThread(after);
                }, "hd-delete").start())
            .show();
    }
}
