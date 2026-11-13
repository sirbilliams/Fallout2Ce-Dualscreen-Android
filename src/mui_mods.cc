#include "mui.h"
#include "mui_screens.h"

#ifdef __ANDROID__
#include <SDL.h>
#include <jni.h>
#endif

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "color.h"
#include "dbox.h"
#include "debug.h"
#include "dev_autotest.h"
#include "fps_limiter.h"
#include "game.h"
#include "game_catalog.h"
#include "input.h"
#include "kb.h"
#include "mod_list.h"
#include "mod_profiles.h"
#include "svga.h"

namespace fallout {

namespace {

    // Texts in `game\ce.msg`.
    constexpr int kTextMods = 380;
    constexpr int kTextProfile = 381;
    constexpr int kTextNew = 382;
    constexpr int kTextRename = 383;
    constexpr int kTextDelete = 384;
    constexpr int kTextAddZip = 385;
    constexpr int kTextBack = 386;
    constexpr int kTextNoMods = 387;
    constexpr int kTextSwitch = 388;
    constexpr int kTextProfileName = 389;
    constexpr int kTextDone = 390;
    constexpr int kTextRestartNote = 391;
    constexpr int kTextDeleteAsk = 392;
    constexpr int kTextProfileFailed = 393;
    constexpr int kTextInstallRpu = 394;
    constexpr int kTextBrowseNexus = 395;
    constexpr int kTextDownloads = 396;
    constexpr int kTextDownloadsReady = 397;

    constexpr size_t kNameLength = 24;

    bool gRestartRequested = false;

    // Restarts the game for the changed files, as the Replace game button
    // of the settings does (the app closes it and opens it again).
    void requestRestart(const char* mode)
    {
        if (mode == nullptr) {
            gRestartRequested = true;
        }
#ifdef __ANDROID__
        JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
        jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
        if (env == nullptr || activity == nullptr) {
            return;
        }
        jclass activityClass = env->GetObjectClass(activity);
        jmethodID method = env->GetMethodID(activityClass, "openGameFiles", "(Ljava/lang/String;)V");
        if (method != nullptr) {
            jstring value = mode != nullptr ? env->NewStringUTF(mode) : nullptr;
            env->CallVoidMethod(activity, method, value);
            if (value != nullptr) {
                env->DeleteLocalRef(value);
            }
        }
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        env->DeleteLocalRef(activityClass);
        env->DeleteLocalRef(activity);
#else
        if (mode == nullptr) {
            debugPrint("MODS restart requested\n");
        } else {
            debugPrint("MODS app screen requested: %s\n", mode);
        }
#endif
    }

    // Downloads finished and not installed yet (the app's download index);
    // asked of the activity, 0 on desktop.
    int downloadsReady()
    {
#ifdef __ANDROID__
        JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
        jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
        if (env == nullptr || activity == nullptr) {
            return 0;
        }
        int count = 0;
        jclass activityClass = env->GetObjectClass(activity);
        jmethodID method = env->GetMethodID(activityClass, "downloadsReady", "()I");
        if (method != nullptr) {
            count = env->CallIntMethod(activity, method);
        }
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            count = 0;
        }
        env->DeleteLocalRef(activityClass);
        env->DeleteLocalRef(activity);
        return count;
#else
        return 0;
#endif
    }

    // Asked once per count: later opens of the Mods screen stay quiet until
    // more downloads are ready.
    int gDownloadsPrompted = 0;

    std::u32string text(int id, const char* fallback)
    {
        return muiDecodeGameText(muiText(id, fallback));
    }

    class ModsScreen : public MuiScreen {
    public:
        ModsScreen()
        {
            modal = true;
            reload();
        }

        void build(MuiContext& ui) override;
        void back() override
        {
            if (editing) {
                closeEditor(false);
            } else if (picking) {
                picking = false;
            } else {
                pendingBack = true;
            }
        }
        bool coversScreen() override { return true; }
        void key(int keyCode) override
        {
            if (!editing) {
                return;
            }
            if (keyCode == KEY_BACKSPACE || keyCode == KEY_DELETE) {
                if (!name.empty()) {
                    name.pop_back();
                }
            } else if (keyCode == KEY_RETURN) {
                closeEditor(true);
            }
        }

