package com.alexbatalov.fallout2ce;

import android.app.Activity;
import android.app.ActivityManager;
import android.app.AlertDialog;
import android.content.Intent;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.webkit.CookieManager;
import android.webkit.WebResourceRequest;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.ArrayList;
import java.util.List;

// "Browse Nexus" in the Mods screen: a WebView on Nexus Mods (the player
// signs in there; no credentials are read or stored). Manual-download files
// are fetched with the WebView's cookies into mod-downloads/, and on leaving
// each is offered for install through ModInstall (the zip path of Mods).
public class NexusActivity extends Activity {
    private static final String TAG = "NexusActivity";
    private static final String START = "https://www.nexusmods.com/fallout2/mods/";
    static final String FOLDER = "mod-downloads";

    private WebView web;
    private TextView status;
    private File folder;
    private boolean leaving;
    private int active;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        folder = new File(getExternalFilesDir(null), FOLDER);
        folder.mkdirs();

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);

        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        bar.setBackgroundColor(Color.rgb(24, 24, 24));
        Button back = new Button(this);
        back.setText(R.string.nexus_back);
        back.setOnClickListener(v -> onBackPressed());
        TextView title = new TextView(this);
        title.setText(R.string.nexus_title);
        title.setTextColor(Color.WHITE);
        title.setPadding(24, 0, 24, 0);
        status = new TextView(this);
        status.setTextColor(Color.rgb(200, 200, 120));
        status.setSingleLine(true);
        status.setEllipsize(android.text.TextUtils.TruncateAt.END);
        Button done = new Button(this);
        done.setText(R.string.nexus_done);
        done.setOnClickListener(v -> leave());
        bar.addView(back);
        bar.addView(title);
        bar.addView(status, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        bar.addView(done);
        root.addView(bar, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        web = new WebView(this);
        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        CookieManager cookies = CookieManager.getInstance();
        cookies.setAcceptCookie(true);
        cookies.setAcceptThirdPartyCookies(web, true);
        web.setWebViewClient(new WebViewClient() {
            @Override
            public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
                String url = request.getUrl().toString();
                String scheme = request.getUrl().getScheme();
                if (!"http".equals(scheme) && !"https".equals(scheme)) return true;
                // A redirect to the CDN archive that DownloadListener never sees.
                if (NexusDownloads.isArchiveUrl(url)) {
                    download(url, null, view.getTitle());
                    return true;
                }
                return false;
            }
        });
        web.setDownloadListener((url, userAgent, contentDisposition, mimeType, length) ->
            download(url, contentDisposition, web.getTitle()));
        root.addView(web, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
        setContentView(root);

        if (savedInstanceState != null) web.restoreState(savedInstanceState);
        else web.loadUrl(START);
    }

    @Override
    protected void onSaveInstanceState(Bundle out) {
        super.onSaveInstanceState(out);
        web.saveState(out);
    }

    @Override
    public void onBackPressed() {
        if (web.canGoBack()) web.goBack();
        else leave();
    }

    @Override
    protected void onPause() {
        super.onPause();
        CookieManager.getInstance().flush();
    }

    @Override
    protected void onDestroy() {
        web.destroy();
        super.onDestroy();
    }

    private void setStatus(String text) {
        runOnUiThread(() -> status.setText(text));
    }

    private void download(String url, String contentDisposition, String pageTitle) {
        final String agent = web.getSettings().getUserAgentString();
        final String referer = web.getUrl();
        synchronized (this) { active++; }
        new Thread(() -> {
            String shown = NexusDownloads.fileName(contentDisposition, url);
            try {
                fetch(url, contentDisposition, agent, referer, pageTitle);
            } catch (IOException | RuntimeException e) {
                Log.w(TAG, "download failed", e);
                setStatus(getString(R.string.nexus_download_failed, shown + " (" + e.getMessage() + ")"));
            } finally {
                synchronized (NexusActivity.this) { active--; }
            }
        }, "nexus-download").start();
    }

    // Manual redirects so each hop (CDN host) gets its own cookies.
    private void fetch(String start, String contentDisposition, String agent, String referer, String pageTitle) throws IOException {
        String url = start;
        for (int hop = 0; hop < 8; hop++) {
            HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
            c.setInstanceFollowRedirects(false);
            c.setConnectTimeout(20000);
            c.setReadTimeout(30000);
            c.setRequestProperty("User-Agent", agent);
            if (referer != null) c.setRequestProperty("Referer", referer);
            String cookie = CookieManager.getInstance().getCookie(url);
            if (cookie != null) c.setRequestProperty("Cookie", cookie);
            int code = c.getResponseCode();
            if (code >= 300 && code < 400) {
                String location = c.getHeaderField("Location");
                c.disconnect();
                if (location == null) throw new IOException("redirect without location");
                url = new URL(new URL(url), location).toString();
                continue;
            }
            if (code != 200) {
                c.disconnect();
                throw new IOException("HTTP " + code);
            }
            String disposition = c.getHeaderField("Content-Disposition");
            String type = c.getContentType();
            if (type != null && type.toLowerCase().startsWith("text/html")) {
                c.disconnect();
                throw new IOException("not a file");
            }
            String wanted = NexusDownloads.fileName(disposition != null ? disposition : contentDisposition, url);
            List<String> existing = new ArrayList<>();
            String[] names = folder.list();
            if (names != null) for (String n : names) existing.add(n);
            String file = NexusDownloads.uniqueFile(existing, wanted);
            File part = new File(folder, file + ".part");
            long total = c.getContentLengthLong();
            long done = 0;
            try (InputStream in = c.getInputStream(); FileOutputStream out = new FileOutputStream(part)) {
                byte[] buffer = new byte[64 * 1024];
                long lastShown = 0;
                for (int n; (n = in.read(buffer)) > 0; ) {
                    out.write(buffer, 0, n);
                    done += n;
                    long now = System.currentTimeMillis();
                    if (now - lastShown > 250) {
                        lastShown = now;
                        String progress = total > 0 ? (done * 100 / total) + "%" : (done >> 10) + " KB";
                        setStatus(getString(R.string.nexus_downloading, file, progress));
                    }
                }
            } catch (IOException e) {
                part.delete();
                throw e;
            } finally {
                c.disconnect();
            }
            File target = new File(folder, file);
            if (!part.renameTo(target)) {
                part.delete();
                throw new IOException("can't save " + file);
            }
            synchronized (this) {
                NexusDownloads.add(folder, file, NexusDownloads.modName(pageTitle, file));
            }
            setStatus(getString(R.string.nexus_downloaded, file));
            return;
        }
        throw new IOException("too many redirects");
    }

    private void leave() {
        if (leaving) return;
        leaving = true;
        List<NexusDownloads.Entry> pending;
        try {
            synchronized (this) { pending = NexusDownloads.pending(folder); }
        } catch (IOException e) {
            pending = new ArrayList<>();
        }
        askNext(pending, 0);
    }

    private void askNext(List<NexusDownloads.Entry> pending, int index) {
        if (index >= pending.size()) {
            startActivity(new Intent(this, MainActivity.class));
            finish();
            return;
        }
        NexusDownloads.Entry entry = pending.get(index);
        new AlertDialog.Builder(this)
            .setTitle(getString(R.string.nexus_install_q, entry.title))
            .setMessage(entry.file)
            .setCancelable(false)
            .setPositiveButton(R.string.nexus_install, (d, w) -> install(pending, index))
            .setNeutralButton(R.string.nexus_later, (d, w) -> askNext(pending, index + 1))
            .setNegativeButton(R.string.nexus_delete, (d, w) -> {
                try {
                    synchronized (this) {
                        new File(folder, entry.file).delete();
                        NexusDownloads.remove(folder, entry.file);
                    }
                } catch (IOException e) {
                    Log.w(TAG, "index update failed", e);
                }
                askNext(pending, index + 1);
            })
            .show();
    }

    private void install(List<NexusDownloads.Entry> pending, int index) {
        NexusDownloads.Entry entry = pending.get(index);
        setStatus(getString(R.string.nexus_installing, entry.title));
        AlertDialog busy = new AlertDialog.Builder(this)
            .setMessage(getString(R.string.nexus_installing, entry.title))
            .setCancelable(false).show();
        new Thread(() -> {
            String problem = null;
            String ok = null;
            try {
                if (!waitForGameToClose()) throw new IOException(getString(R.string.import_game_running));
                File file = new File(folder, entry.file);
                ModInstall.Result result = ModInstall.install(this, Uri.fromFile(file), entry.file,
                    getExternalFilesDir(null), new ModInstall.Progress() {
                        public void update(long done, long total, String path) { }
                        public boolean isCancelled() { return false; }
                    });
                ok = getString(R.string.mod_installed, result.id);
                synchronized (this) {
                    file.delete();
                    NexusDownloads.remove(folder, entry.file);
                }
            } catch (ModInstall.Problem p) {
                problem = p.message == R.string.mod_no_space ? getString(p.message, p.neededMb) : getString(p.message);
            } catch (NativeArchive.Failure f) {
                problem = getString(R.string.mod_unreadable);
            } catch (IOException | RuntimeException e) {
                Log.w(TAG, "install failed", e);
                problem = getString(R.string.import_failed, e.getMessage());
            }
            final String message = problem != null
                ? getString(R.string.nexus_install_failed, entry.title, problem) : ok;
            runOnUiThread(() -> {
                busy.dismiss();
                new AlertDialog.Builder(this).setMessage(message).setCancelable(false)
                    .setPositiveButton(android.R.string.ok, (d, w) -> askNext(pending, index + 1)).show();
            });
        }, "nexus-install").start();
    }

    // The game (main process) has been closed by openGameFiles; wait it out.
    private boolean waitForGameToClose() {
        ActivityManager manager = (ActivityManager) getSystemService(ACTIVITY_SERVICE);
        for (int attempt = 0; attempt < 150; attempt++) {
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
