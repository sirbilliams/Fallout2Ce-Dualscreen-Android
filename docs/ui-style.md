<!-- Drafted 2026-10-30 (local model, reviewed). Every UI change follows this; update it when a design decision changes. -->
# UI Style Guide: Fallout 1/2 CE on AYN Thor

## 1. Principles
- **Diegetic:** Interface must feel like hardware interaction (Pip-Boy or console).
- **Legibility:** Prioritize readability at arm's length; high contrast is mandatory.
- **One Accent:** Use accent color only for active states/hover; never for static text.
- **Calm:** Avoid excessive animation; respect player focus.

## 2. Layout Grid
- **Base Unit:** 8px grid at bottom-screen resolution (1240x1080).
- **Safe Area:** Min 24px padding from screen edges on bottom display.
- **Touch Targets:** Minimum 72x72 px for all interactive elements.
- **Spacing:** Min 16px between adjacent touch targets to prevent mis-taps.
- **Heights:** Headers: 88px; Footers: 80px; List rows: 72px (min - every row is a touch target).

## 3. Typography
- **Font:** Use AAF bitmap font; never default system fonts.
- **Sizing (bottom screen px; the Thor bottom panel is small, so err large):** Title 40; Heading 32; Body 26; Caption 20 (minimum anywhere). Pick the nearest AAF font and scale by an integer; never smaller than Caption.
- **Kerning:** Enforce proportional advance width to prevent gaps (e.g., 'SWI NG').
- **Tabs:** Enable tabular lining figures for all numerical data (HP, caps, time).
- **Truncation:** Middle-ellipsis rule: truncate center with '..'; preserve start and tail. Never clip glyphs mid-character or at panel edges.

## 4. Colour Tokens

### Theme: 'Pip-Boy 2000' (DEFAULT)
- `bg`: #0A150A (Dark glass)
- `panel`: #1A2A1A (CRT bezel)
- `text`: #39FF14 (Phosphor green, bright)
- `text-dim`: #1F8C0D (Glare off)
- `accent`: #7FFF00 (Selected item)
- `accent-pressed`: #5FFF00 (Active click)
- `disabled`: #153A15 (Inactive)
- `danger`: #B8FF3C (brighter green pulse; no red under Pip-Boy - it clashes with the phosphor)
- `warning`: #BFFF00 (Rads)
- `ok`: #39FF14
- *Contrast:* All text >= 4.5:1 against panel/bg.

### Theme: 'Classic Fallout'
- `bg`: #2B1D14 (Rusted metal base)
- `panel`: #3E2723 (Plate dark)
- `text`: #FFCC66 (Amber/Yellow)
- `text-dim`: #8C704A (Faded ink)
- `accent`: #FFD54F (Active toggle/lamp)
- `accent-pressed`: #FFC107 (Pressed lamp)
- `disabled`: #5D4037 (Rivet shadow)
- `danger`: #FF5252 (Alert)
- `warning`: #FFAB40 (Caution)
- `ok`: #69F0AE (Status OK)

## 5. Components
- **Button Primary/Secondary:** Full width available; inner padding 16px vertical.
- **Tab Bar:** Equal weight tabs; indicator matches accent color height: 4px.
- **List/Selected Row:** Highlight row bg with `accent` at 20% opacity.
- **Panel/Card:** Inner shadow simulating depth; border width 2px `panel`.
- **Header:** Left-aligned title; right-aligned 'Back' or close icon (72x72 target).
- **Toggle:** Visual state change must mimic physical lamp/button (on/off).
- **Slider:** Thumb radius 16px; track height 8px.
- **Toast:** Auto-hide after 3000ms; bottom-aligned, max width 80%.
- **Modal Confirm:** Centered; two buttons: Cancel (Secondary) / Confirm (Primary).
- **Progress/HP Bar:** Fill uses `danger` or `ok`. Ensure glow does not bleed.
- **Map Overlay:** Buttons must not overlap location header; offset vertically by 64px.

## 6. Motion
- **Duration:** Transition 200ms.
- **Easing:** Ease-in-out for UI slides; Linear for progress bars.
- **CRT Flicker:** Power-on sequence <=250ms. Must be skippable via any input.
- **Reduce Motion:** Disable all animations if system preference is set to reduce motion.

## 7. Iconography
- **Preference:** Use existing game FRM images/AAM bitmaps where available.
- **Fallback:** Simple 2px line icons; monoline style consistent with AAF font weight.

## 8. Checklist for Merge
Run `shot-check -p "<screen> in the <theme> theme: no clipped or overlapping text, no letter gaps, targets >= 72 px" <shots>` and look at every flag yourself.
- [ ] No text clipping at bottom screen bounds (1240x1080).
- [ ] No layout breakage on top screen (1920x1080, 16:9).
- [ ] All interactive targets >= 72px in height and width.
- [ ] Theme switch (Pip-Boy vs Classic) updates all tokens correctly.
- [ ] Monospace gap bug fixed (proportional advance enabled).
- [ ] Shot-check prompt line verified for both themes.