        void runPending();

        bool finished = false;

    private:
        void reload()
        {
            list.entries.clear();
            modProfileLoadList(&list);
        }
        void buildRows(MuiContext& ui, const MuiRect& rect);
        void buildPicker(MuiContext& ui);
        void buildEditor(MuiContext& ui);
        void closeEditor(bool accept);
        void save()
        {
            if (changed) {
                modProfileSaveList(list);
            }
        }

        ModList list;
        // The list or the profile changed: the game restarts when leaving.
        bool changed = false;

        bool picking = false;
        std::vector<std::string> names;
        int pickSelected = 0;

        enum class Edit { None,
            New,
            Rename };
        Edit editing_ = Edit::None;
        bool editing = false;
        std::u32string name;

        bool pendingBack = false;
        bool pendingAdd = false;
        bool pendingRpu = false;
        bool pendingNexus = false;
        bool pendingDownloads = false;
        bool promptChecked = false;
        bool pendingSwitch = false;
        bool pendingDelete = false;
        bool pendingCreate = false;
        bool pendingRename = false;
        bool failed = false;
        int dropTarget = -1;
    };

    void ModsScreen::closeEditor(bool accept)
    {
        editing = false;
        endTextInput();
        if (!accept || name.empty()) {
            return;
        }
        if (editing_ == Edit::New) {
            pendingCreate = true;
        } else if (editing_ == Edit::Rename) {
            pendingRename = true;
        }
    }

    void ModsScreen::build(MuiContext& ui)
    {
        const MuiTheme& theme = muiTheme();
        muiFillRect(ui.screenRect(), theme.background);

        if (editing) {
            buildEditor(ui);
            return;
        }

        MuiRect safe = ui.safeRect().inset(ui.dp(10.0f));
        float gap = ui.dp(8.0f);
        float headerHeight = ui.dp(44.0f);
        float footerHeight = ui.dp(46.0f);

        // Header: the title and the profile picker.
        MuiRect header = { safe.x, safe.y, safe.w, headerHeight };
        float pickerWidth = std::min(header.w * 0.6f, ui.dp(260.0f));
        MuiRect title = { header.x, header.y, header.w - pickerWidth - gap, header.h };
        muiDrawTextAligned(text(kTextMods, "Mods"), title, ui.dp(20.0f), theme.accent, MuiAlign::Start, MuiAlign::Center);

        std::u32string profileLabel = text(kTextProfile, "Profile") + U": " + muiDecodeGameText(modProfileActive().c_str());
        MuiRect picker = { header.right() - pickerWidth, header.y, pickerWidth, header.h };
        if (ui.button("mods.profile", picker, profileLabel) && !picking) {
            picking = true;
            names = modProfileNames();
            pickSelected = 0;
            for (size_t index = 0; index < names.size(); index++) {
                if (names[index] == modProfileActive()) {
                    pickSelected = static_cast<int>(index);
                }
            }
        }

        // Footer: add from zip, install RPU, browse Nexus, downloads, back.
        // One row when the five buttons stay wide enough, else two rows.
        const std::u32string labels[5] = {
            text(kTextAddZip, "Add from zip"),
            text(kTextInstallRpu, "Install RPU"),
            text(kTextBrowseNexus, "Browse Nexus"),
            text(kTextDownloads, "Downloads"),
            text(kTextBack, "Back"),
        };
        const char* ids[5] = { "mods.add", "mods.rpu", "mods.nexus", "mods.downloads", "mods.back" };
        bool pressed[5] = { false, false, false, false, false };
        float fifth = (safe.w - gap * 4.0f) / 5.0f;
        bool twoRows = fifth < ui.dp(130.0f);
        float footerTotal = twoRows ? footerHeight * 2.0f + gap : footerHeight;
        MuiRect footer = { safe.x, safe.bottom() - footerTotal, safe.w, footerTotal };
        // RPU is a Fallout 2 mod: the button is hidden for the other games.
        const bool rpuAvailable = gameActiveIndex() == 0;
        for (int i = 0; i < 5; i++) {
            if (i == 1 && !rpuAvailable) continue;
            MuiRect cell;
            if (!twoRows) {
                cell = { footer.x + (fifth + gap) * i, footer.y, fifth, footerHeight };
            } else if (i < 3) {
                float third = (footer.w - gap * 2.0f) / 3.0f;
                cell = { footer.x + (third + gap) * i, footer.y, third, footerHeight };
            } else {
                float half = (footer.w - gap) / 2.0f;
                cell = { footer.x + (half + gap) * (i - 3), footer.y + footerHeight + gap, half, footerHeight };
            }
            pressed[i] = ui.button(ids[i], cell, labels[i], i == 4 ? MuiButtonStyle::Primary : MuiButtonStyle::Normal);
        }
        pendingAdd |= pressed[0];
        pendingRpu |= pressed[1];
        pendingNexus |= pressed[2];
        pendingDownloads |= pressed[3];
        pendingBack |= pressed[4];

        float noteHeight = ui.dp(18.0f);
        MuiRect note = { safe.x, footer.y - noteHeight - gap, safe.w, noteHeight };
        if (changed) {
            std::u32string label = text(kTextRestartNote, "The game restarts when you leave.");
            muiDrawTextAligned(label, note, muiFitTextSize(label, note.w, ui.dp(12.0f), ui.dp(9.0f)), theme.textDim, MuiAlign::Start, MuiAlign::Center);
        }

        MuiRect rows = { safe.x, header.bottom() + gap, safe.w, note.y - header.bottom() - gap * 2.0f };
        buildRows(ui, rows);

        if (picking) {
            buildPicker(ui);
        }
    }

