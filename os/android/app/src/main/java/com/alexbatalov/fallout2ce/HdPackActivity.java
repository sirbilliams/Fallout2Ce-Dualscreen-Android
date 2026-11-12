package com.alexbatalov.fallout2ce;

import android.app.AlertDialog;
import android.content.Intent;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.util.Log;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.File;
import java.io.IOException;
import java.util.Locale;

// "HD pack" screen (Game files > HD pack in the game's Settings): the HD
// toggle and lighting quality, the installed tier and its size, install from
// a zip on the phone, Get HD pack (Nexus mod 193 in Browse Nexus) and
// Remove HD pack. Same process as the Nexus browser, so it outlives the game.
public class HdPackActivity extends ImmersiveActivity {
    static final String NEXUS_URL = "https://www.nexusmods.com/fallout2/mods/193";
    private static final String TAG = "HdPackActivity";
    private static final int PICK = 1;

    private File game;
    private TextView status;
    private TextView profile;
    private boolean refreshing;
    private CheckBox enabled;
    private Button quality;
    private Button remove;
    private Button zips;
    private boolean busy;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        game = getExternalFilesDir(null);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(Color.rgb(16, 16, 16));
        root.setPadding(32, 24, 32, 24);

        TextView title = new TextView(this);
        title.setText(R.string.hd_title);
        title.setTextColor(Color.WHITE);
        title.setTextSize(20);
        root.addView(title);

        status = new TextView(this);
        status.setTextColor(Color.rgb(200, 200, 120));
        status.setPadding(0, 16, 0, 16);
        root.addView(status);

        profile = new TextView(this);
        profile.setTextColor(Color.rgb(140, 190, 230));
        profile.setPadding(0, 0, 0, 8);
        root.addView(profile);

        enabled = new CheckBox(this);
        enabled.setText(R.string.hd_enable);
        enabled.setTextColor(Color.WHITE);
        enabled.setOnCheckedChangeListener((b, on) -> {
            if (refreshing) return;
            try {
                HdSettings.setEnabled(game, on);
            } catch (IOException e) {
                Log.w(TAG, "config", e);
            }
        });
        root.addView(enabled);

        quality = new Button(this);
        quality.setOnClickListener(v -> chooseQuality());
        root.addView(quality);

        Button install = new Button(this);
        install.setText(R.string.hd_install);
        install.setOnClickListener(v -> {
            Intent pick = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            pick.addCategory(Intent.CATEGORY_OPENABLE);
            pick.setType("application/zip");
            startActivityForResult(pick, PICK);
        });
        root.addView(install);

        Button get = new Button(this);
        get.setText(R.string.hd_get);
        get.setOnClickListener(v -> startActivity(new Intent(this, NexusActivity.class)
            .putExtra(NexusActivity.EXTRA_URL, NEXUS_URL)));
        root.addView(get);

        Button dustlight = new Button(this);
        dustlight.setText(R.string.hd_get_dustlight);
        dustlight.setOnClickListener(v -> startActivity(new Intent(this, NexusActivity.class)
            .putExtra(NexusActivity.EXTRA_URL, DustlightHd.NEXUS_FILES)
            .putExtra(NexusActivity.EXTRA_GUIDE, DustlightHd.GUIDE)));
        root.addView(dustlight, root.indexOfChild(get));

        remove = new Button(this);
        remove.setText(R.string.hd_remove);
        remove.setOnClickListener(v -> confirmRemove());
        root.addView(remove);

        zips = new Button(this);
        zips.setVisibility(android.view.View.GONE);
        root.addView(zips);

        Button back = new Button(this);
        back.setText(R.string.nexus_done);
        back.setOnClickListener(v -> leave());
        root.addView(back, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        setContentView(root);
        refresh();
    }

    @Override
    protected void onResume() {
        super.onResume();
        refresh();
        // Parts that finished while this screen was closed; tier and zip questions.
        new Thread(() -> {
            DustlightAuto.Report report;
            try {
                DustlightAuto.processPending(this);
                report = DustlightAuto.report(this);
            } catch (RuntimeException e) {
                return;
            }
            final DustlightAuto.Report r = report;
            runOnUiThread(() -> {
                if (isFinishing()) return;
                refresh();
                if (r.blockedTier != 0) {
                    DustlightBanner.askSwitch(this, r.blockedTier, r.status.tier, this::refresh);
                }
                if (r.deletableCount > 0) {
                    zips.setText(getString(R.string.hd_guide_delete, DownloadIndex.formatSize(r.deletableBytes)));
                    zips.setOnClickListener(v -> DustlightBanner.askDelete(this, r.deletableBytes, () -> zips.setVisibility(android.view.View.GONE)));
                    zips.setVisibility(android.view.View.VISIBLE);
                } else {
                    zips.setVisibility(android.view.View.GONE);
                }
            });
        }, "hd-resume").start();
    }

