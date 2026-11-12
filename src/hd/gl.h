#ifndef FALLOUT_HD_GL_H_
#define FALLOUT_HD_GL_H_

// Dustlight HD port: the GPU API is OpenGL ES 3.1 on Android and desktop
// (desktop: an ES 3.1 context from Mesa via SDL/EGL). GL is linked
// directly: GLESv3 on Android, Mesa's GLESv2 on desktop (it exports ES 3.x).
// Headers are vendored in third_party/khronos. hd/gl.cc (lane L3) checks
// the context. Every hd/*.cc that calls GL includes this.
// Dustlight's D3D11 handles became GL object names (GLuint): textures for
// shader resource / render target / image (UAV) views.

#include <GLES3/gl31.h>

namespace fallout::hd {

// Checks the current SDL GL context: false unless it is ES 3.1 or newer.
bool hdGlLoad();

// True when the context has GL_EXT_texture_compression_bptc (BC7 upload).
bool hdGlHasBc7();

} // namespace fallout::hd

#endif /* FALLOUT_HD_GL_H_ */
