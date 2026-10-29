#include "mui_combat_panel.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "actions.h"
#include "combat.h"
#include "combat_defs.h"
#include "critter.h"
#include "game_commands.h"
#include "game_sound.h"
#include "interface.h"
#include "inventory.h"
#include "item.h"
#include "message.h"
#include "mui_draw.h"
#include "mui_dual.h"
#include "mui_gameskin.h"
#include "mui_screens.h"
#include "mui_skin.h"
#include "object.h"
#include "perk.h"
#include "proto.h"
#include "proto_instance.h"
#include "settings.h"
#include "sfall_script_hooks.h"
#include "stat.h"

namespace fallout {

namespace {

    // Texts in `game\ce.msg` (combat lane: 1020..1039).
    constexpr int kTextEnemies = 1020;
    constexpr int kTextAim = 1021;
    constexpr int kTextEndTurn = 1022;
    constexpr int kTextEndCombat = 1023;
    constexpr int kTextNoEnemies = 1024;
    constexpr int kTextEnemyTurn = 1025;
    constexpr int kTextBack = 1026;
    constexpr int kTextAimedShot = 1027;
    constexpr int kTextHexes = 1028;
    constexpr int kTextQuickUse = 1029;

    const MuiColor kAmber = muiRgb(0xFFB000);
    const MuiColor kRed = muiRgb(0xFF4A3A);
    const MuiColor kGreen = muiRgb(0x3CE03C);
    const MuiColor kYellow = muiRgb(0xF0D030);

    std::u32string text(int id, const char* fallback)
    {
        return muiDecodeGameText(muiText(id, fallback));
    }

    // MARK: Commands

    enum PanelOp {
        kOpAttack = 1,
        kOpAimedAttack = 2,
        kOpQuickUse = 3,
    };

    Object* gCommandTarget = nullptr;
    HitLocation gCommandLocation = HIT_LOCATION_TORSO;
    int gCommandSlot = -1;

    // MARK: Quick-use slots

    int gSlots[kQuickUseSlotCount] = { -1, -1, -1, -1, -1, -1 };
    bool gSlotsLoaded = false;

    void loadSlots()
    {
        if (gSlotsLoaded) {
            return;
        }
        gSlotsLoaded = true;

        const char* cursor = settings.touch.hotbar_slots.c_str();
        for (int index = 0; index < kQuickUseSlotCount; index++) {
            char* end;
            long value = strtol(cursor, &end, 10);
            if (end == cursor) {
                break;
            }
            gSlots[index] = static_cast<int>(value);
            cursor = *end == ',' ? end + 1 : end;
        }
    }

    void saveSlots()
    {
        std::string value;
        for (int index = 0; index < kQuickUseSlotCount; index++) {
            if (index != 0) {
                value += ",";
            }
            value += std::to_string(gSlots[index]);
        }
        settings.touch.hotbar_slots = value;
        settingsSave();
    }

    Object* slotItem(int slot)
    {
        loadSlots();
        if (gSlots[slot] < 0 || gDude == nullptr) {
            return nullptr;
        }
        return objectGetCarriedObjectByProtoId(gDude, ProtoId(gSlots[slot]));
    }

    int slotCount(int slot)
    {
        loadSlots();
        if (gSlots[slot] < 0 || gDude == nullptr) {
            return 0;
        }
        return objectGetCarriedQuantityByProtoId(gDude, ProtoId(gSlots[slot]));
    }

    bool dudeTurn()
    {
        return !isInCombat() || _combat_whose_turn() == gDude;
    }

    // Action points the slot's item takes with the game's rules: using a
    // consumable takes what an item costs, reloading what the weapon's
    // reload costs, taking a weapon in hand what opening the inventory does.
    int slotCost(Object* item)
    {
        if (item == nullptr || !isInCombat()) {
            return 0;
        }

        switch (itemGetType(item)) {
        case ITEM_TYPE_AMMO: {
            Object* weapon = nullptr;
            interfaceGetActiveItem(&weapon);
            HitMode mode = interfaceGetCurrentHand() == HAND_LEFT ? HIT_MODE_LEFT_WEAPON_RELOAD : HIT_MODE_RIGHT_WEAPON_RELOAD;
            return itemGetActionPointCost(gDude, mode, false);
        }
        case ITEM_TYPE_WEAPON:
            return inventoryGetInvenApCost();
        default:
            return 2;
        }
    }