    void ModsScreen::buildRows(MuiContext& ui, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();
        float rowHeight = ui.dp(52.0f);
        float rowGap = ui.dp(4.0f);
        int count = static_cast<int>(list.entries.size());

        if (count == 0) {
            std::u32string label = text(kTextNoMods, "No mods installed yet. Add one from a zip, install RPU or browse Nexus.");
            muiDrawTextAligned(label, rect, ui.dp(14.0f), theme.textDim, MuiAlign::Center, MuiAlign::Center);
            return;
        }

        float contentHeight = count * (rowHeight + rowGap);
        float offset = ui.scroll("mods.row", rect, contentHeight);

        // A drag released over a row moves the mod there.
        int payload = 0;
        int dragged = -1;
        bool isDragging = ui.dragging(&payload);
        if (isDragging) {
            dragged = payload;
        }
        int dropFrom = -1;
        int dropTo = -1;

        for (int index = 0; index < count; index++) {
            MuiRect row = { rect.x, rect.y + index * (rowHeight + rowGap) - offset, rect.w, rowHeight };
            if (row.bottom() < rect.y || row.y > rect.bottom()) {
                continue;
            }
            std::string prefix = "mods.row." + std::to_string(index);

            bool hover = isDragging && ui.pointerY() >= row.y && ui.pointerY() < row.bottom();
            muiFillRoundRect(row, ui.dp(theme.radius), hover ? theme.selected : theme.button);
            muiStrokeRoundRect(row, ui.dp(theme.radius), ui.dp(1.0f), index == dragged ? theme.accent : theme.buttonBorder);

            float pad = ui.dp(8.0f);
            float small = ui.dp(40.0f);
            MuiRect toggleRect = { row.x + pad, row.centerY() - ui.dp(14.0f), ui.dp(52.0f), ui.dp(28.0f) };
            bool enabled = list.entries[index].enabled;
            if (ui.toggle(prefix + ".toggle", toggleRect, &enabled)) {
                list.entries[index].enabled = enabled;
                changed = true;
            }

            MuiRect handle = { row.right() - pad - small, row.y, small, row.h };
            MuiRect down = { handle.x - small - ui.dp(2.0f), row.centerY() - small / 2.0f, small, small };
            MuiRect up = { down.x - small - ui.dp(2.0f), down.y, small, small };
            MuiRect label = { toggleRect.right() + pad, row.y, up.x - toggleRect.right() - pad * 2.0f, row.h };

            std::u32string id = muiDecodeGameText(list.entries[index].id.c_str());
            muiDrawTextAligned(id, label, muiFitTextSize(id, label.w, ui.dp(15.0f), ui.dp(10.0f)), enabled ? theme.text : theme.textDim, MuiAlign::Start, MuiAlign::Center);

            if (ui.button(prefix + ".up", up, U"^") && index > 0) {
                dropFrom = index;
                dropTo = index - 1;
            }
            if (ui.button(prefix + ".down", down, U"v") && index + 1 < count) {
                dropFrom = index;
                dropTo = index + 1;
            }

            // Drag handle: three lines; holding it and moving drags the row.
            ui.touchable(prefix + ".drag", handle);
            ui.dragSource(prefix + ".drag", index, true);
            for (int line = -1; line <= 1; line++) {
                muiFillRect({ handle.centerX() - ui.dp(10.0f), handle.centerY() + line * ui.dp(6.0f) - ui.dp(1.0f), ui.dp(20.0f), ui.dp(2.0f) }, theme.textDim);
            }

            int dropped = 0;
            if (ui.dropped(row, &dropped) && dropped != index) {
                dropFrom = dropped;
                dropTo = index;
            }
        }

        if (dropFrom != -1) {
            modListMove(&list, static_cast<size_t>(dropFrom), static_cast<size_t>(dropTo));
            changed = true;
        }
    }

