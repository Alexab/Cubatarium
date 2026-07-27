#ifndef GL_INCLUDES_H
#define GL_INCLUDES_H

#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
// GLES 3.1+: compute shaders, SSBO, glDispatchCompute.
#include <GLES3/gl31.h>
#else
#include <GL/glew.h>
#endif

#endif
