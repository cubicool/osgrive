#pragma once

#include "glad_custom.h"

#include <cstdint>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef OSGRIVE_DEBUG_GL_ERRORS
#include <cstdio>
#endif

namespace osgRive
{

// Shared by TextureRendererBackend.cpp and FramebufferRendererBackend.cpp:
// both are OSG-agnostic Rive backends that need glad's GL entry points
// loaded and a .riv file read from disk, with no OSG/Rive-object-lifecycle
// coupling in either helper. Marked inline (not anonymous-namespace) so
// ODR-merges into one shared definition across translation units instead
// of duplicating per-TU -- harmless either way since the underlying glad
// function-pointer storage is itself process-global (compiled once into
// Rive's own static libraries), but this avoids copy-pasting the loader
// boilerplate.

extern "C" void (*glXGetProcAddressARB(const GLubyte* procName))(void);

inline GLADapiproc getGLProcAddress(const char* name)
{
    return reinterpret_cast<GLADapiproc>(
        glXGetProcAddressARB(reinterpret_cast<const GLubyte*>(name)));
}

inline void ensureRiveGLLoaded()
{
    static bool loaded = false;
    if (loaded)
    {
        return;
    }

    if (!gladLoadCustomLoader(getGLProcAddress))
    {
        throw std::runtime_error("failed to load Rive GL entry points");
    }
    loaded = true;
}

inline std::vector<uint8_t> readBinaryFile(const std::string& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        throw std::runtime_error("failed to open " + path);
    }
    return {std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()};
}

#ifdef OSGRIVE_DEBUG_GL_ERRORS
inline void drainGLErrors(const char* label)
{
    bool printedHeader = false;
    for (;;)
    {
        GLenum err = glGetError();
        if (err == GL_NO_ERROR)
        {
            return;
        }

        if (!printedHeader)
        {
            std::fprintf(stderr, "Rive GL errors drained at %s:\n", label);
            printedHeader = true;
        }
        std::fprintf(stderr, "  0x%04x\n", static_cast<unsigned int>(err));
    }
}
#endif

} // namespace osgRive
