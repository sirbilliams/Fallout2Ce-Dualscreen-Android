// Temporary L2 link scaffold. L1 replaces this with the Dustlight core.
// Neutral even with [hd] enabled=1: no recording, hotkeys or screen changes.
#ifdef FALLOUT_HD

#include "hd/draw_recorder.h"
#include "hd/hd.h"

namespace fallout::hd {

bool gDrawRecorderActive = false;
bool gDrawEmitOnly = false;

void init(bool) { }
void exit() { }
bool enabled() { return false; }
bool handleKey(int) { return false; }
bool uiWalk() { return false; }
bool firstRunScreenSetup(Config*) { return false; }
bool scriptedCaptureRequested() { return false; }
bool scriptedPlayRequested() { return false; }
int prepareScriptedPlay(int (*)(char*)) { return 1; }
int runScriptedCapture(int (*)(char*)) { return 1; }

void recordPass(DrawPass) { }
void recordFill(const Rect&, int) { }
void recordFloor(int, int, int, const Rect&, int, const int*, const int*, int) { }
void recordRoof(int, int, int, const Rect&, int, const Rect&) { }
void recordObject(Object*, int, int, int, const Rect&, int) { }
void recordObjectEgg(int, const Rect&) { }
void recordOutline(Object*, int, int, int, const Rect&) { }
void recordText(int, int, int, int, const unsigned char*, const Rect&) { }

} // namespace fallout::hd

#endif // FALLOUT_HD
