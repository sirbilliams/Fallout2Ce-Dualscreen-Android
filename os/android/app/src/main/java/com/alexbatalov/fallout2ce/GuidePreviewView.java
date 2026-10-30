package com.alexbatalov.fallout2ce;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.view.View;

// A miniature of the bottom screen for the setup guide: one of the three
// dashboard layouts (stage 1: classic, map first, two columns) in one of the
// three themes (Fallout: brass plates, dark green wells, amber; Pip-Boy 2000:
// phosphor green on dark glass; classic: green). Drawn schematically: the
// Fallout theme's art is the player's own
// game data and isn't shipped, so the guide never shows it.
public class GuidePreviewView extends View {
    static final int THEME_FALLOUT = 0;
    static final int THEME_CLASSIC = 1;
    static final int THEME_PIPBOY = 2;

    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint line = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF rect = new RectF();
    private final float density;

    private int layout;
    private int theme;
    private boolean selected;

    public GuidePreviewView(Context context) {
        super(context);
        density = context.getResources().getDisplayMetrics().density;
        line.setStyle(Paint.Style.STROKE);
    }

    void set(int layout, int theme, boolean selected) {
        this.layout = layout;
        this.theme = theme;
        this.selected = selected;
        invalidate();
    }

    @Override
    protected void onMeasure(int widthSpec, int heightSpec) {
        int width = MeasureSpec.getSize(widthSpec);
        // The screen's 1240 x 1080.
        setMeasuredDimension(width, Math.round(width * 1080f / 1240f));
    }

    private int plate() {
        if (theme == THEME_PIPBOY) {
            return 0xFF1A2A1A;
        }
        return theme == THEME_FALLOUT ? 0xFF8B7B52 : getContext().getColor(R.color.import_button);
    }

    private int well() {
        if (theme == THEME_PIPBOY) {
            return 0xFF0A150A;
        }
        return theme == THEME_FALLOUT ? 0xFF0C1A0C : 0xFF0B120D;
    }

    private int edge() {
        if (theme == THEME_PIPBOY) {
            return 0xFF1F8C0D;
        }
        return theme == THEME_FALLOUT ? 0xFF2A2214 : getContext().getColor(R.color.import_button_border);
    }

    private int accent() {
        if (theme == THEME_PIPBOY) {
            return 0xFF39FF14;
        }
        return theme == THEME_FALLOUT ? 0xFFFFC832 : getContext().getColor(R.color.import_accent);
    }

    private void box(Canvas canvas, float left, float top, float right, float bottom, int color, boolean rivets) {
        rect.set(left, top, right, bottom);
        float radius = 2 * density;
        fill.setColor(color);
        canvas.drawRoundRect(rect, radius, radius, fill);
        line.setColor(edge());
        line.setStrokeWidth(density);
        canvas.drawRoundRect(rect, radius, radius, line);
        if (rivets && theme == THEME_FALLOUT && right - left > 14 * density) {
            fill.setColor(0xFF3A301C);
            float inset = 3 * density;
            canvas.drawCircle(left + inset, top + inset, density, fill);
            canvas.drawCircle(right - inset, top + inset, density, fill);
        }
    }

    // A row of small plates (the Screens, System, Tools groups).
    private void row(Canvas canvas, float left, float top, float width, float height, int count) {
        float gap = 2 * density;
        float each = (width - gap * (count - 1)) / count;
        for (int index = 0; index < count; index++) {
            box(canvas, left + index * (each + gap), top, left + index * (each + gap) + each, top + height, plate(), true);
        }
    }

    private void column(Canvas canvas, float left, float top, float width, float height, int count) {
        float gap = 2 * density;
        float each = (height - gap * (count - 1)) / count;
        for (int index = 0; index < count; index++) {
            box(canvas, left, top + index * (each + gap), left + width, top + index * (each + gap) + each, plate(), true);
        }
    }

    private void map(Canvas canvas, float left, float top, float right, float bottom) {
        box(canvas, left, top, right, bottom, well(), false);
        // A few lines of an automap.
        line.setColor(accent());
        line.setStrokeWidth(density);
        line.setAlpha(150);
        float w = right - left;
        float h = bottom - top;
        canvas.drawLine(left + w * 0.15f, top + h * 0.7f, left + w * 0.5f, top + h * 0.3f, line);
        canvas.drawLine(left + w * 0.5f, top + h * 0.3f, left + w * 0.85f, top + h * 0.6f, line);
        canvas.drawLine(left + w * 0.3f, top + h * 0.55f, left + w * 0.65f, top + h * 0.8f, line);
        line.setAlpha(255);
        fill.setColor(accent());
        canvas.drawCircle(left + w * 0.5f, top + h * 0.3f, 2 * density, fill);
    }

    // The status readout: a bar and a line of digits.
    private void status(Canvas canvas, float left, float top, float right, float bottom) {
        box(canvas, left, top, right, bottom, well(), false);
        float h = bottom - top;
        fill.setColor(accent());
        canvas.drawRect(left + 3 * density, top + h * 0.2f, left + (right - left) * 0.7f, top + h * 0.45f, fill);
        fill.setAlpha(120);
        canvas.drawRect(left + 3 * density, top + h * 0.6f, left + (right - left) * 0.45f, top + h * 0.8f, fill);
        fill.setAlpha(255);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        float width = getWidth();
        float height = getHeight();
        float pad = 3 * density;

        fill.setColor(theme == THEME_PIPBOY ? 0xFF0A150A : theme == THEME_FALLOUT ? 0xFF3A3326 : 0xFF050A06);
        rect.set(0, 0, width, height);
        canvas.drawRoundRect(rect, 4 * density, 4 * density, fill);

        float left = pad;
        float right = width - pad;
        float top = pad;
        float bottom = height - pad;
        float bar = height * 0.12f;

        // Screens (top left) and System (top right) in every layout.
        row(canvas, left, top, (right - left) * 0.5f, bar, 4);
        row(canvas, right - (right - left) * 0.2f, top, (right - left) * 0.2f, bar, 2);
        float below = top + bar + pad;

        switch (layout) {
            case 1: {
                // Map first: the map across, a strip under it.
                float strip = height * 0.2f;
                map(canvas, left, below, right, bottom - strip - pad);
                row(canvas, left, bottom - strip, (right - left) * 0.45f, strip, 5);
                status(canvas, left + (right - left) * 0.5f, bottom - strip, right, bottom);
                break;
            }
            case 2: {
                // Two columns: tools at the left, the weapon column at the right.
                float tools = (right - left) * 0.12f;
                float weapon = (right - left) * 0.26f;
                column(canvas, left, below, tools, bottom - below, 5);
                map(canvas, left + tools + pad, below, right - weapon - pad, bottom);
                status(canvas, right - weapon, below, right, below + (bottom - below) * 0.35f);
                column(canvas, right - weapon, below + (bottom - below) * 0.35f + pad, weapon, (bottom - below) * 0.65f - pad, 2);
                break;
            }
            default: {
                // Classic: tools at the left, the map, the status at the bottom.
                float tools = (right - left) * 0.12f;
                column(canvas, left, below, tools, bottom - below, 5);
                float status = height * 0.2f;
                map(canvas, left + tools + pad, below, right, bottom - status - pad);
                status(canvas, left + tools + pad, bottom - status, right, bottom);
                break;
            }
        }

        if (selected) {
            line.setColor(accent());
            line.setStrokeWidth(2.5f * density);
            rect.set(density, density, width - density, height - density);
            canvas.drawRoundRect(rect, 4 * density, 4 * density, line);
        }
    }
}
