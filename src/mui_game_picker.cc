#include "mui_game_picker.h"

#include "art.h"
#include "color.h"
#include "dbox.h"
#include "debug.h"
#include "dev_autotest.h"
#include "fps_limiter.h"
#include "game.h"
#include "game_catalog.h"
#include "game_movie.h"
#include "input.h"
#include "kb.h"
#include "mui.h"
#include "mui_pad.h"
#include "palette.h"
#include "settings.h"
#include "svga.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#ifdef __ANDROID__
#include <jni.h>
#endif

namespace fallout {
namespace {
    std::u32string text(int id, const char* fallback)
    {
        return muiDecodeGameText(muiText(id, fallback));
    }
    unsigned long long baseFileMiB()
    {
        unsigned long long bytes = 0;
        for (const auto& path : { settings.system.master_dat_path, settings.system.critter_dat_path }) {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (file && file.tellg() > 0) bytes += static_cast<unsigned long long>(file.tellg());
        }
        return bytes / (1024 * 1024);
    }
    bool gameInstalled(int index)
    {
        return index == 0 || std::ifstream(gameRootPath(gameTopRoot(), gameCatalog()[index].id) + "/.installed").good();
    }

    std::string installStatus(int index)
    {
        if (gameInstalled(index)) return "Installed";
#ifdef __ANDROID__
        // One poll per second across both surfaces.
        static unsigned int checked = 0;
        static int page = -1;
        static std::string status;
        if (page == index && uiTicks() - checked < 1000) return status;
        checked = uiTicks();
        page = index;
        status = "Not installed";
        JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
        jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
        if (!env || !activity) return status;
        jclass cls = env->GetObjectClass(activity);
        jmethodID method = env->GetMethodID(cls, "gameInstallStatus", "(Ljava/lang/String;)Ljava/lang/String;");
        if (method) {
            jstring id = env->NewStringUTF(gameCatalog()[index].id);
            jstring result = static_cast<jstring>(env->CallObjectMethod(activity, method, id));
            if (!env->ExceptionCheck() && result) {
                const char* value = env->GetStringUTFChars(result, nullptr);
                if (value) {
                    status = value;
                    env->ReleaseStringUTFChars(result, value);
                }
            }
            if (result) env->DeleteLocalRef(result);
            env->DeleteLocalRef(id);
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(cls);
        env->DeleteLocalRef(activity);
        return status;
#else
        return "Not installed";
#endif
    }

    struct PickerState {
        unsigned long long baseMiB = baseFileMiB();
        int page = gamePickerLoadPage(gameTopRoot());
        int previous = page;
        unsigned int changed = 0;
        bool play = false;
        bool mods = false;
        bool intro = false;
        bool stub = false;
        bool install = false;
        bool quit = false;
        // Set false while another modal owns input (Mods or notice).
        bool interactive = true;
        void select(int next)
        {
            next = (next + 8) % 8;
            if (next == page) return;
            previous = page;
            page = next;
            changed = uiTicks();
            if (!gamePickerSavePage(page, gameTopRoot())) debugPrint("Picker selection could not be saved\n");
        }
    };
    PickerState* active = nullptr;

    // Android install/project actions stay behind the platform boundary.
    void requestGameAction(const GameCatalogEntry& entry)
    {
#ifdef __ANDROID__
        JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
        jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
        if (!env || !activity) return;
        jclass cls = env->GetObjectClass(activity);
        bool project = entry.downloadKind == GameDownloadKind::ProjectPage;
        jmethodID method = env->GetMethodID(cls,
            project ? "openGameProjectPage" : "installGame", "(Ljava/lang/String;Ljava/lang/String;)V");
        if (method) {
            jstring id = env->NewStringUTF(entry.id);
            jstring url = env->NewStringUTF(entry.projectUrl);
            env->CallVoidMethod(activity, method, id, url);
            env->DeleteLocalRef(id);
            env->DeleteLocalRef(url);
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(cls);
        env->DeleteLocalRef(activity);
#else
        (void)entry;
#endif
    }

    class PickerScreen : public MuiScreen {
    public:
        PickerScreen(PickerState& state, MuiSurface target)
            : state(state)
            , target(target)
        {
        }
        MuiSurface surface() override { return target; }
        bool coversScreen() override { return true; }
        void back() override
        {
            debugPrint("Picker: back on %s screen\n", target == MuiSurface::Top ? "top" : "bottom");
            state.quit = true;
        }
        void build(MuiContext& ui) override;

    private:
        void artwork(MuiContext& ui, MuiRect rect, int index);
        void controls(MuiContext& ui, MuiRect rect, int index, bool enabled);
        PickerState& state;
        MuiSurface target;
        bool wasDown = false;
        float startX = 0, startY = 0;
    };

    void PickerScreen::artwork(MuiContext& ui, MuiRect rect, int index)
    {
        ui.mark("picker.swipe.art", { rect.x, rect.y, rect.w, rect.h * 0.4f });
        const auto& entry = gameCatalog()[index];
        const auto& theme = muiTheme();
        muiFillGradient(rect, theme.background, theme.well, theme.panel, theme.background);
        if (index == 0) {
            int w = 0, h = 0;
            SDL_Texture* image = muiPictureTexture(InterfaceFrmId("HR_MAINMENU.FRM"), &w, &h);
            int left = 0;
            if (!image) {
                image = muiPictureTexture(FrmId(InterfaceFrameId::MainMenuBackgroundImage), &w, &h);
                left = 215; // Omit the vanilla menu's baked-in button plates.
            }
            if (image && h > 0 && w > left) {
                MuiRect picture = { rect.x + rect.w * 0.35f, rect.y, rect.w * 0.65f, rect.h };
                muiDrawTexturePart(image, { left, 0, w - left, h }, muiFitRect(picture, w - left, h));
                muiFillHorizontalGradient(rect, theme.background, theme.background.withAlpha(0));
            }
        }
        MuiRect title = { rect.x + ui.dp(22), rect.y + rect.h * 0.28f, rect.w - ui.dp(44), rect.h * 0.3f };
        auto label = muiDecodeUtf8(entry.title);
        muiDrawTextAligned(label, title, muiFitTextSize(label, title.w, std::max(ui.dp(52), rect.w * 0.065f), ui.dp(20)), theme.accent, MuiAlign::Start, MuiAlign::Center);
        if (entry.experimental) {
            muiDrawTextAligned(text(405, "Experimental"), { title.x, title.bottom(), title.w, ui.dp(30) }, ui.dp(16), theme.warning, MuiAlign::Start, MuiAlign::Center);
        }
        auto credit = text(408, "by") + U" " + muiDecodeUtf8(entry.creator) + U" - " + muiDecodeUtf8(entry.projectUrl);
        MuiRect footer = { title.x, rect.bottom() - ui.dp(68), title.w, ui.dp(60) };
        float size = std::max(ui.dp(14), rect.w * 0.01f);
        for (const auto& line : muiWrapText(credit, footer.w, size)) {
            muiDrawText(line, footer.x, footer.y, size, theme.textDim);
            footer.y += muiLineHeight(size);
        }
    }

    void PickerScreen::controls(MuiContext& ui, MuiRect rect, int index, bool enabled)
    {
        const auto& theme = muiTheme();
        const auto& entry = gameCatalog()[index];
        if (enabled) ui.mark("picker.swipe.controls", { rect.x, rect.y, rect.w, rect.h * 0.4f });
        MuiRect inner = rect.inset(ui.dp(18));
        auto label = muiDecodeUtf8(entry.title);
        muiDrawTextAligned(label, { inner.x, inner.y, inner.w, inner.h * 0.16f }, muiFitTextSize(label, inner.w, ui.dp(26), ui.dp(14)), theme.accent, MuiAlign::Center, MuiAlign::Center);
        bool installedGame = gameInstalled(index);
        std::u32string status = installedGame ? text(399, "Installed") : muiDecodeUtf8(installStatus(index).c_str());
        muiDrawTextAligned(status, { inner.x, inner.y + inner.h * 0.17f, inner.w, inner.h * 0.09f }, ui.dp(18), theme.text, MuiAlign::Center, MuiAlign::Center);
        char installed[160];
        std::snprintf(installed, sizeof(installed), muiText(409, "Version: unknown | Base files: %llu MiB"), state.baseMiB);
        auto details = index == 0 ? muiDecodeGameText(installed)
            : installedGame       ? muiDecodeUtf8("Version: installed release")
                                  : text(410, "Version: not installed | Size: download pending");
        muiDrawTextAligned(details, { inner.x, inner.y + inner.h * 0.27f, inner.w, inner.h * 0.08f }, muiFitTextSize(details, inner.w, ui.dp(13), ui.dp(9)), theme.textDim, MuiAlign::Center, MuiAlign::Center);
        auto lang = index == 0 ? text(411, "Language: ") + muiDecodeUtf8(settings.system.language.c_str())
                               : muiDecodeUtf8(muiText(411 + index, entry.languageNote));
        muiDrawTextAligned(lang, { inner.x, inner.y + inner.h * 0.36f, inner.w, inner.h * 0.08f }, muiFitTextSize(lang, inner.w, ui.dp(14), ui.dp(9)), theme.textDim, MuiAlign::Center, MuiAlign::Center);
        float height = std::min(ui.dp(56), inner.h * 0.14f);
        MuiRect action = { inner.x, inner.y + inner.h * 0.49f, inner.w, height };
        auto button = [&](const char* id, MuiRect r, std::u32string value, bool primary) {
            // Outgoing pages are painted without registering interactive widgets.
            if (!enabled) {
                muiFillRoundRect(r, ui.dp(theme.radius), theme.button);
                muiDrawTextAligned(value, r, ui.dp(18), theme.textDim, MuiAlign::Center, MuiAlign::Center);
                return false;
            }
            return ui.button(id, r, value, primary ? MuiButtonStyle::Primary : MuiButtonStyle::Normal);
        };
        if (installedGame) {
            bool hasIntro = gameCatalogIntroCount(index) > 0;
            MuiRect playRect = action;
            if (hasIntro) {
                // Intro sits beside Play.
                float gap = ui.dp(10);
                playRect.w = (action.w - gap) * 0.62f;
                MuiRect introRect = { playRect.right() + gap, action.y, action.w - playRect.w - gap, action.h };
                if (button("picker.intro", introRect, text(419, "Intro"), false)) state.intro = true;
            }
            if (button("picker.play", playRect, text(398, "Play"), true)) state.play = true;
            if (enabled) ui.padPrefer("picker.play");
        } else {
            auto caption = entry.downloadKind == GameDownloadKind::ProjectPage ? text(402, "Open project page") : text(401, "Install");
            if (button("picker.install", action, caption, true)) {
                state.stub = true;
                state.install = true;
            }
            if (enabled) ui.padPrefer("picker.install");
        }
        action.y += height + ui.dp(10);
        if (button("picker.mods", action, text(380, "Mods"), false)) {
            if (index == 0)
                state.mods = true;
            else
                state.stub = true;
        }
        // Large touch targets with small illuminated page dots.
        float dot = std::min(inner.w / 8, ui.dp(38));
        float x = inner.centerX() - dot * 4;
        for (int i = 0; i < 8; ++i) {
            MuiRect cell = { x + i * dot, inner.bottom() - ui.dp(40), dot, ui.dp(36) };
            if (enabled && ui.touchable("picker.page." + std::to_string(i), cell)) state.select(i);
            muiFillCircle(cell.centerX(), cell.centerY(), ui.dp(i == index ? 5 : 3), i == index ? theme.accent : theme.textDim);
        }
    }

    void PickerScreen::build(MuiContext& ui)
    {
        muiFillRect(ui.screenRect(), muiTheme().background);
        bool down = ui.pointerDown();
        if (ui.interactive() && state.interactive) {
            if (down && !wasDown) {
                startX = ui.pointerX();
                startY = ui.pointerY();
            }
            if (!down && wasDown) {
                float dx = ui.pointerX() - startX, dy = ui.pointerY() - startY;
                if (std::abs(dx) > ui.dp(50) && std::abs(dx) > std::abs(dy) * 1.3f) state.select(state.page + (dx < 0 ? 1 : -1));
            }
        }
        wasDown = down;
        float t = std::min(1.0f, (uiTicks() - state.changed) / 240.0f);
        float ease = t * t * (3 - 2 * t);
        int direction = (state.page == (state.previous + 1) % 8) ? 1 : -1;
        MuiRect safe = ui.safeRect();
        bool split = muiBottomActive();
        auto draw = [&](int page, float offset, bool enabled) {
            MuiRect area = safe;
            area.x += offset;
            muiPushClip(safe);
            if (target == MuiSurface::Bottom)
                controls(ui, area, page, enabled);
            else if (split)
                artwork(ui, area, page);
            else {
                MuiRect art = { area.x, area.y, area.w * 0.57f, area.h };
                artwork(ui, art, page);
                controls(ui, { art.right(), area.y, area.w - art.w, area.h }, page, enabled);
            }
            muiPopClip();
        };
        if (t < 1 && state.previous != state.page) draw(state.previous, -direction * ease * safe.w, false);
        draw(state.page, direction * (1 - ease) * safe.w, t >= 1 && state.interactive);
    }
} // namespace

bool muiGamePickerMove(int direction)
{
    if (!active || !active->interactive) return false;
    active->select(active->page + direction);
    return true;
}

bool muiGamePickerAccept(MuiSurface surface)
{
    if (!active || !active->interactive) return false;
    const auto& focus = muiPadFocusId(surface);
    // A works on the first press, including when focus was on the art surface.
    const auto& widgets = muiPadWidgets(surface);
    bool validFocus = std::any_of(widgets.begin(), widgets.end(), [&](const MuiPadWidget& widget) { return widget.id == focus; });
    if (!validFocus || focus == "picker.play" || focus == "picker.install") {
        if (gameInstalled(active->page))
            active->play = true;
        else {
            active->stub = true;
            active->install = true;
        }
        return true;
    }
    return false;
}

bool muiGamePickerIntro()
{
    if (!active || !active->interactive || gameCatalogIntroCount(active->page) == 0 || active->page != 0) return false;
    active->intro = true;
    return true;
}

// Plays the game's opening movies (a tap or a key skips each), then the
// picker is shown again as it was.
static void playIntro(int index)
{
    const auto& entry = gameCatalog()[index];
    int count = gameCatalogIntroCount(index);
    for (int i = 0; i < count && _game_user_wants_to_quit == GAME_QUIT_REQUEST_NONE; ++i) {
        gameMoviePlay(entry.introMovies[i], i == 0 ? GAME_MOVIE_FADE_IN : 0);
    }
    paletteFadeTo(_cmap);
}

bool muiGamePickerRun()
{
    PickerState state;
    // No initial slide; both surfaces share the same transition clock.
    state.changed = uiTicks() - 240;
    PickerScreen top(state, MuiSurface::Top), bottom(state, MuiSurface::Bottom);
    active = &state;
    muiPush(&top);
    if (muiBottomActive()) muiPush(&bottom);
    paletteFadeTo(_cmap);
    while (!state.play && !state.quit && _game_user_wants_to_quit == GAME_QUIT_REQUEST_NONE) {
        sharedFpsLimiter.mark();
        int key = inputGetInput();
        if (key == KEY_ESCAPE) {
            debugPrint("Picker: Escape key, quitting\n");
            state.quit = true;
        }
        if (key == KEY_ARROW_LEFT) state.select(state.page - 1);
        if (key == KEY_ARROW_RIGHT) state.select(state.page + 1);
        if (key == KEY_RETURN && gameInstalled(state.page)) state.play = true;
        devAutotestTick();
        renderPresent();
        if (state.intro) {
            state.interactive = false;
            state.intro = false;
            playIntro(state.page);
            state.interactive = true;
        }
        if (state.mods || state.stub) {
            state.interactive = false;
            if (state.mods) {
                state.mods = false;
                muiModsScreenRun();
                if (muiModsRestartRequested()) {
                    debugPrint("Picker: mods asked for a restart\n");
                    state.quit = true;
                }
            } else {
                state.stub = false;
                bool showNotice = true;
                if (state.install) {
                    requestGameAction(gameCatalog()[state.page]);
#ifdef __ANDROID__
                    showNotice = gameCatalog()[state.page].downloadKind == GameDownloadKind::ProjectPage;
#endif
                }
                state.install = false;
                if (showNotice) {
                    const char* body[] = { muiText(404, "Coming in the next update") };
                    showDialogBox(gameCatalog()[state.page].title, body, 1, 0, 0, COLOR_AMBER, nullptr, COLOR_AMBER, 0);
                }
            }
            state.interactive = true;
        }
        sharedFpsLimiter.throttle();
    }
    debugPrint("Picker: done play=%d quit=%d wantsQuit=%d\n", state.play ? 1 : 0, state.quit ? 1 : 0, _game_user_wants_to_quit);
    if (state.play && !gamePickerSavePage(state.page, gameTopRoot())) debugPrint("Picker selection could not be saved\n");
    bool switchGame = state.play && !state.quit && state.page != gameActiveIndex();
    if (switchGame) {
        // CE: Another game: its root exists, the selection is saved at the
        // top root, and the app restarts as a profile switch does.
        if (gameRootEnsure(gameTopRoot(), gameCatalog()[state.page].id)) {
            debugPrint("Picker: switching game to %s\n", gameCatalog()[state.page].id);
            muiRequestAppRestart();
            state.quit = true;
        } else {
            debugPrint("Picker: game root could not be created\n");
            state.play = false;
        }
    }
    muiRemove(&bottom);
    muiRemove(&top);
    active = nullptr;
    return state.play && !state.quit;
}
} // namespace fallout
