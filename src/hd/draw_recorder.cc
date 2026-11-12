#include "hd/draw_recorder.h"

#include <string.h>

#include <vector>

#include "art.h"
#include "geometry.h"
#include "hd/draw_list.h"
#include "hd/json_writer.h"
#include "obj_types.h"
#include "object.h"
#include "proto.h"
#include "proto_types.h"
#include "tile.h"

namespace fallout::hd {

namespace {

    std::vector<DrawRecord> gRecords;
    std::vector<std::vector<unsigned char>> gTextBuffers;
    std::vector<int> gIntensityMapSnapshot;
    bool gMapDirty = true;
    bool gSawFloorPass = false;

    // Every hook starts here. Outside an emission a hook firing means CE is
    // drawing into the map window, so the GPU copy is out of date.
    bool shouldRecord()
    {
        if (!gDrawEmitOnly) {
            gMapDirty = true;
        }
        return gDrawRecorderActive;
    }

    const char* passName(DrawPass pass)
    {
        switch (pass) {
        case DrawPass::Floors:
            return "floors";
        case DrawPass::FlatObjects:
            return "flat_objects";
        case DrawPass::EdgeSquaresUnder:
            return "edge_squares_under";
        case DrawPass::Objects:
            return "objects";
        case DrawPass::EdgeSquaresOver:
            return "edge_squares_over";
        case DrawPass::Roofs:
            return "roofs";
        case DrawPass::Outlines:
            return "outlines";
        case DrawPass::Text:
            return "text";
        case DrawPass::Overlay:
            return "overlay";
        case DrawPass::Stencil:
            return "stencil";
        }
        return "unknown";
    }

    // Mirrors the blit selection at the end of _obj_render_object.
    ObjectBlend objectBlend(int objectType, unsigned int flags)
    {
        if (objectType == OBJ_TYPE_INTERFACE) {
            return ObjectBlend::Interface;
        }

        switch (flags & OBJECT_FLAG_0xFC000) {
        case OBJECT_TRANS_RED:
            return ObjectBlend::TransRed;
        case OBJECT_TRANS_WALL:
            return ObjectBlend::TransWall;
        case OBJECT_TRANS_GLASS:
            return ObjectBlend::TransGlass;
        case OBJECT_TRANS_STEAM:
            return ObjectBlend::TransSteam;
        case OBJECT_TRANS_ENERGY:
            return ObjectBlend::TransEnergy;
        default:
            return ObjectBlend::Normal;
        }
    }

    const char* blendName(ObjectBlend blend)
    {
        switch (blend) {
        case ObjectBlend::Interface:
            return "interface";
        case ObjectBlend::TransRed:
            return "trans_red";
        case ObjectBlend::TransWall:
            return "trans_wall";
        case ObjectBlend::TransGlass:
            return "trans_glass";
        case ObjectBlend::TransSteam:
            return "trans_steam";
        case ObjectBlend::TransEnergy:
            return "trans_energy";
        case ObjectBlend::Normal:
            break;
        }
        return "normal";
    }

    void fillObjectFields(DrawRecord& record, Object* object, int fid, int frameWidth, int frameHeight)
    {
        record.object = object;
        record.fid = fid;
        record.x = object->sx;
        record.y = object->sy;
        record.width = frameWidth;
        record.height = frameHeight;
        record.objectId = object->id;
        record.pid = object->pid;
        record.tile = object->tile;
        record.frame = object->frame;
        record.rotation = object->rotation;
        record.flags = object->flags;
        record.elevation = object->elevation;
        record.offsetX = object->x;
        record.offsetY = object->y;
        record.outline = object->outline;
    }