    // Whether the slot can be used now, and why not.
    bool slotUsable(int slot)
    {
        Object* item = slotItem(slot);
        if (item == nullptr) {
            return false;
        }

        if (isInCombat()) {
            if (!dudeTurn() || (gCombatState & COMBAT_STATE_PLAYER_TURN) == 0) {
                return false;
            }
            if (slotCost(item) > gDude->data.critter.combat.ap) {
                return false;
            }
        }

        if (itemGetType(item) == ITEM_TYPE_AMMO) {
            Object* weapon = nullptr;
            if (interfaceGetActiveItem(&weapon) == -1 || weapon == nullptr || itemGetType(weapon) != ITEM_TYPE_WEAPON) {
                return false;
            }
            return weaponCanBeReloadedWith(weapon, item);
        }

        return true;
    }

    void useSlot(int slot)
    {
        if (!slotUsable(slot)) {
            return;
        }

        Object* item = slotItem(slot);
        int cost = slotCost(item);

        switch (itemGetType(item)) {
        case ITEM_TYPE_AMMO:
            // The game's reload of the active weapon (the HUD's reload).
            if (interfaceSetCurrentItemAction(INTERFACE_ITEM_ACTION_RELOAD)) {
                _intface_use_item();
            }
            return;
        case ITEM_TYPE_WEAPON:
            // Into the active hand.
            if (inventoryEquip(gDude, item, interfaceGetCurrentHand()) != -1) {
                interfaceUpdateItems(false, INTERFACE_ITEM_ACTION_DEFAULT, INTERFACE_ITEM_ACTION_DEFAULT);
                if (isInCombat() && cost > 0) {
                    gDude->data.critter.combat.ap = std::max(gDude->data.critter.combat.ap - cost, 0);
                    interfaceRenderActionPoints(gDude->data.critter.combat.ap, _combat_free_move);
                }
            }
            return;
        case ITEM_TYPE_DRUG:
            if (drugItemTakeDrug(gDude, item)) {
                itemRemoveWithReason(gDude, item, 1, RemoveInventoryObjectHookReason::ConsumeDrug);
                _obj_connect(item, gDude->tile, gDude->elevation, nullptr);
                objectDestroy(item);
                interfaceRenderHitPoints(true);
                if (isInCombat()) {
                    gDude->data.critter.combat.ap = std::max(gDude->data.critter.combat.ap - cost, 0);
                    interfaceRenderActionPoints(gDude->data.critter.combat.ap, _combat_free_move);
                }
            }
            return;
        default:
            if (_action_use_an_item_on_object(gDude, gDude, item) != -1 && isInCombat()) {
                gDude->data.critter.combat.ap = std::max(gDude->data.critter.combat.ap - cost, 0);
                interfaceRenderActionPoints(gDude->data.critter.combat.ap, _combat_free_move);
            }
            return;
        }
    }

    // MARK: Enemies

    struct Enemy {
        Object* critter;
        int distance;
        // -1 - can't be attacked now.
        int chance;
        bool aimable;
        std::u32string name;
        std::u32string state;
    };

    std::vector<Object*> gFound;

    void collect(Object* critter)
    {
        gFound.push_back(critter);
    }

    // The game's words for how a critter is doing (the look text's).
    std::u32string healthState(Object* critter)
    {
        int maximum = critterGetStat(critter, STAT_MAXIMUM_HIT_POINTS);
        int current = critterGetStat(critter, STAT_CURRENT_HIT_POINTS);
        int level;
        if (maximum <= 0 || current <= 0 || critterIsDead(critter)) {
            level = 0;
        } else if (current == maximum) {
            level = 4;
        } else {
            level = (current * 3) / maximum + 1;
        }

        MessageListItem item;
        item.num = 500 + level;
        std::u32string result;
        if (messageListGetItem(&gProtoMessageList, &item)) {
            result = muiDecodeGameText(item.text);
        }
        if (perkGetRank(gDude, PERK_AWARENESS)) {
            char numbers[32];
            snprintf(numbers, sizeof(numbers), " %d/%d", current, maximum);
            result += muiDecodeUtf8(numbers);
        }
        return result;
    }

    bool currentHitMode(HitMode* mode, bool* aiming)
    {
        return interfaceGetCurrentHitMode(mode, aiming) != -1 && *mode != HIT_MODE_INVALID;
    }

