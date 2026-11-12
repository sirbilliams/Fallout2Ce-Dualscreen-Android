#include "hd/map_post.h"
#ifdef FALLOUT_HD
#include "hd/compute.h"
#include "config.h"
#include "game_config.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
namespace fallout::hd {
    namespace {
#include "map_post_shader.h"
#include "map_post_settings.inc"
#include "map_post_uniforms.inc"
        struct State {
            Settings settings;
            GLuint programs[3]= {
            },lut=0;
            int lutSize=0;
            compute::Texture down[6],up[6],output;
            bool outputValid=false;
            ~State() {
                for(auto p:programs)glDeleteProgram(p);
                glDeleteTextures(1,&lut);
            }
        };
        State* gState=nullptr;
        unsigned effects() {
            if(!gState||!gState->settings.enabled||gState->settings.cycle==5)return 0;
            auto& s=gState->settings;
            return s.switches&~(s.cycle>0?kEffects[s.cycle-1].bit:0);
        }
        void pass(int i,const PostConstants& c,compute::Texture& out) {
            GLuint p=gState->programs[i];
            glUseProgram(p);
            uniforms(p,c);
            compute::image(0,out);
            compute::dispatch(out.width,out.height);
        }
        // A .cube file (LUT_3D_SIZE N, then N^3 lines "r g b", red fastest).
        bool loadLut(const std::string& path) {
            FILE* stream = fopen(path.c_str(), "r");
            if (stream == nullptr) {
                std::fprintf(stderr, "HD: post: can't open LUT %s\n", path.c_str());
                return false;
            }
            int size = 0;
            std::vector<float> data;
            char line[256];
            while (fgets(line, sizeof(line), stream) != nullptr) {
                float r;
                float g;
                float b;
                if (strncmp(line, "LUT_3D_SIZE", 11) == 0) {
                    size = atoi(line + 11);
                    data.reserve(static_cast<size_t>(size) * size * size * 4);
                } else if (size > 0 && sscanf(line, "%f %f %f", &r, &g, &b) == 3) {
                    data.insert(data.end(), { r, g, b, 1.0f });
                }
            }
            fclose(stream);
            if (size < 2 || size > 128 || data.size() != static_cast<size_t>(size) * size * size * 4) {
                std::fprintf(stderr, "HD: post: LUT %s has %zu entries for size %d\n", path.c_str(), data.size() / 4, size);
                return false;
            }
            glGenTextures(1,&gState->lut);
            glBindTexture(GL_TEXTURE_3D,gState->lut);
            glTexStorage3D(GL_TEXTURE_3D,1,GL_RGBA16F,size,size,size);
            glTexSubImage3D(GL_TEXTURE_3D,0,0,0,0,size,size,size,GL_RGBA,GL_FLOAT,data.data());
            glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            for(GLenum axis:{GL_TEXTURE_WRAP_S,GL_TEXTURE_WRAP_T,GL_TEXTURE_WRAP_R})glTexParameteri(GL_TEXTURE_3D,axis,GL_CLAMP_TO_EDGE);
            gState->lutSize=size;
            return glGetError()==GL_NO_ERROR;
        }
    }
    // namespace
    bool mapPostInit() {
        mapPostShutdown();
        gState=new State;
        loadSettings(gState->settings);
        const char* names[]= {
            "csDown","csUp","csPost"
        };
        for(int i=0;i<3;i++)if(!(gState->programs[i]=compute::program(kShaderSource,names[i]))) {
            mapPostShutdown();
            return false;
        }
        if(!gState->settings.lutPath.empty())loadLut(gState->settings.lutPath);
        return true;
    }
    void mapPostShutdown() {
        delete gState;
        gState=nullptr;
    }
    bool mapPostActive() {
        return effects()!=0;
    }
    GLuint mapPostDraw(GLuint hd,GLuint bloom,int width,int height) {
        if(gState)gState->outputValid=false;
        if(!hd||width<=0||height<=0||!mapPostActive())return hd;
        auto& t=*gState;
        const auto& s=t.settings;
        unsigned on=effects();
        if(!t.output.ensure(width,height))return hd;
        PostConstants c= {
        };
        c.size[0]=width;
        c.size[1]=height;
        int levels=0,w=width,h=height;
        GLuint source=bloom;
        if((on&kBloom)&&bloom) {
            while(levels<6) {
                w=(w+1)/2;
                h=(h+1)/2;
                if(w<8||h<8)break;
                if(!t.down[levels].ensure(w,h)||!t.up[levels].ensure(w,h))return hd;
                auto& level=t.down[levels];
                c.levelSize[0]=w;
                c.levelSize[1]=h;
                c.sourceTexel[0]=1.0f/(levels?t.down[levels-1].width:width);
                c.sourceTexel[1]=1.0f/(levels?t.down[levels-1].height:height);
                compute::sample(0,source);
                pass(0,c,level);
                source=level.id;
                levels++;
            }
            c.spread=s.bloomSpread;
            for(int i=levels-2;i>=0;i--) {
                c.levelSize[0]=t.up[i].width;
                c.levelSize[1]=t.up[i].height;
                c.sourceTexel[0]=1.0f/t.down[i+1].width;
                c.sourceTexel[1]=1.0f/t.down[i+1].height;
                compute::sample(0,t.down[i].id);
                compute::sample(1,source);
                pass(1,c,t.up[i]);
                source=t.up[i].id;
            }
            float norm=0;
            for(int i=0;i<std::max(levels,1);i++)norm+=powf(s.bloomSpread,float(i));
            c.bloomStrength=s.bloomStrength/std::max(norm,1e-3f);
        }
        float temperature=std::clamp(s.temperature,-1.0f,1.0f)*0.15f;
        float wb[3]= {
            1+temperature,1,1-temperature
        };
        float luma=wb[0]*.299f+wb[1]*.587f+wb[2]*.114f;
        for(int i=0;i<3;i++) {
            c.whiteBalance[i]=wb[i]/luma;
            c.lift[i]=s.lift[i];
            c.gamma[i]=std::max(s.gamma[i],.05f);
            c.gain[i]=s.gain[i];
        }
        c.saturation=s.saturation;
        c.lutSize=t.lutSize;
        c.grade=(on&kGrade)!=0;
        c.vignette=(on&kVignette)?s.vignette:0;
        compute::sample(2,hd);
        compute::sample(3,source);
        compute::sample(4,t.lut,GL_TEXTURE_3D);
        pass(2,c,t.output);
        glBindImageTexture(0,0,0,GL_FALSE,0,GL_READ_ONLY,GL_RGBA16F);
        t.outputValid=glGetError()==GL_NO_ERROR;
        return t.outputValid?t.output.id:hd;
    }
    bool mapPostReadback(std::vector<float>& rgba,int* width,int* height) {
        if(!gState||!gState->outputValid)return false;
        auto& t=gState->output;
        if(!compute::read(t.id,t.width,t.height,rgba))return false;
        if(width)*width=t.width;
        if(height)*height=t.height;
        return true;
    }
    float mapPostGrain() {
        return (effects()&kGrain)?gState->settings.grain:0;
    }
    const char* mapPostCycle() {
        if(!gState)return "post unavailable";
        gState->settings.cycle=(gState->settings.cycle+1)%6;
        static const char* names[]= {
            "all post effects","no bloom","no grade","no vignette","no grain","all post off"
        };
        return names[gState->settings.cycle];
    }
    void mapPostDrawOverlay() {
    }
}
// namespace fallout::hd
#endif
