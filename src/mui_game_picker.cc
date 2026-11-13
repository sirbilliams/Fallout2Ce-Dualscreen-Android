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
#include "picker_picture.h"
#include "settings.h"
#include "svga.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
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
        if (index != 0 && gameOwnDatsMissing(gameTopRoot(), index)) return false; // incomplete: Repair
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
        status = gameNeedsRepair(gameTopRoot(), index) ? "Needs repair: game files incomplete" : "Not installed";
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
        return gameNeedsRepair(gameTopRoot(), index) ? "Needs repair: game files incomplete" : "Not installed";
#endif
    }

    // Download size ("123 MB") from the Java installer; "" while unknown.
    std::string downloadSize(int index)
    {
#ifdef __ANDROID__
        static unsigned int checked[8] = {};
        static std::string sizes[8];
        if (!sizes[index].empty() || (checked[index] != 0 && uiTicks() - checked[index] < 2000)) return sizes[index];
        checked[index] = uiTicks();
        JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
        jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
        if (!env || !activity) return sizes[index];
        jclass cls = env->GetObjectClass(activity);
        jmethodID method = env->GetMethodID(cls, "gameDownloadSize", "(Ljava/lang/String;)Ljava/lang/String;");
        if (method) {
            jstring id = env->NewStringUTF(gameCatalog()[index].id);
            jstring result = static_cast<jstring>(env->CallObjectMethod(activity, method, id));
            if (!env->ExceptionCheck() && result) {
                const char* value = env->GetStringUTFChars(result, nullptr);
                if (value) {
                    sizes[index] = value;
                    env->ReleaseStringUTFChars(result, value);
                }
            }
            if (result) env->DeleteLocalRef(result);
            env->DeleteLocalRef(id);
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(cls);
        env->DeleteLocalRef(activity);
        return sizes[index];
#else
        (void)index;
        return "";
#endif
    }

    // True while a download or install is in flight (statuses written by
    // GameInstaller/GameInstallJob); failures and "Not installed" are not busy,
    // so the Install button returns after a failure or cancel.
    bool installBusy(const std::string& status)
    {
        for (const char* prefix : { "Downloading", "Waiting", "Looking up", "Installing" }) {
            if (status.rfind(prefix, 0) == 0) return true;
        }
        return false;
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
        // "Add Fallout 1 files" was chosen (Et Tu needs the player's own files).
        bool addBase = false;
        // base/fallout1 holds master.dat and critter.dat; rechecked while the
        // picker runs, as the import screen is a separate activity.
        bool fo1Ready = gameBaseHasFiles(gameTopRoot(), 1);
        unsigned int fo1Checked = uiTicks();
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

    bool needsFallout1Files(const PickerState& state, int index)
    {
        return std::string(gameCatalog()[index].baseGame) == "fallout1" && !state.fo1Ready;
    }

    // JNI entry for the Fallout 1 import (MainActivity.addFallout1Files); false
    // when this build has none (desktop), so the caller shows the notice.
    bool requestFallout1Import(const GameCatalogEntry& entry)
    {
#ifdef __ANDROID__
        JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
        jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
        if (!env || !activity) return false;
        jclass cls = env->GetObjectClass(activity);
        jmethodID method = env->GetMethodID(cls, "addFallout1Files", "(Ljava/lang/String;)V");
        bool called = false;
        if (method) {
            jstring id = env->NewStringUTF(entry.id);
            env->CallVoidMethod(activity, method, id);
            env->DeleteLocalRef(id);
            called = true;
        }
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            called = false;
        }
        env->DeleteLocalRef(cls);
        env->DeleteLocalRef(activity);
        return called;
#else
        (void)entry;
        return false;
#endif
    }

    void requestPicture(const GameCatalogEntry& entry)
    {
#ifdef __ANDROID__
        JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
        jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
        if (!env || !activity) return;
        jclass cls = env->GetObjectClass(activity);
        jmethodID method = env->GetMethodID(cls, "prepareGamePictures", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
        if (method) {
            jstring id = env->NewStringUTF(entry.id);
            jstring creator = env->NewStringUTF(entry.creator);
            jstring source = env->NewStringUTF(entry.pictureSource);
            env->CallVoidMethod(activity, method, id, creator, source);
            env->DeleteLocalRef(id);
            env->DeleteLocalRef(creator);
            env->DeleteLocalRef(source);
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
        struct CachedPicture {
            PickerPicture pixels;
            std::string path;
            std::filesystem::file_time_type modified {};
            unsigned int version = 0;
            unsigned int checked = 0;
        };
        std::array<CachedPicture, 8> pictures;
        bool wasDown = false;
        float startX = 0, startY = 0;
    };

    void PickerScreen::artwork(MuiContext& ui, MuiRect rect, int index)
    {
        ui.mark("picker.swipe.art", { rect.x, rect.y, rect.w, rect.h * 0.4f });
        const auto& entry = gameCatalog()[index];
        const auto& theme = muiTheme();
        muiFillGradient(rect, theme.background, theme.well, theme.panel, theme.background);
        auto& cached = pictures[index];
        if (cached.checked == 0 || uiTicks() - cached.checked >= 1000) {
            cached.checked = uiTicks();
            requestPicture(entry);
            // Prefer full art; corrupt/missing files fall through to preview and original art.
            for (bool installed : { true, false }) {
                std::string path = pickerPicturePath(gameTopRoot(), entry.id, installed);
                std::error_code ec;
                auto modified = std::filesystem::last_write_time(path, ec);
                if (ec) continue;
                if (path == cached.path && modified == cached.modified) break;
                PickerPicture loaded;
                if (!pickerPictureLoad(path, loaded)) continue;
                cached.pixels = std::move(loaded);
                cached.path = path;
                cached.modified = modified;
                cached.version = static_cast<unsigned int>(modified.time_since_epoch().count());
                break;
            }
        }
        auto& pixels = cached.pixels;
        SDL_Texture* downloaded = pixels.rgba.empty() ? nullptr
                                                      : muiRgbaTexture(std::string("picker.") + entry.id, pixels.rgba.data(), pixels.width, pixels.height, cached.version);
        if (downloaded) {
            // Art sits right of the title column so the title never overlaps it.
            MuiRect picture = { rect.x + rect.w * 0.4f, rect.y, rect.w * 0.6f, rect.h };
            muiDrawTexture(downloaded, muiFitRect(picture, pixels.width, pixels.height));
            muiFillHorizontalGradient({ rect.x, rect.y, rect.w * 0.6f, rect.h }, theme.background, theme.background.withAlpha(0));
        }
        if (!downloaded && index == 0) {
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
        bool wantsFo1 = std::string(entry.baseGame) == "fallout1";
        auto details = index == 0         ? muiDecodeGameText(installed)
            : installedGame               ? muiDecodeUtf8("Version: installed release")
            : wantsFo1                    ? (state.fo1Ready ? text(431, "Fallout 1 files: ready") : text(432, "Needs your own Fallout 1 files"))
            : downloadSize(index).empty() ? text(433, "Version: not installed")
                                          : text(410, "Version: not installed | Size: ") + muiDecodeUtf8(downloadSize(index).c_str());
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
            // Movies are read through the running game's data, so only the
            // active game can play its intro (other roots are not mounted).
            bool hasIntro = gameCatalogIntroCount(index) > 0 && index == gameActiveIndex();
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
            bool addBase = needsFallout1Files(state, index);
            // Progress text replaces the button while the install runs.
            if (!addBase && entry.downloadKind != GameDownloadKind::ProjectPage && installBusy(installStatus(index))) {
                muiFillRoundRect(action, ui.dp(theme.radius), theme.button);
                muiDrawTextAligned(status, action, muiFitTextSize(status, action.w - ui.dp(16), ui.dp(18), ui.dp(10)), theme.textDim, MuiAlign::Center, MuiAlign::Center);
            } else {
                auto caption = addBase                                    ? text(430, "Add Fallout 1 files")
                    : entry.downloadKind == GameDownloadKind::ProjectPage ? text(402, "Open project page")
                    : gameNeedsRepair(gameTopRoot(), index)               ? muiDecodeUtf8("Repair")
                                                                          : text(401, "Install");
                if (button("picker.install", action, caption, true)) {
                    if (addBase) {
                        state.addBase = true;
                    } else {
                        state.stub = true;
                        state.install = true;
                    }
                }
                if (enabled) ui.padPrefer("picker.install");
            }
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
        else if (needsFallout1Files(*active, active->page))
            active->addBase = true;
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
    if (!active || !active->interactive || gameCatalogIntroCount(active->page) == 0 || active->page != gameActiveIndex()) return false;
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
    // CE: After a game switch restart, Play was already pressed for this game.
    std::string playOnce = gamePickerConsumePlayOnce(gameTopRoot());
    if (!playOnce.empty() && playOnce == gameCatalog()[gameActiveIndex()].id) {
        debugPrint("Picker: skipped, Play already pressed for %s\n", playOnce.c_str());
        return true;
    }
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
        if (state.fo1Checked + 1000 < uiTicks()) {
            state.fo1Checked = uiTicks();
            state.fo1Ready = gameBaseHasFiles(gameTopRoot(), 1);
        }
        if (state.addBase) {
            state.addBase = false;
            if (!requestFallout1Import(gameCatalog()[state.page])) state.stub = true;
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
            if (!gamePickerWritePlayOnce(gameCatalog()[state.page].id, gameTopRoot())) debugPrint("Picker play-once marker could not be saved\n");
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