    std::vector<Enemy> enemies()
    {
        std::vector<Enemy> result;
        if (gDude == nullptr) {
            return result;
        }

        gFound.clear();
        combatForEachCritter(collect);

        HitMode mode = HIT_MODE_INVALID;
        bool aiming = false;
        bool haveMode = currentHitMode(&mode, &aiming);

        // The weapon has an aimed action.
        InterfaceItemAction actions[INTERFACE_ITEM_ACTION_COUNT];
        int actionCount = interfaceGetAvailableItemActions(actions, INTERFACE_ITEM_ACTION_COUNT);
        bool canAim = false;
        for (int index = 0; index < actionCount; index++) {
            canAim = canAim || actions[index] == INTERFACE_ITEM_ACTION_PRIMARY_AIMING || actions[index] == INTERFACE_ITEM_ACTION_SECONDARY_AIMING;
        }

        for (Object* critter : gFound) {
            if (critter == gDude || critter->data.critter.combat.team == gDude->data.critter.combat.team) {
                continue;
            }
            if (critterIsDead(critter) || !_can_see(gDude, critter)) {
                continue;
            }

            Enemy enemy;
            enemy.critter = critter;
            enemy.distance = objectGetDistanceBetween(gDude, critter);
            enemy.chance = -1;
            enemy.aimable = false;
            if (haveMode) {
                bool aimed = aiming;
                CombatBadShot bad = _combat_check_bad_shot(gDude, critter, mode, aimed);
                if (bad == COMBAT_BAD_SHOT_OK || bad == COMBAT_BAD_SHOT_NOT_ENOUGH_AP) {
                    enemy.chance = _determine_to_hit(gDude, critter, HIT_LOCATION_UNCALLED, mode);
                }
                enemy.aimable = canAim && _combat_check_bad_shot(gDude, critter, mode, true) == COMBAT_BAD_SHOT_OK;
            }
            enemy.name = muiDecodeGameText(objectGetName(critter));
            enemy.state = healthState(critter);
            result.push_back(std::move(enemy));
        }

        std::sort(result.begin(), result.end(), [](const Enemy& a, const Enemy& b) { return a.distance < b.distance; });
        return result;
    }

    MuiColor chanceColor(int chance)
    {
        const MuiTheme& theme = muiTheme();
        if (chance < 0) {
            return theme.textDim;
        }
        return chance >= 60 ? kGreen : (chance >= 30 ? kYellow : kRed);
    }

    // MARK: Drawing

    void drawButton(MuiContext& ui, const std::string& id, const MuiRect& rect, const std::u32string& label, bool enabled, bool primary, bool* tapped)
    {
        const MuiTheme& theme = muiTheme();
        bool pressed = false;
        bool hit = enabled ? ui.touchable(id, rect, &pressed) : false;
        if (!enabled) {
            ui.mark(id, rect);
        }
        themedButton(ui, rect, pressed, primary && enabled);
        float size = muiFitTextSize(label, rect.w - ui.dp(12.0f), ui.dp(18.0f), ui.dp(11.0f));
        muiDrawTextAligned(label, rect, size, enabled ? (pressed ? theme.accent : theme.text) : theme.textDim.withAlpha(150), MuiAlign::Center, MuiAlign::Center);
        if (tapped != nullptr) {
            *tapped = hit;
        }
    }

    // AP bulbs: one cell per action point, the next [cost] of the left in
    // amber, the rest green, the spent ones dark.
    void drawActionPoints(MuiContext& ui, const MuiRect& rect, int cost)
    {
        const MuiTheme& theme = muiTheme();
        themedWell(ui, rect);

        int ap = gDude->data.critter.combat.ap;
        int maximum = std::max(critterGetStat(gDude, STAT_MAXIMUM_ACTION_POINTS), ap);
        int bonus = isInCombat() ? std::max(_combat_free_move, 0) : 0;

        std::string label = "AP " + std::to_string(ap) + "/" + std::to_string(maximum);
        if (bonus > 0) {
            label += " +" + std::to_string(bonus);
        }
        float labelSize = ui.dp(18.0f);
        float labelWidth = muiTextWidth(muiDecodeUtf8(label.c_str()), labelSize) + ui.dp(20.0f);
        MuiRect labelRect = { rect.x + ui.dp(8.0f), rect.y, labelWidth, rect.h };
        muiDrawTextAligned(muiDecodeUtf8(label.c_str()), labelRect, labelSize, theme.accent, MuiAlign::Start, MuiAlign::Center);

        std::string costLabel;
        if (cost > 0) {
            costLabel = std::to_string(cost) + " AP";
        }
        float costWidth = costLabel.empty() ? 0.0f : muiTextWidth(muiDecodeUtf8(costLabel.c_str()), labelSize) + ui.dp(12.0f);
        if (!costLabel.empty()) {
            MuiRect costRect = { rect.right() - costWidth - ui.dp(6.0f), rect.y, costWidth, rect.h };
            muiDrawTextAligned(muiDecodeUtf8(costLabel.c_str()), costRect, labelSize, ap >= cost ? kAmber : kRed, MuiAlign::End, MuiAlign::Center);
        }

        MuiRect bar = { labelRect.right(), rect.y + ui.dp(8.0f), rect.right() - costWidth - ui.dp(14.0f) - labelRect.right(), rect.h - ui.dp(16.0f) };
        if (bar.w <= 0.0f || maximum <= 0) {
            return;
        }
        float gap = ui.dp(3.0f);
        float cell = (bar.w - gap * (maximum - 1)) / maximum;
        // The cost eats the lowest cells that are lit: the top ones.
        for (int index = 0; index < maximum; index++) {
            MuiRect cellRect = { bar.x + index * (cell + gap), bar.y, cell, bar.h };
            MuiColor color = muiRgb(0x1C261C);
            if (index < ap) {
                color = index >= ap - cost && cost > 0 ? kAmber : kGreen;
            }
            muiFillRect(cellRect, color);
        }
    }