    void ModsScreen::buildPicker(MuiContext& ui)
    {
        const MuiTheme& theme = muiTheme();
        ui.dim();

        MuiRect safe = ui.safeRect().inset(ui.dp(10.0f));
        float gap = ui.dp(6.0f);
        float rowHeight = ui.dp(40.0f);
        float buttonHeight = ui.dp(42.0f);
        float padding = ui.dp(10.0f);
        float width = std::min(safe.w, ui.dp(340.0f));

        int count = static_cast<int>(names.size());
        float listHeight = std::min(count * (rowHeight + gap), std::max(safe.h - padding * 2.0f - buttonHeight * 2.0f - gap * 3.0f, rowHeight));
        float height = padding * 2.0f + listHeight + gap + buttonHeight + gap + buttonHeight;
        MuiRect card = { safe.centerX() - width / 2.0f, safe.centerY() - height / 2.0f, width, height };
        if (ui.tappedOutside(card)) {
            picking = false;
            return;
        }
        ui.panel(card);

        MuiRect inner = card.inset(padding);
        MuiRect listRect = { inner.x, inner.y, inner.w, listHeight };
        float offset = ui.scroll("mods.profile.list", listRect, count * (rowHeight + gap));
        for (int index = 0; index < count; index++) {
            MuiRect row = { listRect.x, listRect.y + index * (rowHeight + gap) - offset, listRect.w, rowHeight };
            if (row.bottom() < listRect.y || row.y > listRect.bottom()) {
                continue;
            }
            std::u32string label = muiDecodeGameText(names[index].c_str());
            if (names[index] == modProfileActive()) {
                label += U"  *";
            }
            bool pressed = false;
            if (ui.touchable("mods.profile.list." + std::to_string(index), row, &pressed)) {
                pickSelected = index;
            }
            muiFillRoundRect(row, ui.dp(theme.radius), index == pickSelected ? theme.selected : (pressed ? theme.buttonPressed : theme.button));
            muiStrokeRoundRect(row, ui.dp(theme.radius), ui.dp(1.0f), index == pickSelected ? theme.accent : theme.buttonBorder);
            muiDrawTextAligned(label, row.inset(ui.dp(10.0f), 0.0f), ui.dp(15.0f), theme.text, MuiAlign::Start, MuiAlign::Center);
        }

        bool selectedActive = pickSelected >= 0 && pickSelected < count && names[pickSelected] == modProfileActive();
        bool selectedVanilla = pickSelected >= 0 && pickSelected < count && names[pickSelected] == kModProfileVanilla;

        float y = listRect.bottom() + gap;
        MuiRect switchButton = { inner.x, y, inner.w, buttonHeight };
        if (ui.button("mods.profile.switch", switchButton, text(kTextSwitch, "Switch"), selectedActive ? MuiButtonStyle::Normal : MuiButtonStyle::Primary) && !selectedActive) {
            pendingSwitch = true;
        }
        y += buttonHeight + gap;
        float third = (inner.w - gap * 2.0f) / 3.0f;
        MuiRect newButton = { inner.x, y, third, buttonHeight };
        MuiRect renameButton = { newButton.right() + gap, y, third, buttonHeight };
        MuiRect deleteButton = { renameButton.right() + gap, y, third, buttonHeight };
        if (ui.button("mods.profile.new", newButton, text(kTextNew, "New"))) {
            editing = true;
            editing_ = Edit::New;
            name.clear();
            beginTextInput();
        }
        if (ui.button("mods.profile.rename", renameButton, text(kTextRename, "Rename")) && !selectedActive && !selectedVanilla) {
            editing = true;
            editing_ = Edit::Rename;
            name = muiDecodeGameText(names[pickSelected].c_str());
            beginTextInput();
        }
        if (ui.button("mods.profile.delete", deleteButton, text(kTextDelete, "Delete")) && !selectedActive && !selectedVanilla) {
            pendingDelete = true;
        }
    }