    private void refresh() {
        int tier = HdPackInstall.installedTier(game);
        if (tier == 0) {
            status.setText(R.string.hd_none);
        } else {
            long mb = HdPackInstall.sizeOf(HdPackInstall.tierDir(game, tier)) / 1048576;
            String more = "";
            DustlightHd.Status s = DustlightHd.status(game);
            if (s.complete()) more = "\n" + getString(R.string.hd_guide_ready);
            else if (s.totalPacks >= 0) {
                more = "\n" + getString(R.string.hd_guide_packs, s.tier, s.packs, s.totalPacks)
                    + "\n" + getString(R.string.hd_guide_more, s.moreParts());
            }
            status.setText(getString(R.string.hd_installed, tier, mb) + "\n" + getString(R.string.hd_shared, mb) + more);
        }
        remove.setEnabled(tier != 0);
        String active = HdSettings.activeProfile(game);
        profile.setText(getString(R.string.hd_profile, active.isEmpty() ? getString(R.string.hd_profile_vanilla) : active));
        refreshing = true;
        enabled.setChecked(HdSettings.isEnabled(game));
        refreshing = false;
        quality.setText(getString(R.string.hd_quality, HdSettings.quality(game)));
    }

    private void chooseQuality() {
        final String[] names = HdSettings.QUALITIES;
        int current = 3;
        String now = HdSettings.quality(game);
        for (int i = 0; i < names.length; i++) if (names[i].equals(now)) current = i;
        new AlertDialog.Builder(this).setTitle(R.string.hd_quality_title)
            .setSingleChoiceItems(names, current, (d, which) -> {
                try {
                    HdSettings.setQuality(game, names[which]);
                } catch (IOException e) {
                    Log.w(TAG, "config", e);
                }
                d.dismiss();
                refresh();
            }).show();
    }

    private void confirmRemove() {
        final int tier = HdPackInstall.installedTier(game);
        if (tier == 0) return;
        new AlertDialog.Builder(this).setMessage(getString(R.string.hd_remove_q, tier))
            .setPositiveButton(R.string.nexus_delete, (d, w) -> {
                HdPackInstall.remove(game, tier);
                refresh();
            }).setNegativeButton(android.R.string.cancel, null).show();
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != PICK || result != RESULT_OK || data == null || data.getData() == null || busy) return;
        final Uri uri = data.getData();
        busy = true;
        AlertDialog dialog = new AlertDialog.Builder(this).setMessage(R.string.hd_installing).setCancelable(false).show();
        new Thread(() -> {
            String text;
            try {
                DustlightHd.Status before = DustlightHd.status(game);
                HdPackInstall.Result r = HdPackInstall.install(this, uri, uri.getLastPathSegment(), game,
                    new HdPackInstall.Progress() {
                        public void update(long done, long total, String path) { }
                        public boolean isCancelled() { return false; }
                    });
                text = getString(R.string.hd_done, r.tier, r.packs);
                if (DustlightHd.enableIfNewlyComplete(game, before)) text += "\n" + getString(R.string.dl_hd_ready);
            } catch (HdPackInstall.Problem p) {
                text = p.message == R.string.mod_no_space ? getString(p.message, p.neededMb) : getString(p.message);
            } catch (HdPackInstall.Invalid e) {
                text = getString(R.string.hd_invalid, e.getMessage());
            } catch (IOException | RuntimeException e) {
                Log.w(TAG, "install failed", e);
                text = getString(R.string.import_failed, String.valueOf(e.getMessage()));
            }
            final String shown = text;
            runOnUiThread(() -> {
                busy = false;
                dialog.dismiss();
                new AlertDialog.Builder(this).setMessage(shown).setPositiveButton(android.R.string.ok, null).show();
                refresh();
            });
        }, "hd-install").start();
    }

    @Override
    public void onBackPressed() {
        leave();
    }

    private void leave() {
        startActivity(new Intent(this, MainActivity.class));
        finish();
    }
}