    // MARK: Panel state

    int gAimEnemy = -1;
    Object* gAimCritter = nullptr;

    std::u32string weaponLine(MuiContext& ui, int* cost)
    {
        *cost = 0;
        InterfaceItemButtonInfo info;
        Object* item = nullptr;
        interfaceGetActiveItem(&item);
        std::u32string result = item != nullptr ? muiDecodeGameText(objectGetName(item)) : text(1030, "Unarmed");
        int quantity;
        int capacity;
        if (interfaceGetCurrentItemAmmo(&quantity, &capacity)) {
            result += muiDecodeUtf8((std::string("  ") + std::to_string(quantity) + "/" + std::to_string(capacity)).c_str());
        }
        HitMode mode;
        bool aiming;
        if (interfaceGetItemButtonInfo(&info) && info.actionPoints >= 0) {
            *cost = info.actionPoints;
        } else if (currentHitMode(&mode, &aiming)) {
            *cost = weaponGetActionPointCost(gDude, mode, aiming);
        }
        (void)ui;
        return result;
    }

} // namespace

bool muiCombatPanelEnabled()
{
    return settings.touch.combat_panel;
}

bool muiQuickUseEnabled()
{
    return settings.touch.hotbar;
}

bool muiCombatPanelActive()
{
    return muiCombatPanelEnabled() && isInCombat() && gDude != nullptr;
}

void muiCombatPanelBuild(MuiContext& ui, const MuiRect& rect, MuiRect* modes)
{
    const MuiTheme& theme = muiTheme();
    themedPanel(ui, rect, true);
    MuiRect inner = rect.inset(ui.dp(12.0f));
    float gap = ui.dp(6.0f);
    bool myTurn = dudeTurn() && (gCombatState & COMBAT_STATE_PLAYER_TURN) != 0;

    // Action points with the current action's cost.
    int cost;
    std::u32string weapon = weaponLine(ui, &cost);
    MuiRect apRect = { inner.x, inner.y, inner.w, ui.dp(34.0f) };
    drawActionPoints(ui, apRect, cost);

    // Weapon with its ammo and dude's HP at the left, the HUD's attack modes
    // at the right.
    float modeRowHeight = ui.dp(44.0f);
    MuiRect weaponRect = { inner.x, apRect.bottom() + gap, inner.w * 0.4f, modeRowHeight };
    MuiRect weaponName = { weaponRect.x, weaponRect.y, weaponRect.w, modeRowHeight * 0.5f };
    muiDrawTextAligned(weapon, weaponName, muiFitTextSize(weapon, weaponName.w, ui.dp(16.0f), ui.dp(10.0f)), theme.text, MuiAlign::Start, MuiAlign::Center);
    char hp[32];
    snprintf(hp, sizeof(hp), "HP %d/%d", critterGetHitPoints(gDude), critterGetStat(gDude, STAT_MAXIMUM_HIT_POINTS));
    MuiRect hpRect = { weaponRect.x, weaponName.bottom(), weaponRect.w, modeRowHeight * 0.5f };
    muiDrawTextAligned(muiDecodeUtf8(hp), hpRect, ui.dp(15.0f), muiDualHitPointsFraction() < 0.35f ? kRed : theme.textDim, MuiAlign::Start, MuiAlign::Center);
    *modes = { weaponRect.right() + gap, weaponRect.y, inner.right() - weaponRect.right() - gap, modeRowHeight };

    // End turn / end combat at the bottom, the hotbar over them.
    float buttonHeight = ui.dp(42.0f);
    MuiRect buttons = { inner.x, inner.bottom() - buttonHeight, inner.w, buttonHeight };
    MuiRect hotbar = { inner.x, buttons.y - gap - muiQuickUseHeight(ui), inner.w, muiQuickUseHeight(ui) };
    // The aimed shot's body takes the hotbar's room.
    if (muiQuickUseEnabled() && gAimCritter == nullptr) {
        muiQuickUseBuild(ui, hotbar);
    } else {
        hotbar = { inner.x, buttons.y, inner.w, 0.0f };
    }
    float half = (buttons.w - gap) / 2.0f;
    bool endTurn = false;
    bool endCombat = false;
    drawButton(ui, "combat.endturn", { buttons.x, buttons.y, half, buttons.h }, text(kTextEndTurn, "END TURN"), myTurn, false, &endTurn);
    drawButton(ui, "combat.endcombat", { buttons.x + half + gap, buttons.y, half, buttons.h }, text(kTextEndCombat, "END COMBAT"), myTurn, true, &endCombat);
    if (endTurn) {
        gameCommandPost(GameCommandType::EndTurn);
    }
    if (endCombat) {
        gameCommandPost(GameCommandType::EndCombat);
    }

    MuiRect area = { inner.x, modes->bottom() + gap, inner.w, hotbar.y - gap - modes->bottom() - gap };
    themedWell(ui, area);
    MuiRect list = area.inset(ui.dp(6.0f));

    std::vector<Enemy> found = enemies();

    // Aimed shot: the game's called-shot body for one enemy.
    if (gAimCritter != nullptr) {
        int index = -1;
        for (size_t i = 0; i < found.size(); i++) {
            if (found[i].critter == gAimCritter) {
                index = static_cast<int>(i);
            }
        }
        if (index == -1) {
            gAimCritter = nullptr;
        }
    }

    if (gAimCritter != nullptr) {
        Object* critter = gAimCritter;
        HitMode mode = HIT_MODE_INVALID;
        bool aiming = false;
        currentHitMode(&mode, &aiming);

        float headerHeight = ui.dp(40.0f);
        MuiRect back = { list.x, list.y, ui.dp(96.0f), headerHeight };
        bool tapped = false;
        drawButton(ui, "combat.aim.back", back, text(kTextBack, "BACK"), true, false, &tapped);
        if (tapped) {
            gAimCritter = nullptr;
            return;
        }

        int aimedCost = weaponGetActionPointCost(gDude, mode, true);
        std::u32string title = muiDecodeGameText(objectGetName(critter));
        char costText[48];
        snprintf(costText, sizeof(costText), "  %d AP", aimedCost);
        title += U" - " + text(kTextAimedShot, "AIMED SHOT") + muiDecodeUtf8(costText);
        MuiRect titleRect = { back.right() + ui.dp(10.0f), list.y, list.w - back.w - ui.dp(10.0f), headerHeight };
        muiDrawTextAligned(title, titleRect, muiFitTextSize(title, titleRect.w, ui.dp(18.0f), ui.dp(11.0f)), theme.accent, MuiAlign::Start, MuiAlign::Center);

        CalledShotTarget targets[kCalledShotTargetCount];
        calledShotGetTargets(critter, mode, targets);
        bool canShoot = myTurn && _combat_check_bad_shot(gDude, critter, mode, true) == COMBAT_BAD_SHOT_OK;

        MuiRect body = { list.x, back.bottom() + gap, list.w, list.bottom() - back.bottom() - gap };
        float columnWidth = (body.w - gap) / 2.0f;
        float rowHeight = std::min((body.h - gap * 3.0f) / 4.0f, ui.dp(60.0f));
        for (int row = 0; row < 4; row++) {
            for (int column = 0; column < 2; column++) {
                int part = row + column * 4;
                const CalledShotTarget& target = targets[part];
                MuiRect rowRect = { body.x + column * (columnWidth + gap), body.y + row * (rowHeight + gap), columnWidth, rowHeight };
                std::string id = "combat.part." + std::to_string(part);
                bool pressed = false;
                bool hit = canShoot && target.chance >= 0 ? ui.touchable(id, rowRect, &pressed) : false;
                if (!(canShoot && target.chance >= 0)) {
                    ui.mark(id, rowRect);
                }
                themedButton(ui, rowRect, pressed, false);

                std::string chanceText = target.chance >= 0 ? std::to_string(target.chance) + "%" : "--";
                std::u32string chance = muiDecodeUtf8(chanceText.c_str());
                float chanceSize = ui.dp(22.0f);
                float chanceWidth = muiTextWidth(U"100%", chanceSize) + ui.dp(8.0f);
                MuiRect chanceRect = { rowRect.right() - chanceWidth - ui.dp(8.0f), rowRect.y, chanceWidth, rowRect.h };
                muiDrawTextAligned(chance, chanceRect, chanceSize, canShoot ? chanceColor(target.chance) : theme.textDim, MuiAlign::End, MuiAlign::Center);
                std::u32string name = muiDecodeGameText(target.name);
                MuiRect nameRect = { rowRect.x + ui.dp(10.0f), rowRect.y, chanceRect.x - rowRect.x - ui.dp(12.0f), rowRect.h };
                muiDrawTextAligned(name, nameRect, muiFitTextSize(name, nameRect.w, ui.dp(17.0f), ui.dp(10.0f)), canShoot ? (pressed ? theme.accent : theme.text) : theme.textDim, MuiAlign::Start, MuiAlign::Center);

                if (hit) {
                    gCommandTarget = critter;
                    gCommandLocation = target.location;
                    gAimCritter = nullptr;
                    gameCommandPost(GameCommandType::Panel, kOpAimedAttack);
                    return;
                }
            }
        }
        return;
    }

    // The visible enemies, nearest first.
    if (found.empty()) {
        std::u32string none = myTurn ? text(kTextNoEnemies, "NO ENEMIES IN SIGHT") : text(kTextEnemyTurn, "ENEMY TURN");
        muiDrawTextAligned(none, area, ui.dp(20.0f), myTurn ? theme.textDim : kRed, MuiAlign::Center, MuiAlign::Center);
        return;
    }

    float rowHeight = ui.dp(62.0f);
    float rowGap = ui.dp(5.0f);
    float contentHeight = found.size() * (rowHeight + rowGap);
    float offset = ui.scroll("combat.list", list, contentHeight);
    muiPushClip(list);
    for (size_t index = 0; index < found.size(); index++) {
        const Enemy& enemy = found[index];
        MuiRect row = { list.x, list.y - offset + index * (rowHeight + rowGap), list.w, rowHeight };
        if (row.bottom() < list.y || row.y > list.bottom()) {
            continue;
        }

        float aimWidth = ui.dp(86.0f);
        MuiRect aimRect = { row.right() - aimWidth - ui.dp(4.0f), row.y + ui.dp(4.0f), aimWidth, row.h - ui.dp(8.0f) };
        MuiRect attackRect = { row.x, row.y, aimRect.x - row.x - ui.dp(4.0f), row.h };

        bool canAttack = myTurn && enemy.chance >= 0;
        std::string id = "combat.enemy." + std::to_string(index);
        bool pressed = false;
        bool hit = canAttack ? ui.touchable(id, attackRect, &pressed) : false;
        if (!canAttack) {
            ui.mark(id, attackRect);
        }
        themedButton(ui, attackRect, pressed, false);

        // Chance at the right of the row's button, name over the state, the
        // distance between.
        std::string chanceText = enemy.chance >= 0 ? std::to_string(enemy.chance) + "%" : "--";
        float chanceSize = ui.dp(26.0f);
        float chanceWidth = muiTextWidth(U"100%", chanceSize) + ui.dp(8.0f);
        MuiRect chanceRect = { attackRect.right() - chanceWidth - ui.dp(10.0f), attackRect.y, chanceWidth, attackRect.h };
        muiDrawTextAligned(muiDecodeUtf8(chanceText.c_str()), chanceRect, chanceSize, canAttack ? chanceColor(enemy.chance) : theme.textDim, MuiAlign::End, MuiAlign::Center);

        std::string distanceText = std::to_string(enemy.distance);
        std::u32string distance = muiDecodeUtf8(distanceText.c_str()) + U" " + text(kTextHexes, "hex");
        float distanceSize = ui.dp(16.0f);
        float distanceWidth = muiTextWidth(distance, distanceSize) + ui.dp(8.0f);
        MuiRect distanceRect = { chanceRect.x - distanceWidth - ui.dp(6.0f), attackRect.y, distanceWidth, attackRect.h };
        muiDrawTextAligned(distance, distanceRect, distanceSize, theme.accent, MuiAlign::End, MuiAlign::Center);

        MuiRect nameArea = { attackRect.x + ui.dp(12.0f), attackRect.y + ui.dp(6.0f), distanceRect.x - attackRect.x - ui.dp(18.0f), attackRect.h - ui.dp(12.0f) };
        MuiRect nameRect = { nameArea.x, nameArea.y, nameArea.w, nameArea.h * 0.55f };
        MuiRect stateRect = { nameArea.x, nameRect.bottom(), nameArea.w, nameArea.h * 0.45f };
        muiDrawTextAligned(enemy.name, nameRect, muiFitTextSize(enemy.name, nameRect.w, ui.dp(19.0f), ui.dp(11.0f)), canAttack ? (pressed ? theme.accent : theme.text) : theme.textDim, MuiAlign::Start, MuiAlign::Center);
        muiDrawTextAligned(enemy.state, stateRect, muiFitTextSize(enemy.state, stateRect.w, ui.dp(14.0f), ui.dp(10.0f)), theme.textDim, MuiAlign::Start, MuiAlign::Center);

        bool aim = false;
        bool aimEnabled = myTurn && enemy.aimable;
        drawButton(ui, "combat.aim." + std::to_string(index), aimRect, text(kTextAim, "AIM"), aimEnabled, true, &aim);

        if (hit && !ui.isScrolling("combat.list")) {
            gCommandTarget = enemy.critter;
            gameCommandPost(GameCommandType::Panel, kOpAttack);
        } else if (aim) {
            gAimCritter = enemy.critter;
        }
    }
    muiPopClip();
}

float muiQuickUseHeight(MuiContext& ui)
{
    return ui.dp(58.0f);
}

void muiQuickUseBuild(MuiContext& ui, const MuiRect& rect)
{
    const MuiTheme& theme = muiTheme();
    loadSlots();
    themedPanel(ui, rect, false);

    float gap = ui.dp(6.0f);
    MuiRect inner = rect.inset(ui.dp(5.0f));
    // One row when the strip is wide, a grid when it is a block.
    int columns = std::clamp(static_cast<int>((inner.w + gap) / (ui.dp(50.0f) + gap)), 1, kQuickUseSlotCount);
    int rows = (kQuickUseSlotCount + columns - 1) / columns;
    float cell = std::min((inner.w - gap * (columns - 1)) / columns, (inner.h - gap * (rows - 1)) / rows);
    cell = std::min(cell, ui.dp(72.0f));
    float totalWidth = cell * columns + gap * (columns - 1);
    float totalHeight = cell * rows + gap * (rows - 1);
    float x = inner.x + (inner.w - totalWidth) / 2.0f;
    float y = inner.y + (inner.h - totalHeight) / 2.0f;

    for (int slot = 0; slot < kQuickUseSlotCount; slot++) {
        MuiRect cellRect = { x + (slot % columns) * (cell + gap), y + (slot / columns) * (cell + gap), cell, cell };
        std::string id = "hotbar." + std::to_string(slot);
        bool assigned = gSlots[slot] >= 0;
        int count = assigned ? slotCount(slot) : 0;
        bool usable = assigned && slotUsable(slot);

        bool pressed = false;
        bool tapped = usable ? ui.touchable(id, cellRect, &pressed) : false;
        if (!usable) {
            ui.mark(id, cellRect);
        }
        themedWell(ui, cellRect);
        if (pressed) {
            muiStrokeRoundRect(cellRect, ui.dp(3.0f), ui.dp(2.0f), theme.accent);
        }

        if (assigned) {
            Proto* proto = nullptr;
            if (protoGetProto(ProtoId(gSlots[slot]), &proto) != -1 && proto != nullptr) {
                int width;
                int height;
                SDL_Texture* texture = muiArtTexture(FrmId(proto->item.inventoryFid), 0, &width, &height);
                if (texture != nullptr && width > 0 && height > 0) {
                    MuiRect area = cellRect.inset(ui.dp(5.0f));
                    float scale = std::min(area.w / width, area.h / height);
                    if (scale >= 1.0f) {
                        scale = std::floor(scale);
                    }
                    MuiRect art = { area.centerX() - width * scale / 2.0f, area.centerY() - height * scale / 2.0f, width * scale, height * scale };
                    muiDrawTexture(texture, art);
                    if (!usable) {
                        // Greyed out.
                        muiFillRect(cellRect.inset(ui.dp(2.0f)), muiRgb(0x050805, 170));
                    }
                }
            }

            std::string countText = "x" + std::to_string(count);
            float countSize = ui.dp(13.0f);
            MuiRect countRect = { cellRect.x, cellRect.bottom() - muiLineHeight(countSize) - ui.dp(3.0f), cellRect.w - ui.dp(5.0f), muiLineHeight(countSize) };
            muiDrawTextAligned(muiDecodeUtf8(countText.c_str()), countRect, countSize, count > 0 ? (usable ? theme.accent : theme.textDim) : kRed, MuiAlign::End, MuiAlign::Start);

            Object* item = slotItem(slot);
            if (usable && isInCombat() && item != nullptr) {
                int cost = slotCost(item);
                if (cost > 0) {
                    std::string costText = std::to_string(cost);
                    MuiRect costRect = { cellRect.x + ui.dp(5.0f), cellRect.y + ui.dp(2.0f), cellRect.w, muiLineHeight(countSize) };
                    muiDrawTextAligned(muiDecodeUtf8(costText.c_str()), costRect, countSize, kAmber, MuiAlign::Start, MuiAlign::Start);
                }
            }
        } else {
            // Empty: its number, dim.
            std::string number = std::to_string(slot + 1);
            muiDrawTextAligned(muiDecodeUtf8(number.c_str()), cellRect, ui.dp(22.0f), theme.textDim.withAlpha(110), MuiAlign::Center, MuiAlign::Center);
        }

        if (tapped) {
            gCommandSlot = slot;
            gameCommandPost(GameCommandType::Panel, kOpQuickUse);
        }
    }
}

bool muiQuickUseCanAssign(Object* item)
{
    if (item == nullptr || !muiQuickUseEnabled()) {
        return false;
    }

    switch (itemGetType(item)) {
    case ITEM_TYPE_DRUG:
    case ITEM_TYPE_AMMO:
    case ITEM_TYPE_WEAPON:
        return true;
    case ITEM_TYPE_MISC:
        return _obj_action_can_use(item);
    default:
        return false;
    }
}

void muiQuickUseAssign(int slot, Object* item)
{
    loadSlots();
    if (slot < 0 || slot >= kQuickUseSlotCount || item == nullptr) {
        return;
    }

    int pid = ProtoId(item).pid();
    for (int index = 0; index < kQuickUseSlotCount; index++) {
        if (gSlots[index] == pid) {
            gSlots[index] = -1;
        }
    }
    gSlots[slot] = pid;
    saveSlots();
}

void muiQuickUseClear(Object* item)
{
    loadSlots();
    int pid = ProtoId(item).pid();
    for (int index = 0; index < kQuickUseSlotCount; index++) {
        if (gSlots[index] == pid) {
            gSlots[index] = -1;
        }
    }
    saveSlots();
}

int muiQuickUseSlotOf(Object* item)
{
    loadSlots();
    int pid = ProtoId(item).pid();
    for (int index = 0; index < kQuickUseSlotCount; index++) {
        if (gSlots[index] == pid) {
            return index;
        }
    }
    return -1;
}

void muiCombatPanelRun(int value)
{
    switch (value) {
    case kOpAttack:
        if (gCommandTarget != nullptr && !critterIsDead(gCommandTarget)) {
            _combat_attack_this(gCommandTarget);
        }
        break;
    case kOpAimedAttack: {
        if (gCommandTarget == nullptr || critterIsDead(gCommandTarget)) {
            break;
        }

        // The aimed counterpart of the current action, the attack with the
        // chosen part.
        InterfaceItemAction previous = interfaceGetCurrentItemAction();
        InterfaceItemAction aimed = previous;
        if (previous == INTERFACE_ITEM_ACTION_PRIMARY) {
            aimed = INTERFACE_ITEM_ACTION_PRIMARY_AIMING;
        } else if (previous == INTERFACE_ITEM_ACTION_SECONDARY) {
            aimed = INTERFACE_ITEM_ACTION_SECONDARY_AIMING;
        }
        if (aimed != previous) {
            interfaceSetCurrentItemAction(aimed);
        }
        combatSetPresetHitLocation(gCommandLocation);
        _combat_attack_this(gCommandTarget);
        combatSetPresetHitLocation(-1);
        if (aimed != previous && gDude != nullptr && !critterIsDead(gDude)) {
            interfaceSetCurrentItemAction(previous);
        }
        break;
    }
    case kOpQuickUse:
        if (gCommandSlot >= 0 && gCommandSlot < kQuickUseSlotCount) {
            useSlot(gCommandSlot);
        }
        break;
    }
}

} // namespace fallout
