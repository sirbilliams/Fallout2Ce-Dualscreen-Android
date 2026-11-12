#ifndef FALLOUT_HD_HD_ART_H_
#define FALLOUT_HD_HD_ART_H_

#ifdef FALLOUT_HD

#include "hd/gl.h"

#include <vector>

namespace fallout::hd {

// HD art store (PLAN.md P4.3): the runtime packs written by `fo2hd pack`.
//
//   [hd] hd_art=1             draw HD art where a pack exists (default 1)
//   [hd] hd_art_dir=<data_root>\hdpack   pack root (default "hd" in the game folder)
//   [hd] vram_budget_mb=1024  size of the HD page pool on the GPU (part of
//                             the 8 GB VRAM budget, PLAN.md P4.5)
//   [hd_debug] hd_force_original=1   every frame uses the original art (the
//                             "original-equivalent" test of the HD color pass)
//   [hd_debug] hd_tile_span=64   frames over 64 px (1x) go into the pool in
//                             tiles, as frames larger than a page do (tests
//                             the tiled paths of the map and the UI)
//   [hd_debug] hd_fallback_percent=50   this share of art files (by path
//                             hash) is treated as missing: the mixing test
//
// One pack per FRM: <dir>\art\<type>\<name>\pack.json + albedo_<tier>x.dds
// (BC7, one atlas per tier). A loader thread reads the files of the tier in
// use; frames go into a pool of 2048x2048 BC7 pages (a Texture2DArray whose
// size is the VRAM budget) on first use, shelf-packed, and pages are evicted
// least recently used, never one the frame being built uses. Until a pack is
// loaded, if it doesn't exist, if a frame's size or pixel hash doesn't match
// the art CE draws, or if the pool is full, the frame is drawn from the
// original art.

struct HdFrame {
    int x; // pool texels of the frame's (0, 0)
    int y;
    int slice; // pool page
    bool hardAlpha; // floors, roofs, walls: coverage from the 1x mask
};

bool hdArtInit();
void hdArtShutdown();

// HD art configured and the pool usable.
bool hdArtActive();

// Ctrl+F8: HD art on/off at runtime (off = the exact palette renderer).
bool hdArtToggle();

// Who builds: the GPU map and the HD UI layer (P7b.2, hd/ui_layer.h) share
// the pool. The map's sprites keep pointing into the pool between its
// builds, so no build evicts a page the map's last build uses; the UI layer
// draws into its own targets during its build and needs nothing kept.
enum class HdArtClient {
    Map,
    Ui,
};

// A build at `tier` (2..4) starts / ends. A tier change empties the pool.
void hdArtBeginBuild(int tier, HdArtClient client = HdArtClient::Map);
void hdArtEndBuild();

// The frame's place in the pool, uploading it if its pack is loaded.
// `hash` is the FNV-1a of the frame's palette indices as CE draws them.
bool hdArtFrame(int fid, int rotation, int frame, unsigned int hash, int width, int height, HdFrame* out);

// The same for named art (FRMs CE loads by path: fid -1).
bool hdArtFrameNamed(const char* path, int rotation, int frame, unsigned int hash, int width, int height, HdFrame* out);
// A part of a frame as tiles that each fit a pool page: UI backgrounds are
// wider than a page at 3x and 4x (the 800-wide interface bar is 2400
// texels at 3x), which hdArtFrame can't place. The tiles cover the frame's
// part (x, y, w, h), in 1x pixels; a frame that fits a page is one tile.
// fid -1: named art by `path`.
struct HdFrameTile {
    int x; // the tile's part of the frame, 1x pixels
    int y;
    int width;
    int height;
    HdFrame frame; // pool texels of the tile's (0, 0)
};
bool hdArtFrameTiles(int fid, const char* path, int rotation, int frame, unsigned int hash, int width, int height, int x, int y, int w, int h, std::vector<HdFrameTile>* out);

// True if a frame of this size (1x pixels) fits one pool page at the tier
// being built, so hdArtFrame can place it; a larger one needs
// hdArtFrameTiles (the map: one critter frame, 568 px wide, at 4x).
bool hdArtFitsPage(int width, int height);

// Takes finished loads from the loader thread. True if the client should
// draw again: for the map, a pack its last build missed is now loaded or
// the build hit the upload limit; for the UI, any pack finished loading
// since its last poll (it keeps track of what it still waits for) or its
// last build hit the upload limit.
bool hdArtPoll(HdArtClient client = HdArtClient::Map);

// True while packs the last build asked for are still loading.
bool hdArtLoading();

GLuint hdArtPoolView();

struct HdArtStats {
    int packsKnown = 0;
    int packsLoaded = 0;
    int packsMissing = 0;
    int packsFailed = 0;
    int packsLoading = 0;
    int mismatches = 0; // frames whose size or hash didn't match the game's art
    int pages = 0;
    int pagesUsed = 0;
    long long poolBytes = 0;
    long long ramBytes = 0; // pack data held in memory
    int buildFrames = 0; // frames asked for by the last build
    int buildHd = 0; // ... that got HD art
    int buildUploads = 0;
    long long buildUploadBytes = 0;
    int buildDeferred = 0; // upload limit reached: next build
    int buildOverBudget = 0; // no evictable page
    // The same for the UI layer's last build (the build* fields above are
    // always the map's).
    int uiFrames = 0;
    int uiHd = 0;
    int uiUploads = 0;
    long long uiUploadBytes = 0;
    int uiDeferred = 0;
    int uiOverBudget = 0;
    long long uploadsTotal = 0;
    long long evictionsTotal = 0;
    long long overBudgetTotal = 0;
    int tier = 0;
    bool forceOriginal = false; // [hd_debug] hd_force_original
    // The whole process's dedicated video memory (DXGI), sampled per build.
    unsigned long long vramUsage = 0;
    unsigned long long vramPeak = 0;
    unsigned long long vramOsBudget = 0;
};

const HdArtStats& hdArtStats();

// Lines for the developer overlay.
void hdArtDrawOverlay();

} // namespace fallout::hd

#endif // FALLOUT_HD

#endif /* FALLOUT_HD_HD_ART_H_ */
