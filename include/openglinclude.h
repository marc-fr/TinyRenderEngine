#ifndef INCLUDE
#define INCLUDE

#ifdef WIN32
#include <GL/glew.h>
#else
#define GL_GLEXT_PROTOTYPES
#endif

#include <SDL2/SDL.h>
#include "SDL2/SDL_opengl.h"

#ifdef TRE_WITH_SDL2_IMAGE
#include "SDL2/SDL_image.h"
#endif

#define GLM_FORCE_INTRINSICS
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#endif // INCLUDE

