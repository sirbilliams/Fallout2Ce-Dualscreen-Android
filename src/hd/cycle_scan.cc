#include "hd/cycle_scan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <array>
#include <filesystem>
#include <map>
#include <utility>

#include "art.h"
#include "debug.h"
#include "hd/paths.h"
#include "map.h"
#include "map_edge.h"
#include "object.h"
#include "tile.h"
#include "worldmap.h"

namespace fallout::hd {

namespace {

    // cycle.cc's ranges, in palette indices.
    struct CycleRange {
        int first;
        int last;
        const char* name;
    };

    constexpr CycleRange kRanges[] = {
        { 229, 232, "slime" },
        { 233, 237, "monitors" },
        { 238, 242, "slow_fire" },
        { 243, 247, "fast_fire" },
        { 248, 253, "shoreline" },
        { 254, 254, "alarm" },
    };
    constexpr int kRangeCount = sizeof(kRanges) / sizeof(kRanges[0]);

    // Areas are scored over 3 x 3 cells of this size (1x screen px), about
    // what a 3440x1440 window at scale 3 shows (1146 x 480 minus the bar).
    constexpr int kCellWidth = 384;
    constexpr int kCellHeight = 160;

    using Counts = std::array<int, kRangeCount>;

    std::map<long long, Counts> gArtCounts; // by (fid, frame, rotation)

    Counts artCounts(int fid, int frame, int rotation)
    {
        long long key = (static_cast<long long>(fid) << 16) | (frame << 4) | rotation;
        auto found = gArtCounts.find(key);
        if (found != gArtCounts.end()) {
            return found->second;
        }
        Counts counts = {};
        CacheEntry* handle;
        Art* art = artLock(FrmId(fid), &handle);
        if (art != nullptr) {
            Rotation r = static_cast<Rotation>(rotation);
            int f = frame < artGetFrameCount(art) ? frame : 0;
            int width = artGetWidth(art, f, r);
            int height = artGetHeight(art, f, r);
            unsigned char* pixels = artGetFrameData(art, f, r);
            if (pixels != nullptr) {
                for (int i = 0; i < width * height; i++) {
                    int index = pixels[i];
                    for (int k = 0; k < kRangeCount; k++) {
                        if (index >= kRanges[k].first && index <= kRanges[k].last) {
                            counts[k]++;
                        }
                    }
                }
            }
            artUnlock(handle);
        }
        gArtCounts[key] = counts;
        return counts;
    }

    struct RangeScan {
        int tiles = 0;
        int objects = 0;
        std::map<std::pair<int, int>, int> cells;
        std::map<int, int> art; // pixels per fid
    };

    void add(RangeScan* scans, const Counts& counts, int x, int y, int fid, bool tile)
    {
        std::pair<int, int> cell(x >= 0 ? x / kCellWidth : -1 - (-x - 1) / kCellWidth, y >= 0 ? y / kCellHeight : -1 - (-y - 1) / kCellHeight);
        for (int k = 0; k < kRangeCount; k++) {
            if (counts[k] == 0) {
                continue;
            }
            (tile ? scans[k].tiles : scans[k].objects) += counts[k];
            scans[k].cells[cell] += counts[k];
            scans[k].art[fid] += counts[k];
        }
    }

    // Pixels of each cycle range in the elevation's floor and roof tiles
    // and objects, by 1x screen cell (the current view's coordinates).
    void collect(RangeScan* scans, MapElevation elevation)
    {
        for (int square = 0; square < SQUARE_GRID_SIZE; square++) {
            int tileFid = _square[elevation]->tileFid[square];
            int x;
            int y;
            squareTileToScreenXY(square, &x, &y, elevation);
            TileFrameId ids[2] = { FloorTileFrmId(tileFid).frameId<TileFrameId>(), RoofTileFrmId(tileFid).frameId<TileFrameId>() };
            for (TileFrameId id : ids) {
                if (id == TileFrameId::Invalid || id == TileFrameId::Reserved || id == TileFrameId::Grid || id == TileFrameId::Last) {
                    continue;
                }
                int fid = TileFrmId(id).fid();
                add(scans, artCounts(fid, 0, 0), x, y, fid, true);
            }
        }
        for (Object* object = objectFindFirstAtElevation(elevation); object != nullptr; object = objectFindNextAtElevation()) {
            if (object->tile == -1 || (object->flags & OBJECT_HIDDEN) != OBJECT_NONE) {
                continue;
            }
            int x;
            int y;
            tileToScreenXY(object->tile, &x, &y);
            add(scans, artCounts(object->fid, object->frame, object->rotation), x, y, object->fid, false);
        }
    }

