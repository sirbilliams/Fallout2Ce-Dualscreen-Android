    // Feature bits, shared with the shaders.
    enum : unsigned int {
        kNormals = 1,
        kColors = 2,
        kShadows = 4,
        kPool = 8,
        kTonemap = 16,
        kSun = 32,
        kAo = 64,
        kNight = 128,
        kGi = 256,
    };

    struct LightingConstants {
        int targetSize[2];
        int scale;
        int lightCount;
        int groundOrigin[2];
        int groundSize[2];
        float ambient;
        float ambientColor[3];
        float poolGain;
        float shadowStrength;
        float sunStrength;
        float aoStrength;
        float sun[4];
        float sunStep[2];
        float sunRise;
        unsigned int features;
        float mix;
        float fieldSigma;
        float shadowRange;
        float fieldMax;
        int fieldSize[2];
        int shadowSize[2];
        float knee;
        float lightScale;
        float sunGain;
        float tonemapWhite;
        float giStrength;
        float giGround;
        float giWall;
        int cascade;
        int cascadeSize[2];
        int tonemapCurve;
        float agxExposure;
        float bloomGlow;
        float bloomHot;
        int sunSteps;
        float pad4;
    };
    static_assert(sizeof(LightingConstants) == 192, "LightingConstants must match the HLSL cbuffer");

    constexpr int kCascades = 4; // must match the shader

    struct GpuHex {
        float pos[2];
        float intensity;
        float pad;
    };

    struct GpuLight {
        float pos[2];
        float z;
        float radius;
        float intensity;
        float step;
        int slice;
        float pool;
        float color[3];
        float pad2;
    };
    static_assert(sizeof(GpuLight) == 48, "GpuLight must match the HLSL struct");

    // Effects in Ctrl+F6 order, with their [hd_light] switch names.
    struct Effect {
        unsigned int bit;
        const char* name;
    };
    constexpr Effect kEffects[] = {
        { kNormals, "normals" },
        { kColors, "colors" },
        { kShadows, "shadows" },
        { kPool, "pool" },
        { kTonemap, "tonemap" },
        { kSun, "sun" },
        { kAo, "ao" },
        { kNight, "night" },
        { kGi, "gi" },
    };
    constexpr int kEffectCount = sizeof(kEffects) / sizeof(kEffects[0]);

    struct Settings {
        bool newLight = true;
        int quality = 2; // 0 low, 1 medium, 2 high
        bool timing = false; // timing=1: per-stage timing log
        unsigned int switches = kNormals | kColors | kShadows | kPool | kTonemap | kSun | kAo | kNight | kGi;
        int effectOff = -1; // Ctrl+F6: index into kEffects, -1 = none
        float poolGain = 0.6f;
        float shadowStrength = 0.5f;
        float shadowRange = 64.0f;
        float sunStrength = 0.3f;
        float sunGain = 1.3f;
        float dayLights = 0.15f;
        float sunDir[3] = { -1.0f, -0.6f, 1.5f };
        // The sun's path through the day (the author, 2026-11-09): sunDir is noon;
        // from 5:00 to 19:00 the sun turns sunSwing degrees around (morning
        // counterclockwise of noon, evening clockwise: east, south, west as
        // the maps' north is up-right) and sinks to sunLow degrees above the
        // horizon, so shadows turn and grow longer. Moderate by default: the
        // art's own painted shadows fall to the lower right whatever the hour.
        // sun_path=0: the fixed sun of 0.9.
        int sunPath = 1;
        float sunSwing = 40.0f;
        float sunLow = 22.0f;
        float aoStrength = 0.4f;
        float nightAmbient = 0.92f;
        float nightTint[3] = { 0.82f, 0.88f, 1.0f };
        float fieldSigma = 14.0f;
        float fieldMax = 1.5f;
        float originalMix = 0.0f;
        float knee = 0.8f;
        float tonemapWhite = 0.3f;
        int tonemapCurve = 0; // 0 = shoulder (P5), 1 = AgX
        float agxExposure = 1.0f;
        float bloomGlow = 0.8f;
        float bloomHot = 0.0f;
        float giStrength = 1.0f;
        float giGround = 0.25f;
        float giWall = 0.5f;
        // Per category: detail strength (fo2hd.hdassets DETAIL_STRENGTH),
        // relief weight and shape weight (fo2hd.compose NORMAL_DETAIL,
        // NORMAL_SHAPE), and how much of the sun's shadow the art ignores.
        // Walls and scenery ignore all of it: the shadow map holds one height
        // per ground point, so a face reads its own wall and its neighbors
        // there, column by column, which drew stripes on walls and bright
        // bands between cliff pieces. They still cast it on what lies around.
        float category[kLightCategoryCount][4] = {
            { 0.0f, 0.0f, 0.0f, 0.0f }, // none
            { 1.2f, 0.8f, 0.3f, 0.0f }, // floor
            { 1.0f, 0.0f, 0.0f, 0.0f }, // roof
            { 0.8f, 0.6f, 0.45f, 1.0f }, // wall
            { 1.0f, 0.6f, 0.3f, 1.0f }, // scenery
            { 0.6f, 0.5f, 0.2f, 0.0f }, // critter
            { 0.8f, 0.5f, 0.2f, 0.0f }, // item
            { 0.8f, 0.5f, 0.2f, 0.0f }, // misc
            // Round scenery (map_renderer.cc roundScenery): its rounded body
            // takes the sun's shape fully; like other scenery it ignores the
            // sun shadow map and still casts into it.
            { 1.0f, 0.6f, 1.0f, 1.0f }, // round
        };
    };

    std::string configString(const char* key, const char* defaultValue)
    {
        char* value = nullptr;
        configGetString(&gGameConfig, "hd_light", key, &value, defaultValue);
        return value != nullptr ? value : defaultValue;
    }

    void configFloat(const char* key, float* value)
    {
        std::string text = configString(key, "");
        if (!text.empty()) {
            *value = static_cast<float>(atof(text.c_str()));
        }
    }

    // "a,b,c" into up to `count` floats; missing ones keep their value.
    void configFloats(const char* key, float* values, int count)
    {
        std::string text = configString(key, "");
        size_t pos = 0;
        for (int i = 0; i < count && pos < text.size(); i++) {
            size_t comma = text.find(',', pos);
            std::string item = text.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
            if (!item.empty()) {
                values[i] = static_cast<float>(atof(item.c_str()));
            }
            if (comma == std::string::npos) {
                break;
            }
            pos = comma + 1;
        }
    }

    void configSwitch(const char* key, unsigned int bit, unsigned int* switches)
    {
        int value = (*switches & bit) != 0 ? 1 : 0;
        configGetInt(&gGameConfig, "hd_light", key, &value, value);
        *switches = value != 0 ? (*switches | bit) : (*switches & ~bit);
    }

    void loadSettings(Settings& s)
    {
        s.newLight = configString("mode", "new") != "original";
#ifdef __ANDROID__
        // The new light is too slow on phone GPUs at 2x (Thor, 2026-11-12:
        // low still lags), so Android starts with the original light.
        std::string quality = configString("quality", "off");
#else
        std::string quality = configString("quality", "high");
#endif
        s.quality = quality == "off" ? -1 : (quality == "low" ? 0 : (quality == "medium" ? 1 : 2));
        int timing = 0;
        configGetInt(&gGameConfig, "hd_light", "timing", &timing, 0);
        s.timing = timing != 0;
        for (const Effect& effect : kEffects) {
            configSwitch(effect.name, effect.bit, &s.switches);
        }
        configFloat("pool_gain", &s.poolGain);
        configFloat("shadow_strength", &s.shadowStrength);
        configFloat("shadow_range", &s.shadowRange);
        configFloat("sun_strength", &s.sunStrength);
        configFloats("sun_dir", s.sunDir, 3);
        configGetInt(&gGameConfig, "hd_light", "sun_path", &s.sunPath, s.sunPath);
        configFloat("sun_swing", &s.sunSwing);
        configFloat("sun_low", &s.sunLow);
        configFloat("sun_gain", &s.sunGain);
        configFloat("day_lights", &s.dayLights);
        configFloat("ao_strength", &s.aoStrength);
        configFloat("night_ambient", &s.nightAmbient);
        configFloats("night_tint", s.nightTint, 3);
        configFloat("field_sigma", &s.fieldSigma);
        configFloat("field_max", &s.fieldMax);
        configFloat("original_mix", &s.originalMix);
        configFloat("knee", &s.knee);
        configFloat("tonemap_white", &s.tonemapWhite);
        s.tonemapCurve = configString("tonemap_curve", "shoulder") == "agx" ? 1 : 0;
        configFloat("agx_exposure", &s.agxExposure);
        configFloat("bloom_glow", &s.bloomGlow);
        configFloat("bloom_hot", &s.bloomHot);
        configFloat("gi_strength", &s.giStrength);
        configFloat("gi_ground", &s.giGround);
        configFloat("gi_wall", &s.giWall);
        const char* names[kLightCategoryCount] = { "none", "floor", "roof", "wall", "scenery", "critter", "item", "misc", "round" };
        for (int i = 1; i < kLightCategoryCount; i++) {
            configFloats(names[i], s.category[i], 4);
        }
    }

    // Night: 1 from 20:00 to 05:00, ramps over the two hours either side;
    // only where the ambient is actually dimmed (caves stay as they are).
    float nightAmount(int timeOfDay, float ambient)
    {
        float hour = (timeOfDay / 100) + (timeOfDay % 100) / 60.0f;
        float night;
        if (hour >= 20.0f || hour < 5.0f) {
            night = 1.0f;
        } else if (hour >= 18.0f) {
            night = (hour - 18.0f) / 2.0f;
        } else if (hour < 7.0f) {
            night = (7.0f - hour) / 2.0f;
        } else {
            night = 0.0f;
        }
        return night * std::min(std::max((0.95f - ambient) / 0.15f, 0.0f), 1.0f);
    }

    // Daylight for the sun: bright ambient outside the night hours.
    float dayAmount(int timeOfDay, float ambient)
    {
        float hour = (timeOfDay / 100) + (timeOfDay % 100) / 60.0f;
        float day = hour >= 7.0f && hour < 18.0f ? 1.0f : 0.0f;
        if (hour >= 5.0f && hour < 7.0f) {
            day = (hour - 5.0f) / 2.0f;
        } else if (hour >= 18.0f && hour < 20.0f) {
            day = (20.0f - hour) / 2.0f;
        }
        return day * std::min(std::max((ambient - 0.75f) / 0.2f, 0.0f), 1.0f);
    }

    // Toward the sun (unit, world u v z) at this time of day: Settings::sunDir
    // at noon, turned and lowered toward 5:00 and 19:00 when sunPath is on.
    void sunDirection(const Settings& s, int timeOfDay, float out[3])
    {
        float length = sqrtf(s.sunDir[0] * s.sunDir[0] + s.sunDir[1] * s.sunDir[1] + s.sunDir[2] * s.sunDir[2]);
        for (int k = 0; k < 3; k++) {
            out[k] = length > 0.0f ? s.sunDir[k] / length : (k == 2 ? 1.0f : 0.0f);
        }
        if (s.sunPath == 0) {
            return;
        }
        const float degree = 0.017453293f;
        float hour = (timeOfDay / 100) + (timeOfDay % 100) / 60.0f;
        float t = std::min(std::max((hour - 12.0f) / 7.0f, -1.0f), 1.0f);
        float ground = sqrtf(out[0] * out[0] + out[1] * out[1]);
        float noonHeight = atan2f(out[2], ground);
        float low = std::min(s.sunLow * degree, noonHeight);
        float height = low + (noonHeight - low) * cosf(t * 1.5707963f);
        // u right, v down the screen: a growing angle turns clockwise on screen.
        float turn = (ground > 1e-4f ? atan2f(out[1], out[0]) : 0.0f) + s.sunSwing * degree * t;
        out[0] = cosf(height) * cosf(turn);
        out[1] = cosf(height) * sinf(turn);
        out[2] = sinf(height);
    }
