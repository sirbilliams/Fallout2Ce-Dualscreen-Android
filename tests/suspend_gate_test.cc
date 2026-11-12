// The automatic suspend save refuses states where the engine holds a map
// without objects (title screen, map load): it wrote such a slot once and
// "Continue" then loaded floor tiles only.
#include "suspend_gate.h"
#include <cstdio>
#include <cstdlib>

#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAIL at %d: %s\n", __LINE__, #condition); std::abort(); } } while (false)

using fallout::SuspendGateState;
using fallout::suspendSaveAllowed;

static SuspendGateState playing()
{
    SuspendGateState s {};
    s.saveSystemReady = s.dudeExists = s.dudeOnMap = s.mapNamed = true;
    s.gameLoopRunning = true;
    s.stateNormal = s.onlyCombatModes = s.interfaceUsable = s.combatSafe = s.dudeIdle = true;
    return s;
}

int main()
{
    CHECK(suspendSaveAllowed(playing()));

    // After gameReset at the title: dude and map name survive, the loop is over.
    SuspendGateState s = playing();
    s.gameLoopRunning = false;
    CHECK(!suspendSaveAllowed(s));

    s = playing(); s.mapLoading = true; CHECK(!suspendSaveAllowed(s));
    s = playing(); s.loadingGame = true; CHECK(!suspendSaveAllowed(s));
    s = playing(); s.dudeOnMap = false; CHECK(!suspendSaveAllowed(s));
    s = playing(); s.mapNamed = false; CHECK(!suspendSaveAllowed(s));
    s = playing(); s.combatSafe = false; CHECK(!suspendSaveAllowed(s));
    s = playing(); s.dudeIdle = false; CHECK(!suspendSaveAllowed(s));
    s = playing(); s.onlyCombatModes = false; CHECK(!suspendSaveAllowed(s));
    return 0;
}
