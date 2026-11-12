    struct PostConstants {
        int size[2];
        int levelSize[2];
        float sourceTexel[2];
        float spread;
        float bloomStrength;
        float whiteBalance[3];
        float saturation;
        float lift[3];
        float vignette;
        float gamma[3];
        float lutSize;
        float gain[3];
        int grade;
    };
    static_assert(sizeof(PostConstants) == 96, "PostConstants must match the HLSL cbuffer");

    constexpr int kMaxLevels = 6;

    enum Effect : unsigned int {
        kBloom = 1,
        kGrade = 2,
        kVignette = 4,
        kGrain = 8,
    };

    struct EffectName {
        unsigned int bit;
        const char* name;
    };
    constexpr EffectName kEffects[] = {
        { kBloom, "bloom" },
        { kGrade, "grade" },
        { kVignette, "vignette" },
        { kGrain, "grain" },
    };
    constexpr int kEffectCount = sizeof(kEffects) / sizeof(kEffects[0]);

    struct Settings {
        bool enabled = true;
        unsigned int switches = kBloom | kGrade | kVignette | kGrain;
        int cycle = 0; // Ctrl+F5: 0 all on, 1..kEffectCount one off, kEffectCount + 1 all off
        float bloomStrength = 1.5f;
        float bloomSpread = 0.8f;
        float temperature = 0.0f;
        float lift[3] = { 0.0f, 0.0f, 0.0f };
        float gamma[3] = { 1.0f, 1.0f, 1.0f };
        float gain[3] = { 1.0f, 1.0f, 1.0f };
        float saturation = 1.0f;
        std::string lutPath;
        float vignette = 0.2f;
        float grain = 0.025f;
    };

    std::string configString(const char* key, const char* defaultValue)
    {
        char* value = nullptr;
        configGetString(&gGameConfig, "hd_post", key, &value, defaultValue);
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

    void loadSettings(Settings& s)
    {
        int enabled = 1;
        configGetInt(&gGameConfig, "hd_post", "enabled", &enabled, 1);
        s.enabled = enabled != 0;
        for (const EffectName& effect : kEffects) {
            int value = 1;
            configGetInt(&gGameConfig, "hd_post", effect.name, &value, 1);
            s.switches = value != 0 ? (s.switches | effect.bit) : (s.switches & ~effect.bit);
        }
        configFloat("bloom_strength", &s.bloomStrength);
        configFloat("bloom_spread", &s.bloomSpread);
        configFloat("grade_temperature", &s.temperature);
        configFloats("grade_lift", s.lift, 3);
        configFloats("grade_gamma", s.gamma, 3);
        configFloats("grade_gain", s.gain, 3);
        configFloat("grade_saturation", &s.saturation);
        s.lutPath = configString("grade_lut", "");
        configFloat("vignette_strength", &s.vignette);
        configFloat("grain_strength", &s.grain);
    }
