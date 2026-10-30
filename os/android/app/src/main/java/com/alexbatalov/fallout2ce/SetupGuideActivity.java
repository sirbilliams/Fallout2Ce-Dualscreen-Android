package com.alexbatalov.fallout2ce;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Typeface;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.IOException;

// The first-run setup guide (shown once; again from the game's Settings >
// "Setup guide"): 1 the game's files (found, or the import screen), 2 the
// bottom screen's dashboard layout with a preview, 3 the theme with a
// preview, 4 a short tour of the controls (touch at the bottom, the gamepad's
// bindings). Skippable on every page. The choices go into fallout2.cfg's
// [touch] (dashboard_layout, bottom_theme: GuideSettings), the same keys the
// game's settings change; the mobile UI's colors and font as ImportActivity.
public class SetupGuideActivity extends Activity {
    private static final int PAGE_FILES = 0;
    private static final int PAGE_LAYOUT = 1;
    private static final int PAGE_THEME = 2;
    private static final int PAGE_CONTROLS = 3;
    private static final int PAGE_COUNT = 4;
    private static final String STATE_PAGE = "page";

    // Dashboard layouts (touch.dashboard_layout 0..2).
    private static final int[] LAYOUT_NAMES = { R.string.guide_layout_classic, R.string.guide_layout_map, R.string.guide_layout_columns };
    // Themes (touch.bottom_theme: 0 Fallout, 1 classic, 2 Pip-Boy 2000).
    private static final int[] THEME_NAMES = { R.string.guide_theme_fallout, R.string.guide_theme_classic, R.string.guide_theme_pipboy };

    private File game;
    private int page;
    private int layout;
    private int theme;
    private Typeface font;

    private TextView stepText;
    private TextView titleText;
    private LinearLayout content;
    private Button back;
    private Button skip;
    private Button next;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        game = getExternalFilesDir(null);
        layout = GuideSettings.getInt(game, "dashboard_layout", 0);
        theme = GuideSettings.getInt(game, "bottom_theme", 2);
        page = savedInstanceState != null ? savedInstanceState.getInt(STATE_PAGE) : PAGE_FILES;

        try {
            font = Typeface.createFromAsset(getAssets(), "ce.dat/fonts/PTM55FT.ttf");
        } catch (RuntimeException e) {
            font = Typeface.MONOSPACE;
        }