    void writeRect(JsonWriter& json, const char* name, const Rect& rect)
    {
        json.key(name).rect(rect.left, rect.top, rect.right, rect.bottom);
    }

} // namespace

bool gDrawRecorderActive = false;
bool gDrawEmitOnly = false;

void drawRecorderBegin()
{
    gRecords.clear();
    gTextBuffers.clear();
    gIntensityMapSnapshot.clear();
    gSawFloorPass = false;
    gDrawRecorderActive = true;
}

void drawRecorderEnd()
{
    gDrawRecorderActive = false;
}

bool drawEmitFullFrame()
{
    // A capture records CE's own refresh; never nest an emission inside it.
    if (gDrawRecorderActive) {
        return false;
    }

    drawRecorderBegin();
    gDrawEmitOnly = true;
    tileWindowRefresh();
    gDrawEmitOnly = false;
    drawRecorderEnd();
    return gSawFloorPass;
}

const std::vector<DrawRecord>& drawRecords()
{
    return gRecords;
}

const std::vector<unsigned char>& drawTextPixels(int textIndex)
{
    return gTextBuffers[textIndex];
}

bool drawListComplete()
{
    return gSawFloorPass;
}

bool drawMapDirty()
{
    return gMapDirty;
}

void drawClearMapDirty()
{
    gMapDirty = false;
}

void recordPass(DrawPass pass)
{
    if (!shouldRecord()) {
        return;
    }

    if (pass == DrawPass::Floors) {
        gSawFloorPass = true;
    }

    DrawRecord record;
    record.op = DrawOp::Pass;
    record.pass = static_cast<int>(pass);
    gRecords.push_back(record);
}

void recordFill(const Rect& rect, int color)
{
    if (!shouldRecord()) {
        return;
    }

    DrawRecord record;
    record.op = DrawOp::Fill;
    record.clip = rect;
    record.color = color;
    gRecords.push_back(record);
}

void recordFloor(int fid, int x, int y, const Rect& clip, int tile, const int* vertexIntensities, const int* intensityMap, int intensityMapSize)
{
    if (!shouldRecord()) {
        return;
    }

    if (gIntensityMapSnapshot.empty()) {
        gIntensityMapSnapshot.assign(intensityMap, intensityMap + intensityMapSize);
    }

    DrawRecord record;
    record.op = DrawOp::Floor;
    record.fid = fid;
    record.x = x;
    record.y = y;
    record.clip = clip;
    record.tile = tile;
    memcpy(record.vertexIntensities, vertexIntensities, sizeof(record.vertexIntensities));
    gRecords.push_back(record);
}

void recordRoof(int fid, int x, int y, const Rect& clip, int light, const Rect& eggRect)
{
    if (!shouldRecord()) {
        return;
    }

    DrawRecord record;
    record.op = DrawOp::Roof;
    record.fid = fid;
    record.x = x;
    record.y = y;
    record.clip = clip;
    record.light = light;
    record.egg = true;
    record.eggFid = FrmId(gEgg).fid();
    record.eggRect = eggRect;
    gRecords.push_back(record);
}

void recordObject(Object* object, int fid, int frameWidth, int frameHeight, const Rect& clip, int light)
{
    if (!shouldRecord()) {
        return;
    }

    DrawRecord record;
    record.op = DrawOp::Object;
    fillObjectFields(record, object, fid, frameWidth, frameHeight);
    record.clip = clip;
    record.light = light;
    record.blend = objectBlend(FrmId(fid).objectType(), object->flags);

    // Walls and scenery carry their orientation in the proto (the egg logic
    // reads the same flags); later phases use it for normal-map heuristics.
    ObjectType type = ProtoId(object).objectType();
    if (type == OBJ_TYPE_WALL || type == OBJ_TYPE_SCENERY) {
        Proto* proto;
        if (protoGetProto(ProtoId(object), &proto) != -1) {
            record.extendedFlags = proto->critter.extendedFlags;
        }
    }

    gRecords.push_back(record);
}

void recordObjectEgg(int eggFid, const Rect& eggRect)
{
    if (!gDrawRecorderActive || gRecords.empty() || gRecords.back().op != DrawOp::Object) {
        return;
    }

    gRecords.back().egg = true;
    gRecords.back().eggFid = eggFid;
    gRecords.back().eggRect = eggRect;
}

void recordOutline(Object* object, int fid, int frameWidth, int frameHeight, const Rect& clip)
{
    if (!shouldRecord()) {
        return;
    }

    DrawRecord record;
    record.op = DrawOp::Outline;
    fillObjectFields(record, object, fid, frameWidth, frameHeight);
    record.clip = clip;
    gRecords.push_back(record);
}

void recordText(int x, int y, int width, int height, const unsigned char* data, const Rect& clip)
{
    if (!shouldRecord()) {
        return;
    }

    DrawRecord record;
    record.op = DrawOp::Text;
    record.x = x;
    record.y = y;
    record.width = width;
    record.height = height;
    record.clip = clip;
    record.textIndex = static_cast<int>(gTextBuffers.size());
    gTextBuffers.emplace_back(data, data + width * height);
    gRecords.push_back(record);
}

std::set<int> drawRecorderFids()
{
    std::set<int> fids;
    for (const DrawRecord& record : gRecords) {
        switch (record.op) {
        case DrawOp::Floor:
        case DrawOp::Roof:
        case DrawOp::Object:
        case DrawOp::Outline:
            fids.insert(record.fid);
            if (record.egg) {
                fids.insert(record.eggFid);
            }
            break;
        default:
            break;
        }
    }
    return fids;
}

void drawRecorderWrite(JsonWriter& json)
{
    json.key("floor_intensity_map").beginObject();
    json.member("width", 80);
    json.member("count", static_cast<int>(gIntensityMapSnapshot.size()));
    json.key("int32le").base64(gIntensityMapSnapshot.data(), gIntensityMapSnapshot.size() * sizeof(int));
    json.endObject();

    json.key("draws").beginArray();
    for (const DrawRecord& record : gRecords) {
        json.newline();
        json.beginObject();
        switch (record.op) {
        case DrawOp::Pass:
            json.member("op", "pass");
            json.member("pass", passName(static_cast<DrawPass>(record.pass)));
            break;
        case DrawOp::Fill:
            json.member("op", "fill");
            writeRect(json, "rect", record.clip);
            json.member("color", record.color);
            break;
        case DrawOp::Floor:
            json.member("op", "floor");
            json.member("fid", record.fid);
            json.member("x", record.x);
            json.member("y", record.y);
            writeRect(json, "clip", record.clip);
            json.member("tile", record.tile);
            json.key("vertex_light").beginArray();
            for (int intensity : record.vertexIntensities) {
                json.value(intensity);
            }
            json.endArray();
            break;
        case DrawOp::Roof:
            json.member("op", "roof");
            json.member("fid", record.fid);
            json.member("x", record.x);
            json.member("y", record.y);
            writeRect(json, "clip", record.clip);
            json.member("light", record.light);
            writeRect(json, "egg_rect", record.eggRect);
            break;
        case DrawOp::Object:
        case DrawOp::Outline:
            json.member("op", record.op == DrawOp::Object ? "object" : "outline");
            json.member("fid", record.fid);
            json.member("frame", record.frame);
            json.member("rotation", record.rotation);
            json.member("x", record.x);
            json.member("y", record.y);
            json.member("w", record.width);
            json.member("h", record.height);
            writeRect(json, "clip", record.clip);
            json.member("id", record.objectId);
            json.member("pid", record.pid);
            json.member("type", static_cast<int>(ProtoId(record.pid).objectType()));
            json.member("tile", record.tile);
            json.member("elevation", record.elevation);
            json.member("offset_x", record.offsetX);
            json.member("offset_y", record.offsetY);
            json.member("flags", record.flags);
            if (record.op == DrawOp::Object) {
                json.member("light", record.light);
                json.member("mode", blendName(record.blend));
                json.member("ext_flags", record.extendedFlags);
                if (record.egg) {
                    json.member("egg_fid", record.eggFid);
                    writeRect(json, "egg_rect", record.eggRect);
                }
            } else {
                json.member("outline", record.outline);
            }
            break;
        case DrawOp::Text:
            json.member("op", "text");
            json.member("x", record.x);
            json.member("y", record.y);
            json.member("w", record.width);
            json.member("h", record.height);
            writeRect(json, "clip", record.clip);
            const std::vector<unsigned char>& pixels = gTextBuffers[record.textIndex];
            json.key("pixels").base64(pixels.data(), pixels.size());
            break;
        }
        json.endObject();
    }
    json.newline();
    json.endArray();
}

} // namespace fallout::hd
