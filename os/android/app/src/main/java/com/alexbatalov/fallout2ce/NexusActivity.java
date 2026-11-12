package com.alexbatalov.fallout2ce;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.os.Bundle;
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


// "Browse Nexus" in the Mods screen: a WebView on Nexus Mods (the player
// signs in there; no credentials are read or stored). Manual-download files
// are queued in ModDownloads (system DownloadManager, with the WebView's
// cookies); the Downloads screen lists and installs them.
public class NexusActivity extends Activity {
    private static final String START = "https://www.nexusmods.com/fallout2/mods/";

    private WebView web;
    private TextView status;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

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
        Button downloads = new Button(this);
        downloads.setText(R.string.dl_title);
        downloads.setOnClickListener(v -> startActivity(new Intent(this, DownloadsActivity.class)));
        bar.addView(downloads);
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
                    download(url, null, null, view.getTitle());
                    return true;
                }
                return false;
            }
        });
        web.setDownloadListener((url, userAgent, contentDisposition, mimeType, length) ->
            download(url, contentDisposition, mimeType, web.getTitle()));
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

    // Hands the file to the system download manager: it goes on in the
    // background (notification bar) and the Downloads screen lists it.
    private void download(String url, String contentDisposition, String mimeType, String pageTitle) {
        if (mimeType != null && mimeType.toLowerCase(java.util.Locale.ROOT).startsWith("text/html")) return;
        String file = NexusDownloads.fileName(contentDisposition, url);
        String title = NexusDownloads.modName(pageTitle, file);
        long id = ModDownloads.enqueue(this, url, file, title, CookieManager.getInstance().getCookie(url),
            web.getSettings().getUserAgentString(), web.getUrl(), false, DownloadIndex.NEXUS);
        setStatus(id >= 0 ? getString(R.string.dl_started, title) : getString(R.string.dl_start_failed));
    }

    private void leave() {
        startActivity(new Intent(this, MainActivity.class));
        finish();
    }
}
