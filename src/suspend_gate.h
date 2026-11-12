#ifndef FALLOUT_SUSPEND_GATE_H_
#define FALLOUT_SUSPEND_GATE_H_

namespace fallout {

// CE: What the automatic suspend save needs to be true to write a slot. The
// save writes whatever the engine holds, so out of the game loop (the main
// menu, the title's Continue/Load, a game being torn down: `gameReset` removes
// the objects, scripts and map variables but keeps the dude and the map's
// name) it writes a map with floor tiles only and a dude without a name.
struct SuspendGateState {
    bool saveSystemReady;
    bool dudeExists;
    bool dudeOnMap; // tile != -1
    bool mapNamed;
    bool gameLoopRunning; // `mainIsPlaying`
    bool mapLoading; // `mapIsLoading`
    bool loadingGame; // `_isLoadingGame`
    bool stateNormal;
    bool onlyCombatModes;
    bool interfaceUsable;
    bool combatSafe;
    bool dudeIdle;
};

inline bool suspendSaveAllowed(const SuspendGateState& s)
{
    return s.saveSystemReady && s.dudeExists && s.dudeOnMap && s.mapNamed
        && s.gameLoopRunning && !s.mapLoading && !s.loadingGame
        && s.stateNormal && s.onlyCombatModes && s.interfaceUsable
        && s.combatSafe && s.dudeIdle;
}

} // namespace fallout

#endif
