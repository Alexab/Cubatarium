#ifndef GL_INCLUDES_H
#define GL_INCLUDES_H

#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
#include <GLES3/gl3.h>
#else
#include <GL/glew.h>
#endif

#endif