        setContentView(buildFrame());
        show();
    }

    @Override
    protected void onSaveInstanceState(Bundle outState) {
        super.onSaveInstanceState(outState);
        outState.putInt(STATE_PAGE, page);
    }

    // Back is the previous page, on the first one the same as Skip.
    @Override
    public void onBackPressed() {
        if (page > 0) {
            page--;
            show();
        } else {
            finishGuide();
        }
    }

    // Back from the import screen: the files may be there now.
    @Override
    protected void onResume() {
        super.onResume();
        if (content != null) {
            show();
        }
    }

    private int dp(float value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private TextView text(CharSequence value, int colorRes, float size) {
        TextView view = new TextView(this);
        view.setText(value);
        view.setTextColor(getColor(colorRes));
        view.setTextSize(size);
        view.setTypeface(font);
        view.setLineSpacing(dp(3), 1f);
        return view;
    }

    private Button button(int label, View.OnClickListener onClick) {
        Button button = new Button(this);
        button.setText(label);
        button.setTypeface(font);
        button.setTransformationMethod(null);
        button.setTextSize(15);
        button.setTextColor(getColor(R.color.import_button_text));
        button.setBackgroundResource(R.drawable.import_button);
        button.setOnClickListener(onClick);
        return button;
    }

    private View buildFrame() {
        LinearLayout root = new LinearLayout(this);
        root.setBackgroundColor(getColor(R.color.import_background));
        root.setGravity(Gravity.CENTER);
        root.setPadding(dp(16), dp(16), dp(16), dp(16));

        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setBackgroundResource(R.drawable.import_panel);
        panel.setPadding(dp(20), dp(20), dp(20), dp(20));
        root.addView(panel, new LinearLayout.LayoutParams(dp(640), ViewGroup.LayoutParams.MATCH_PARENT));

        stepText = text("", R.color.import_text_dim, 13);
        panel.addView(stepText);
        titleText = text("", R.color.import_accent, 22);
        panel.addView(titleText);

        ScrollView scroll = new ScrollView(this);
        scroll.setFadingEdgeLength(0);
        content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        scroll.addView(content, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        LinearLayout.LayoutParams scrollParams = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f);
        scrollParams.topMargin = dp(10);
        panel.addView(scroll, scrollParams);

        LinearLayout buttons = new LinearLayout(this);
        buttons.setOrientation(LinearLayout.HORIZONTAL);
        back = button(R.string.guide_back, v -> onBackPressed());
        skip = button(R.string.guide_skip, v -> finishGuide());
        next = button(R.string.guide_next, v -> {
            if (page < PAGE_COUNT - 1) {
                page++;
                show();
            } else {
                finishGuide();
            }
        });
        for (Button button : new Button[] { back, skip, next }) {
            LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(0, dp(48), 1f);
            params.setMarginEnd(button == next ? 0 : dp(10));
            buttons.addView(button, params);
        }
        LinearLayout.LayoutParams buttonsParams = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        buttonsParams.topMargin = dp(16);
        panel.addView(buttons, buttonsParams);
        return root;
    }

    private void show() {
        content.removeAllViews();
        stepText.setText(getString(R.string.guide_step, page + 1, PAGE_COUNT));
        back.setVisibility(page > 0 ? View.VISIBLE : View.INVISIBLE);
        next.setText(page == PAGE_COUNT - 1 ? R.string.guide_done : R.string.guide_next);

        switch (page) {
            case PAGE_FILES:
                showFiles();
                break;
            case PAGE_LAYOUT:
                showChoices(R.string.guide_layout_title, R.string.guide_layout_text, LAYOUT_NAMES, layout, true);
                break;
            case PAGE_THEME:
                showChoices(R.string.guide_theme_title, R.string.guide_theme_text, THEME_NAMES, theme, false);
                break;
            default:
                showControls();
                break;
        }
    }

    private void showFiles() {
        titleText.setText(R.string.guide_files_title);
        boolean installed = GameImport.isInstalled(game);
        content.addView(text(getString(installed ? R.string.guide_files_found : R.string.guide_files_missing),
            installed ? R.color.import_text : R.color.import_warning, 15));

        Button choose = button(installed ? R.string.guide_files_change : R.string.guide_files_choose, v -> {
            startActivity(new Intent(this, ImportActivity.class));
            // The import starts the game when done; the guide (not marked
            // done) comes first then.
        });
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(48));
        params.topMargin = dp(16);
        content.addView(choose, params);
        next.setEnabled(true);
    }

    // [names]: the options' labels; previews of the layouts or of the themes.
    private void showChoices(int title, int body, int[] names, int current, boolean layouts) {
        titleText.setText(title);
        content.addView(text(getString(body), R.color.import_text, 15));

        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        LinearLayout.LayoutParams rowParams = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        rowParams.topMargin = dp(14);
        content.addView(row, rowParams);

        final GuidePreviewView[] previews = new GuidePreviewView[names.length];
        for (int index = 0; index < names.length; index++) {
            final int option = index;
            LinearLayout cell = new LinearLayout(this);
            cell.setOrientation(LinearLayout.VERTICAL);
            cell.setClickable(true);

            GuidePreviewView preview = new GuidePreviewView(this);
            previews[index] = preview;
            cell.addView(preview, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
            TextView label = text(getString(names[index]), R.color.import_text, 14);
            label.setGravity(Gravity.CENTER);
            label.setPadding(0, dp(6), 0, 0);
            cell.addView(label);

            cell.setOnClickListener(v -> {
                if (layouts) {
                    layout = option;
                    save("dashboard_layout", option);
                } else {
                    theme = option;
                    save("bottom_theme", option);
                }
                refreshPreviews(previews, layouts);
            });
            LinearLayout.LayoutParams cellParams = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
            cellParams.setMarginEnd(index < names.length - 1 ? dp(10) : 0);
            row.addView(cell, cellParams);
        }
        refreshPreviews(previews, layouts);
    }

    // Layouts are previewed in the chosen theme, themes in the chosen layout.
    private void refreshPreviews(GuidePreviewView[] previews, boolean layouts) {
        for (int index = 0; index < previews.length; index++) {
            if (layouts) {
                previews[index].set(index, theme, index == layout);
            } else {
                previews[index].set(layout, index, index == theme);
            }
        }
    }

    private void save(String key, int value) {
        try {
            GuideSettings.setInt(game, key, value);
        } catch (IOException e) {
            e.printStackTrace();
        }
    }

    private void showControls() {
        titleText.setText(R.string.guide_controls_title);
        addControl(R.string.guide_controls_touch_title, R.string.guide_controls_touch);
        addControl(R.string.guide_controls_pad_title, R.string.guide_controls_pad);
        addControl(R.string.guide_controls_again_title, R.string.guide_controls_again);
    }

    private void addControl(int title, int body) {
        TextView heading = text(getString(title), R.color.import_accent, 15);
        heading.setPadding(0, dp(10), 0, dp(2));
        content.addView(heading);
        content.addView(text(getString(body), R.color.import_text, 14));
    }

    // Done or skipped: not shown again on its own; the game (the import
    // screen first, without the files).
    private void finishGuide() {
        GuideSettings.markDone(game);
        startActivity(new Intent(this, MainActivity.class));
        finish();
    }
}
