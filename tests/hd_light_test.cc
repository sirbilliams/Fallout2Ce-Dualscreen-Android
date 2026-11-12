// Synthetic L3 boundary, deliberately independent of map_renderer.cc.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "hd/compute.h"
#include <GLES2/gl2ext.h>
#include "hd/map_lighting.h"
#include "hd/map_post.h"
#include "hd/map_renderer.h"
#include "hd/light_scene.h"
#include "game_config.h"
#include <cmath>
#include <map>
#include <string>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <chrono>
using namespace fallout;
using namespace fallout::hd;
namespace {
    std::map<std::string,std::string> settings;
    LightScene scene;
    compute::Texture output;
    constexpr int W=128,H=64;
    bool wall=false, shapes=false, emissive=false;
    float relief=0, shapeWeight=0;
    float original=.5f;
    int dispatches=0;
    struct Timing {
        double total=0, elapsed=0;
        int count=0;
    };
    std::map<std::string,Timing> timings;
    void require(bool ok,const char* message) {
        if(!ok)throw std::runtime_error(message);
    }
    void fill(GLuint id,int w,int h,const float* value,GLenum format=GL_RGBA) {
        int channels=format==GL_RGBA?4:1;
        std::vector<float> pixels(size_t(w)*h*channels);
        for(size_t i=0;i<pixels.size();i++)pixels[i]=value[i%channels];
        glBindTexture(GL_TEXTURE_2D,id);
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,w,h,format,GL_FLOAT,pixels.data());
    }
    std::vector<float> draw() {
        require(mapLightingDraw(W,H,1,nullptr),"lighting draw failed");
        std::vector<float> p;
        require(compute::read(output.id,W,H,p),"read failed");
        for(float v:p)require(std::isfinite(v),"nonfinite light");
        return p;
    }
    void init() {
        mapLightingShutdown();
        require(mapLightingInit(),"lighting init failed");
        dispatches=0;
    }
    void allOff() {
        for(const char* name:{"normals","colors","shadows","sun","ao","pool","tonemap","night","gi"})settings[std::string("hd_light.")+name]="0";
    }
    void constantField() {
        scene=LightScene {
        };
        scene.ambient=.25f;
        scene.timeOfDay=2200;
        for(int y=-80;y<360;y+=8)for(int x=-80;x<220;x+=8)scene.hexes.push_back({float(x),float(y),.5f});
    }
    void lampField() {
        constantField();
        scene.lights.push_back({32,32,36,5,1,{1,.45f,.2f},LightKind::Lamp,1});
        for(auto& h:scene.hexes) {
            float d=std::hypot((h.x-32)/32,(h.y-32)/13.856f);
            h.intensity=std::max(.25f,1-std::max(d-.5f,0.f)*(.99f/6));
        }
    }
    float at(const std::vector<float>& p,int x,int y,int c=0) {
        return p[(y*W+x)*4+c];
    }
}
namespace fallout {
    Config gGameConfig= {
    };
    bool configGetString(Config*,const char* section,const char* key,char** value,const char* fallback) {
        auto i=settings.find(std::string(section)+"."+key);
        *value=const_cast<char*>(i==settings.end()?fallback:i->second.c_str());
        return i!=settings.end();
    }
    bool configGetInt(Config* c,const char* section,const char* key,int* value,int fallback) {
        char* s;
        bool found=configGetString(c,section,key,&s,"");
        *value=found?std::atoi(s):fallback;
        return found;
    }
}
namespace fallout::hd {
    const LightScene& lightSceneGather(int,int,int,int,const float*) {
        return scene;
    }
    void mapRendererWantShapes(bool wanted) {
        shapes=wanted;
    }
    GLuint mapRendererHdUav(int) {
        require(output.ensure(W,H),"output allocation");
        return output.id;
    }
    bool mapRendererDrawHd(int,const float*) {
        mapRendererHdUav(1);
        float c[]= {
            .5f*original,.5f*original,.5f*original,1
        };
        fill(output.id,W,H,c);
        return true;
    }
    bool mapRendererDrawGbuffer(int,const float*,const MapGbufferDraw& d) {
        const float a[]= {
            .5,.5,.5,1
        },n[]= {
            .5,.5,1,1
        },s[]= {
            0,original,shapeWeight,1
        },m[]= {
            0,0,relief,1
        },coverage[]= {
            1,0,0,1
        };
        const float* values[]= {
            a,n,n,s,m,coverage
        };
        for(int i=0;i<6;i++) {
            if(i==0||i==3)fill(d.targets[i],W,H,values[i]);
            else {
                std::vector<unsigned char> pixels(W*H*4);
                for(size_t j=0;j<pixels.size();j++)pixels[j]=std::lround(values[i][j%4]*255);
                if(i==5&&emissive)for(int yy=28;yy<36;yy++)for(int xx=28;xx<36;xx++)pixels[(yy*W+xx)*4+2]=255;
                glBindTexture(GL_TEXTURE_2D,d.targets[i]);
                glTexSubImage2D(GL_TEXTURE_2D,0,0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            }
        }
        if(wall) {
            std::vector<unsigned> heights(size_t(d.occluderSize[0])*d.occluderSize[1],0);
            float h=100;
            unsigned bits;
            memcpy(&bits,&h,4);
            for(int y=0;y<64;y++)for(int x=68;x<=71;x++)heights[(y+d.occluderOrigin[1])*d.occluderSize[0]+x+d.occluderOrigin[0]]=bits;
            glBindTexture(GL_TEXTURE_2D,d.occluders);
            glTexSubImage2D(GL_TEXTURE_2D,0,0,0,d.occluderSize[0],d.occluderSize[1],GL_RED_INTEGER,GL_UNSIGNED_INT,heights.data());
        }
        return glGetError()==GL_NO_ERROR;
    }
}
extern "C" void __real_glDispatchCompute(GLuint,GLuint,GLuint);
extern "C" void __wrap_glDispatchCompute(GLuint x,GLuint y,GLuint z) {
    static auto begin=reinterpret_cast<PFNGLBEGINQUERYEXTPROC>(eglGetProcAddress("glBeginQueryEXT"));
    static auto end=reinterpret_cast<PFNGLENDQUERYEXTPROC>(eglGetProcAddress("glEndQueryEXT"));
    static auto gen=reinterpret_cast<PFNGLGENQUERIESEXTPROC>(eglGetProcAddress("glGenQueriesEXT"));
    static auto get=reinterpret_cast<PFNGLGETQUERYOBJECTUI64VEXTPROC>(eglGetProcAddress("glGetQueryObjectui64vEXT"));
    static auto del=reinterpret_cast<PFNGLDELETEQUERIESEXTPROC>(eglGetProcAddress("glDeleteQueriesEXT"));
    require(begin&&end&&gen&&get&&del,"timer query unavailable");
    GLint program;
    glGetIntegerv(GL_CURRENT_PROGRAM,&program);
    GLuint shader;
    GLsizei count;
    glGetAttachedShaders(program,1,&count,&shader);
    GLint len;
    glGetShaderiv(shader,GL_SHADER_SOURCE_LENGTH,&len);
    std::string source(len,'\0');
    glGetShaderSource(shader,len,nullptr,source.data());
    auto pos=source.rfind("void main()");
    std::string name=source.substr(pos,100);
    auto cs=name.find("cs");
    name=name.substr(cs,name.find('(',cs)-cs);
    glFinish();
    auto start=std::chrono::steady_clock::now();
    GLuint query;
    gen(1,&query);
    begin(GL_TIME_ELAPSED_EXT,query);
    __real_glDispatchCompute(x,y,z);
    glFinish();
    end(GL_TIME_ELAPSED_EXT);
    GLuint64 ns=0;
    get(query,GL_QUERY_RESULT_EXT,&ns);
    del(1,&query);
    GLint disjoint=0;
    glGetIntegerv(GL_GPU_DISJOINT_EXT,&disjoint);
    require(!disjoint,"GPU timer disjoint");
    auto& t=timings[name];
    t.total+=ns/1e6;
    t.elapsed+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    t.count++;
    dispatches++;
}
int main() {
    EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major,minor;
    if(!eglInitialize(display,&major,&minor))return 2;
    eglBindAPI(EGL_OPENGL_ES_API);
    EGLConfig config;
    EGLint count;
    EGLint attrs[]= {
        EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_NONE
    };
    eglChooseConfig(display,attrs,&config,1,&count);
    EGLint contextAttrs[]= {
        EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,1,EGL_NONE
    };
    EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,contextAttrs);
    if(!eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,context))return 3;
    int result=0;
    try {
        printf("GL %s; %s\n",glGetString(GL_VERSION),glGetString(GL_RENDERER));
        constantField();
        settings["hd_light.mode"]="original";
        init();
        require(!mapLightingActive()&&!shapes,"original active");
        auto p=draw();
        require(at(p,20,20)==.25f&&dispatches==0,"original mismatch");
        settings.clear();
        settings["hd_light.quality"]="off";
        init();
        p=draw();
        require(!mapLightingActive()&&dispatches==0&&at(p,20,20)==.25f,"quality off mismatch");
        settings.clear();
        allOff();
        init();
        p=draw();
        for(int y=0;y<H;y++)for(int x=0;x<W;x++)require(at(p,x,y)==.25f,"all off changed field");
        lampField();
        init();
        wall=false;
        auto clear=draw();
        require(at(clear,32,32)>at(clear,120,32)+.05f,"no falloff");
        settings["hd_light.shadows"]="1";
        settings["hd_light.shadow_strength"]="1";
        init();
        wall=true;
        auto blocked=draw();
        require(at(blocked,100,32)<at(clear,100,32)-.02f,"no shadow");
        wall=false;
        settings["hd_light.colors"]="1";
        init();
        p=draw();
        require(at(p,32,32,0)>at(p,32,32,2),"no light colors");
        // Directional normals must change the lamp shape, without replacing CE's field.
        relief=.5f;
        shapeWeight=.8f;
        settings["hd_light.normals"]="1";
        init();
        auto shaped=draw();
        settings["hd_light.normals"]="0";
        init();
        auto flat=draw();
        require(std::abs(at(shaped,90,32)-at(flat,90,32))>.005f,"normals have no effect");
        relief=shapeWeight=0;
        settings.clear();
        allOff();
        settings["hd_light.gi"]="1";
        init();
        auto bounce=draw();
        settings["hd_light.gi"]="0";
        init();
        auto direct=draw();
        double gain=0;
        for(size_t i=0;i<bounce.size();i+=4)gain+=bounce[i]-direct[i];
        require(gain>1,"GI has no energy");
        settings.clear();
        allOff();
        scene.ambient=1;
        scene.timeOfDay=1200;
        settings["hd_light.sun"]="1";
        settings["hd_light.sun_path"]="0";
        settings["hd_light.sun_dir"]="-1,0,0.5";
        settings["hd_light.sun_gain"]="1";
        wall=false;
        init();
        auto sunClear=draw();
        wall=true;
        auto sunBlocked=draw();
        require(at(sunBlocked,100,32)<at(sunClear,100,32)-.02f,"sun has no shadow");
        settings["hd_light.sun"]="0";
        settings["hd_light.ao"]="1";
        init();
        auto ao=draw();
        settings["hd_light.ao"]="0";
        init();
        auto noAo=draw();
        require(at(ao,78,32)<at(noAo,78,32)-.01f,"AO has no effect");
        wall=false;
        lampField();
        for(const char* quality:{"low","medium","high"}) {
            settings.clear();
            settings["hd_light.quality"]=quality;
            init();
            p=draw();
            int expected=strcmp(quality,"high")==0?11:strcmp(quality,"medium")==0?5:4;
            require(dispatches==expected,"quality dispatch mismatch");
            require((mapLightingBloomSource()!=0)==(strcmp(quality,"low")!=0),"quality bloom mismatch");
            printf("quality %s: %d dispatches\n",quality,dispatches);
        }
        auto shoulder=p;
        settings["hd_light.tonemap_curve"]="agx";
        init();
        p=draw();
        double curveDifference=0;
        for(size_t i=0;i<p.size();i++)curveDifference+=std::abs(p[i]-shoulder[i]);
        require(curveDifference>1,"AgX curve unchanged");
        settings.clear();
        settings["hd_post.enabled"]="0";
        require(mapPostInit(),"post init");
        require(mapPostDraw(output.id,0,W,H)==output.id,"post off mismatch");
        mapPostShutdown();
        emissive=true;
        p=draw();
        settings.clear();
        settings["hd_post.grade"]="0";
        settings["hd_post.vignette"]="0";
        require(mapPostInit(),"post init");
        GLuint post=mapPostDraw(output.id,mapLightingBloomSource(),W,H);
        require(post&&post!=output.id,"post no output");
        int w,h;
        require(mapPostReadback(p,&w,&h)&&w==W&&h==H,"post readback");
        for(float v:p)require(std::isfinite(v),"nonfinite post");
        std::vector<float> raw;
        require(compute::read(output.id,W,H,raw),"raw read");
        require(at(p,42,32)>at(raw,42,32)+.001f,"bloom has no halo");
        emissive=false;
        require(glGetError()==GL_NO_ERROR,"GL error");
        for(auto& [name,t]:timings)printf("GPU %-15s query %.6f ms; dispatch+finish %.3f ms (%d samples; first use included)\n",name.c_str(),t.total/t.count,t.elapsed/t.count,t.count);
        puts("PASS: original/off identity, field identity, falloff, shadow, colors, quality, shoulder/AgX, bloom/post");
    }
    catch(const std::exception& e) {
        fprintf(stderr,"FAIL: %s (GL 0x%x)\n",e.what(),glGetError());
        result=1;
    }
    mapLightingShutdown();
    mapPostShutdown();
    glDeleteTextures(1,&output.id);
    output.id=0;
    eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    eglDestroyContext(display,context);
    eglTerminate(display);
    return result;
}
