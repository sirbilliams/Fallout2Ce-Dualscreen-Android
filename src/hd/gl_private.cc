#include "hd/gl_private.h"
#include "debug.h"
#include <algorithm>
namespace fallout::hd {
GlState::GlState()
{
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFbo);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_SCISSOR_BOX, scissor);
    glGetBooleanv(GL_COLOR_WRITEMASK, mask);
    for (int i = 0; i < 5; i++)
        enabled[i] = glIsEnabled(caps[i]);
    const GLenum bp[] = { GL_BLEND_SRC_RGB, GL_BLEND_DST_RGB, GL_BLEND_SRC_ALPHA, GL_BLEND_DST_ALPHA, GL_BLEND_EQUATION_RGB, GL_BLEND_EQUATION_ALPHA };
    for (int i = 0; i < 6; i++)
        glGetIntegerv(bp[i], &blend[i]);
    for (int i = 0; i < 12; i++) {
        glActiveTexture(GL_TEXTURE0 + i);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex[i][0]);
        glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &tex[i][1]);
        glGetIntegerv(GL_SAMPLER_BINDING, &tex[i][2]);
    }
    const GLenum ip[] = { GL_IMAGE_BINDING_NAME, GL_IMAGE_BINDING_LEVEL, GL_IMAGE_BINDING_LAYERED, GL_IMAGE_BINDING_LAYER, GL_IMAGE_BINDING_ACCESS, GL_IMAGE_BINDING_FORMAT };
    glGetIntegerv(GL_MAX_IMAGE_UNITS, &imageCount);
    imageCount = std::min(imageCount, 7);
    for (int i = 0; i < imageCount; i++)
        for (int j = 0; j < 6; j++)
            glGetIntegeri_v(ip[j], i, &images[i][j]);
    glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING, &ssboGeneric);
    for (int i = 0; i < 4; i++) {
        glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, i, &ssbo[i]);
        glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_START, i, &ssboStart[i]);
        glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_SIZE, i, &ssboSize[i]);
    }
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &packBuffer);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpackBuffer);
    const GLenum pp[] = { GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH, GL_PACK_SKIP_PIXELS, GL_PACK_SKIP_ROWS };
    const GLenum up[] = { GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH, GL_UNPACK_SKIP_PIXELS, GL_UNPACK_SKIP_ROWS, GL_UNPACK_IMAGE_HEIGHT, GL_UNPACK_SKIP_IMAGES };
    for (int i = 0; i < 4; i++)
        glGetIntegerv(pp[i], &pack[i]);
    for (int i = 0; i < 6; i++)
        glGetIntegerv(up[i], &unpack[i]);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    for (int i = 0; i < 4; i++)
        glPixelStorei(pp[i], i == 0 ? 1 : 0);
    for (int i = 0; i < 6; i++)
        glPixelStorei(up[i], i == 0 ? 1 : 0);
    glActiveTexture(GL_TEXTURE0);
}
GlState::~GlState()
{
    for (int i = 0; i < 12; i++) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, tex[i][0]);
        glBindTexture(GL_TEXTURE_2D_ARRAY, tex[i][1]);
        glBindSampler(i, tex[i][2]);
    }
    glActiveTexture(active);
    for (int i = 0; i < imageCount; i++)
        glBindImageTexture(i, images[i][0], images[i][1], images[i][2], images[i][3], images[i][4], images[i][5]);
    for (int i = 0; i < 4; i++) {
        if (ssbo[i] && ssboSize[i])
            glBindBufferRange(GL_SHADER_STORAGE_BUFFER, i, ssbo[i], ssboStart[i], ssboSize[i]);
        else
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, i, ssbo[i]);
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboGeneric);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, packBuffer);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpackBuffer);
    const GLenum pp[] = { GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH, GL_PACK_SKIP_PIXELS, GL_PACK_SKIP_ROWS };
    const GLenum up[] = { GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH, GL_UNPACK_SKIP_PIXELS, GL_UNPACK_SKIP_ROWS, GL_UNPACK_IMAGE_HEIGHT, GL_UNPACK_SKIP_IMAGES };
    for (int i = 0; i < 4; i++)
        glPixelStorei(pp[i], pack[i]);
    for (int i = 0; i < 6; i++)
        glPixelStorei(up[i], unpack[i]);
    glUseProgram(program);
    glBindVertexArray(vao);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
    for (int i = 0; i < 5; i++) {
        if (enabled[i])
            glEnable(caps[i]);
        else
            glDisable(caps[i]);
    }
    glColorMask(mask[0], mask[1], mask[2], mask[3]);
    glBlendFuncSeparate(blend[0], blend[1], blend[2], blend[3]);
    glBlendEquationSeparate(blend[4], blend[5]);
}
static GLuint compile(GLenum type, const char* text)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &text, nullptr);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[16000];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        debugPrint("HD: GLSL: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}
static GLuint link(GLuint a, GLuint b = 0)
{
    if (!a) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, a);
    if (b) glAttachShader(p, b);
    glLinkProgram(p);
    glDeleteShader(a);
    if (b) glDeleteShader(b);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[16000];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        debugPrint("HD: GLSL link: %s\n", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}
GLuint glCompute(const std::string& source) { return link(compile(GL_COMPUTE_SHADER, source.c_str())); }
GLuint glProgram(const char* v, const char* f)
{
    GLuint a = compile(GL_VERTEX_SHADER, v), b = compile(GL_FRAGMENT_SHADER, f);
    if (!a || !b) {
        glDeleteShader(a);
        glDeleteShader(b);
        return 0;
    }
    return link(a, b);
}
GLuint glTexture(int w, int h, GLenum format, GLenum type, const void* p)
{
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexStorage2D(GL_TEXTURE_2D, 1, format, w, h);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (p) glUpload(t, 0, 0, w, h, format == GL_RGBA16F || format == GL_RGBA8 ? GL_RGBA : GL_RED_INTEGER, type, p);
    return t;
}
void glUpload(GLuint t, int x, int y, int w, int h, GLenum format, GLenum type, const void* p)
{
    glBindTexture(GL_TEXTURE_2D, t);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, format, type, p);
}
void glBuffer(GLuint b, const void* p, size_t n)
{
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, b);
    glBufferData(GL_SHADER_STORAGE_BUFFER, n ? n : 4, n ? p : nullptr, GL_DYNAMIC_DRAW);
}
} // namespace fallout::hd