    void ModsScreen::buildEditor(MuiContext& ui)
    {
        for (char32_t ch : ui.takeTextInput()) {
            if (ch >= 0x20 && ch != U'/' && ch != U'\\' && ch != U':'
                && !muiEncodeGameText(std::u32string(1, ch)).empty()
                && muiEncodeGameText(name).size() < kNameLength) {
                name.push_back(ch);
            }
        }

        const MuiTheme& theme = muiTheme();
        ui.dim();

        MuiRect safe = ui.safeRect().inset(ui.dp(9.0f));
        float width = std::min(safe.w, ui.dp(540.0f));
        MuiRect card = { safe.centerX() - width / 2.0f, safe.y, width, ui.dp(104.0f) };
        ui.panel(card);

        MuiRect inner = card.inset(ui.dp(10.0f));
        muiDrawTextAligned(text(kTextProfileName, "Profile name"), { inner.x, inner.y, inner.w, ui.dp(22.0f) }, ui.dp(13.0f), theme.textDim, MuiAlign::Start, MuiAlign::Center);

        float doneWidth = ui.dp(102.0f);
        MuiRect field = { inner.x, inner.y + ui.dp(29.0f), inner.w - doneWidth - ui.dp(6.0f), ui.dp(44.0f) };
        muiFillRoundRect(field, ui.dp(6.0f), theme.background);
        muiStrokeRoundRect(field, ui.dp(6.0f), ui.dp(1.0f), theme.accent);
        muiDrawTextAligned(name, field.inset(ui.dp(8.0f), 0.0f), ui.dp(17.0f), theme.accent, MuiAlign::Start, MuiAlign::Center);
        if (ui.touchable("mods.name.field", field)) {
            beginTextInput();
        }
        if (ui.button("mods.name.done", { field.right() + ui.dp(6.0f), field.y, doneWidth, field.h }, text(kTextDone, "Done"), MuiButtonStyle::Primary)) {
            closeEditor(true);
        }
    }