    // The view (3 x 3 cells) with the most of the range's pixels, among
    // views the camera can be centered on: a forced center is clamped to
    // the map's edge zone (tileSetCenter, map_edge.h), so each candidate
    // is scored where the clamp would actually put the view (P6 picked
    // den_fire and sf_docks views next to an edge, and the camera showed
    // something else). Returns the (clamped) tile, -1 if there are none.
    int bestTile(const RangeScan& scan, MapElevation elevation, int* bestPixels)
    {
        *bestPixels = -1;
        int best = -1;
        for (const auto& [cell, pixels] : scan.cells) {
            int tile = tileFromScreenXY(cell.first * kCellWidth + kCellWidth / 2, cell.second * kCellHeight + kCellHeight / 2, true);
            if (tile < 0) {
                continue;
            }
            if (mapEdgeIsEnabled()) {
                tile = mapEdgeSelectZoneAndClamp(tile, elevation);
                if (!tileIsValid(tile)) {
                    continue;
                }
            }
            int x;
            int y;
            tileToScreenXY(tile, &x, &y);
            int sum = 0;
            for (const auto& [other, otherPixels] : scan.cells) {
                int cx = other.first * kCellWidth + kCellWidth / 2;
                int cy = other.second * kCellHeight + kCellHeight / 2;
                if (abs(cx - x) <= kCellWidth * 3 / 2 && abs(cy - y) <= kCellHeight * 3 / 2) {
                    sum += otherPixels;
                }
            }
            if (sum > *bestPixels) {
                *bestPixels = sum;
                best = tile;
            }
        }
        return best;
    }

    void scanElevation(FILE* out, const char* mapName, MapElevation elevation)
    {
        RangeScan scans[kRangeCount];
        collect(scans, elevation);
        for (int k = 0; k < kRangeCount; k++) {
            const RangeScan& scan = scans[k];
            if (scan.tiles + scan.objects == 0) {
                continue;
            }
            int bestPixels;
            int best = bestTile(scan, elevation, &bestPixels);
            int topFid = -1;
            int topPixels = 0;
            for (const auto& [fid, pixels] : scan.art) {
                if (pixels > topPixels) {
                    topPixels = pixels;
                    topFid = fid;
                }
            }
            char topName[64] = "?";
            if (topFid != -1) {
                artCopyFileName(FrmId(topFid), topName);
            }
            fprintf(out, "%s\t%d\t%s\t%d\t%d\t%d\t%d\t%d\t%s\t%d\n", mapName, static_cast<int>(elevation), kRanges[k].name,
                scan.tiles + scan.objects, scan.tiles, scan.objects, best, bestPixels, topName, topPixels);
        }
    }

} // namespace

int cycleScanBestTile(MapElevation elevation, const char* range, int* pixels)
{
    *pixels = 0;
    for (int k = 0; k < kRangeCount; k++) {
        if (strcmp(kRanges[k].name, range) != 0) {
            continue;
        }
        RangeScan scans[kRangeCount];
        collect(scans, elevation);
        return scans[k].tiles + scans[k].objects > 0 ? bestTile(scans[k], elevation, pixels) : -1;
    }
    return -1;
}

int runCycleScan(int (*loadNewMap)(char* mapFileName), const std::string& firstMap, const std::string& outDir)
{
    std::error_code error;
    std::filesystem::create_directories(nativePath(outDir), error);
    if (error) {
        debugPrint("HD: cycle scan cannot create %s: %s\n", outDir.c_str(), error.message().c_str());
        return 1;
    }
    std::string path = (std::filesystem::path(nativePath(outDir)) / "cycle_scan.tsv").string();
    FILE* out = openFile(path, "w");
    if (out == nullptr) {
        debugPrint("HD: cycle scan can't write %s\n", path.c_str());
        return 1;
    }
    fprintf(out, "map\televation\trange\tpixels\ttile_pixels\tobject_pixels\tbest_tile\tbest_pixels\ttop_art\ttop_art_pixels\n");

    char name[COMPAT_MAX_PATH];
    snprintf(name, sizeof(name), "%s", firstMap.c_str());
    if (loadNewMap(name) != 0) {
        debugPrint("HD: cycle scan can't start with %s\n", name);
        fclose(out);
        return 1;
    }

    int maps = 0;
    int count = wmMapMaxCount();
    for (int index = 0; index < count; index++) {
        if (wmMapIdxToName(static_cast<Map>(index), name, sizeof(name)) != 0 || name[0] == '\0') {
            continue;
        }
        if (mapLoadByName(name) != 0) {
            debugPrint("HD: cycle scan can't load %s\n", name);
            continue;
        }
        maps++;
        for (int e = 0; e < ELEVATION_COUNT; e++) {
            MapElevation elevation = static_cast<MapElevation>(e);
            // A set flag means the map file has no such elevation.
            if ((static_cast<int>(gMapHeader.flags) & (MAP_HEADER_ELEVATION_0 << e)) != 0 || mapSetElevation(elevation) != 0) {
                continue;
            }
            scanElevation(out, name, elevation);
        }
        fflush(out);
        debugPrint("HD: cycle scan %d/%d %s\n", index + 1, count, name);
    }
    fclose(out);
    debugPrint("HD: cycle scan of %d maps written to %s\n", maps, path.c_str());
    return 0;
}

} // namespace fallout::hd