    // Game-thread actions, run outside the build.
    void ModsScreen::runPending()
    {
        if (pendingSwitch) {
            pendingSwitch = false;
            if (pickSelected >= 0 && pickSelected < static_cast<int>(names.size())) {
                // The list as edited so far belongs to the profile left.
                save();
                if (modProfileSetActive(names[pickSelected])) {
                    reload();
                    changed = true;
                } else {
                    failed = true;
                }
            }
            picking = false;
        }

        if (pendingCreate) {
            pendingCreate = false;
            std::string value = muiEncodeGameText(name);
            if (modProfileCreate(value, false)) {
                names = modProfileNames();
                for (size_t index = 0; index < names.size(); index++) {
                    if (names[index] == value) {
                        pickSelected = static_cast<int>(index);
                    }
                }
            } else {
                failed = true;
            }
        }

        if (pendingRename) {
            pendingRename = false;
            if (pickSelected >= 0 && pickSelected < static_cast<int>(names.size())) {
                if (modProfileRename(names[pickSelected], muiEncodeGameText(name))) {
                    names = modProfileNames();
                } else {
                    failed = true;
                }
            }
        }

        if (pendingDelete) {
            pendingDelete = false;
            if (pickSelected >= 0 && pickSelected < static_cast<int>(names.size())) {
                const char* body[] = { muiText(kTextDeleteAsk, "Delete this profile with its saves?") };
                if (showDialogBox(names[pickSelected].c_str(), body, 1, 0, 0, COLOR_AMBER, nullptr, COLOR_AMBER, DIALOG_BOX_YES_NO) != 0) {
                    if (modProfileDelete(names[pickSelected])) {
                        names = modProfileNames();
                        pickSelected = 0;
                    } else {
                        failed = true;
                    }
                }
            }
        }

        if (failed) {
            failed = false;
            const char* body[] = { muiText(kTextProfileFailed, "The profile could not be changed.") };
            showDialogBox(muiText(kTextMods, "Mods"), body, 1, 0, 0, COLOR_AMBER, nullptr, COLOR_AMBER, 0);
        }

        if (pendingAdd) {
            pendingAdd = false;
            // The app's screen installs the zip into the saved list.
            save();
            if (changed) {
                finished = true;
            }
            requestRestart("mod");
        }

        if (!promptChecked) {
            promptChecked = true;
            int ready = downloadsReady();
            if (ready > 0 && ready != gDownloadsPrompted) {
                gDownloadsPrompted = ready;
                char line[160];
                std::snprintf(line, sizeof(line), muiText(kTextDownloadsReady, "%d downloads ready to install. Open Downloads?"), ready);
                const char* body[] = { line };
                if (showDialogBox(muiText(kTextDownloads, "Downloads"), body, 1, 0, 0, COLOR_AMBER, nullptr, COLOR_AMBER, DIALOG_BOX_YES_NO) != 0) {
                    pendingDownloads = true;
                }
            }
        }

        if (pendingRpu || pendingNexus || pendingDownloads) {
            // The app's RPU download (mode "rpu"), Nexus browser (mode
            // "nexus") or Downloads (mode "downloads") screen installs into
            // the saved list.
            const char* mode = pendingRpu ? "rpu" : pendingNexus ? "nexus"
                                                                 : "downloads";
            pendingRpu = false;
            pendingNexus = false;
            pendingDownloads = false;
            save();
            if (changed) {
                finished = true;
            }
            requestRestart(mode);
        }

        if (pendingBack) {
            pendingBack = false;
            if (changed) {
                save();
                requestRestart(nullptr);
            }
            finished = true;
        }
    }

} // namespace

void muiRequestAppRestart()
{
    gRestartRequested = true;
#ifdef __ANDROID__
    // Closes the game's process and opens the game again (not the import screen).
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (env == nullptr || activity == nullptr) {
        return;
    }
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID method = env->GetMethodID(activityClass, "restartApp", "()V");
    if (method != nullptr) {
        env->CallVoidMethod(activity, method);
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(activityClass);
    env->DeleteLocalRef(activity);
#else
    requestRestart(nullptr);
#endif
}

bool muiModsRestartRequested()
{
    return gRestartRequested;
}

void muiModsScreenRun()
{
    ModsScreen screen;
    muiPush(&screen);

    while (!screen.finished) {
        sharedFpsLimiter.mark();

        int keyCode = inputGetInput();
        if (keyCode == KEY_ESCAPE) {
            screen.back();
        }

        if (_game_user_wants_to_quit != GAME_QUIT_REQUEST_NONE) {
            screen.finished = true;
        }

        devAutotestTick();
        renderPresent();

        screen.runPending();

        sharedFpsLimiter.throttle();
    }

    muiRemove(&screen);
}

} // namespace fallout
