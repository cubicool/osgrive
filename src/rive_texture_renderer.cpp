#include "rive_texture_renderer.hpp"

#include "glad_custom.h"

#include "rive/animation/state_machine_instance.hpp"
#include "rive/artboard.hpp"
#include "rive/file.hpp"
#include "rive/renderer/gl/render_context_gl_impl.hpp"
#include "rive/renderer/gl/render_target_gl.hpp"
#include "rive/renderer.hpp"
#include "rive/renderer/rive_renderer.hpp"
#include "rive/renderer/texture.hpp"
#include "rive/scene.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
extern "C" void (*glXGetProcAddressARB(const GLubyte* procName))(void);

GLADapiproc getGLProcAddress(const char* name)
{
    return reinterpret_cast<GLADapiproc>(
        glXGetProcAddressARB(reinterpret_cast<const GLubyte*>(name)));
}

void ensureRiveGLLoaded()
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

std::vector<uint8_t> readBinaryFile(const std::string& path)
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
void drainGLErrors(const char* label)
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

class ScopedGLRestore
{
public:
    ScopedGLRestore()
    {
        glGetIntegerv(GL_CURRENT_PROGRAM, &m_program);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &m_activeTexture);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &m_texture2D);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &m_arrayBuffer);
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &m_elementArrayBuffer);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &m_vertexArray);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &m_drawFramebuffer);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &m_readFramebuffer);
        glGetIntegerv(GL_VIEWPORT, m_viewport);
        m_blend = glIsEnabled(GL_BLEND);
        m_depthTest = glIsEnabled(GL_DEPTH_TEST);
        m_cullFace = glIsEnabled(GL_CULL_FACE);
        m_scissorTest = glIsEnabled(GL_SCISSOR_TEST);
    }

    ~ScopedGLRestore()
    {
        restoreEnable(GL_BLEND, m_blend);
        restoreEnable(GL_DEPTH_TEST, m_depthTest);
        restoreEnable(GL_CULL_FACE, m_cullFace);
        restoreEnable(GL_SCISSOR_TEST, m_scissorTest);
        glUseProgram(static_cast<GLuint>(m_program));
        glActiveTexture(static_cast<GLenum>(m_activeTexture));
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(m_texture2D));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(m_arrayBuffer));
        // Restore VAO before EAB: GL_ELEMENT_ARRAY_BUFFER is VAO-local state,
        // so switching VAO first ensures the EAB binding lands in the right VAO.
        glBindVertexArray(static_cast<GLuint>(m_vertexArray));
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLuint>(m_elementArrayBuffer));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,
                          static_cast<GLuint>(m_drawFramebuffer));
        glBindFramebuffer(GL_READ_FRAMEBUFFER,
                          static_cast<GLuint>(m_readFramebuffer));
        glViewport(m_viewport[0], m_viewport[1], m_viewport[2], m_viewport[3]);
    }

private:
    static void restoreEnable(GLenum cap, GLboolean enabled)
    {
        if (enabled)
        {
            glEnable(cap);
        }
        else
        {
            glDisable(cap);
        }
    }

    GLint m_program = 0;
    GLint m_activeTexture = GL_TEXTURE0;
    GLint m_texture2D = 0;
    GLint m_arrayBuffer = 0;
    GLint m_elementArrayBuffer = 0;
    GLint m_vertexArray = 0;
    GLint m_drawFramebuffer = 0;
    GLint m_readFramebuffer = 0;
    GLint m_viewport[4] = {0, 0, 0, 0};
    GLboolean m_blend = GL_FALSE;
    GLboolean m_depthTest = GL_FALSE;
    GLboolean m_cullFace = GL_FALSE;
    GLboolean m_scissorTest = GL_FALSE;
};
} // namespace

class RiveTextureRenderer::Impl
{
public:
    Impl(std::string rivPath, uint32_t width, uint32_t height) :
        m_rivPath(std::move(rivPath)), m_width(width), m_height(height)
    {}

    uint32_t textureID() const { return m_textureID; }
    uint32_t width() const { return m_width; }
    uint32_t height() const { return m_height; }

    void render(float elapsedSeconds)
    {
        ensureRiveGLLoaded();
        ScopedGLRestore restoreGL;
        ensureInitialized();
        renderToTarget(elapsedSeconds, m_renderTarget.get(), RiveDrawMode::scene);
    }

    void renderToCurrentFramebuffer(float elapsedSeconds)
    {
        renderToCurrentFramebuffer(elapsedSeconds, RiveDrawMode::scene);
    }

    void renderToCurrentFramebuffer(float elapsedSeconds, RiveDrawMode drawMode)
    {
        ensureRiveGLLoaded();
        ScopedGLRestore restoreGL;
        ensureInitialized();

        GLint drawFramebuffer = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);

        auto framebufferTarget =
            rive::make_rcp<rive::gpu::FramebufferRenderTargetGL>(
                m_width,
                m_height,
                static_cast<GLuint>(drawFramebuffer),
                0);

        // Preserve existing framebuffer content (e.g. OSG's rendered scene)
        // as Rive's background instead of clearing to a solid color.
        renderToTarget(elapsedSeconds,
                       framebufferTarget.get(),
                       drawMode,
                       rive::gpu::LoadAction::preserveRenderTarget);
    }

    void renderToTexture(uint32_t textureID, float elapsedSeconds)
    {
        renderToTexture(textureID, elapsedSeconds, RiveDrawMode::scene);
    }

    void renderToTexture(uint32_t textureID,
                         float elapsedSeconds,
                         RiveDrawMode drawMode)
    {
        // glad must be loaded before ScopedGLRestore can call any GL function.
        // ensureRiveGLLoaded() is idempotent (static bool guard).
        ensureRiveGLLoaded();
        // Capture host GL state before ensureInitialized() so we restore the
        // caller's clean state (e.g. OSG's texture/FBO/VAO bindings), not
        // whatever Rive's MakeContext() leaves behind on the first frame.
        ScopedGLRestore restoreGL;
        ensureInitialized();

        if (!m_externalTextureTarget)
        {
            m_externalTextureTarget =
                rive::make_rcp<rive::gpu::TextureRenderTargetGL>(m_width,
                                                                 m_height);
        }
        m_externalTextureTarget->setTargetTexture(textureID);

        // Clear to fully transparent so the quad's alpha blend lets whatever
        // is behind it (OSG scene, other geometry) show through by default.
        renderToTarget(elapsedSeconds,
                       m_externalTextureTarget.get(),
                       drawMode,
                       rive::gpu::LoadAction::clear,
                       0x00000000u);
    }

    void clearTexture(uint32_t textureID)
    {
        ensureRiveGLLoaded();
        ScopedGLRestore restoreGL;
        ensureInitialized();

        if (!m_externalTextureTarget)
        {
            m_externalTextureTarget =
                rive::make_rcp<rive::gpu::TextureRenderTargetGL>(m_width,
                                                                 m_height);
        }
        m_externalTextureTarget->setTargetTexture(textureID);

        auto* glImpl =
            m_renderContext
                ->static_impl_cast<rive::gpu::RenderContextGLImpl>();

        glImpl->invalidateGLState();

        m_renderContext->beginFrame({
            .renderTargetWidth = m_width,
            .renderTargetHeight = m_height,
            .loadAction = rive::gpu::LoadAction::clear,
            // .clearColor = 0xffff00ff,
            .clearColor = 0xffff0000,
        });

        m_renderContext->flush({.renderTarget = m_externalTextureTarget.get()});

        glImpl->unbindGLInternalResources();
#ifdef OSGRIVE_DEBUG_GL_ERRORS
        drainGLErrors("after Rive clear-only");
#endif
    }

private:
    void renderToTarget(float elapsedSeconds,
                        rive::gpu::RenderTarget* target,
                        RiveDrawMode drawMode,
                        rive::gpu::LoadAction loadAction = rive::gpu::LoadAction::clear,
                        // uint32_t clearColor = 0xffff00ff)
                        uint32_t clearColor = 0xffff0000)
    {
#ifdef OSGRIVE_DEBUG_GL_ERRORS
        drainGLErrors("before Rive render");
#endif

        auto* glImpl =
            m_renderContext
                ->static_impl_cast<rive::gpu::RenderContextGLImpl>();

        glImpl->invalidateGLState();

        rive::gpu::RenderContext::FrameDescriptor frameDescriptor = {
            .renderTargetWidth = m_width,
            .renderTargetHeight = m_height,
            .loadAction = loadAction,
            .clearColor = clearColor,
        };
        m_renderContext->beginFrame(frameDescriptor);

        if (m_scene && drawMode != RiveDrawMode::none)
        {
            m_scene->advanceAndApply(elapsedSeconds);
        }
        else if (drawMode != RiveDrawMode::none)
        {
            m_artboard->advance(elapsedSeconds);
        }

        rive::RiveRenderer renderer(m_renderContext.get());
        renderer.save();
        renderer.transform(rive::computeAlignment(
            rive::Fit::contain,
            rive::Alignment::center,
            rive::AABB(0.0f,
                       0.0f,
                       static_cast<float>(m_width),
                       static_cast<float>(m_height)),
            m_artboard->bounds()));
        switch (drawMode)
        {
            case RiveDrawMode::scene:
                if (m_scene)
                {
                    m_scene->draw(&renderer);
                }
                else
                {
                    m_artboard->draw(&renderer);
                }
                break;
            case RiveDrawMode::artboard:
                m_artboard->draw(&renderer);
                break;
            case RiveDrawMode::artboardInternal:
                m_artboard->drawInternal(&renderer);
                break;
            case RiveDrawMode::none:
                break;
        }
        renderer.restore();

        m_renderContext->flush({.renderTarget = target});

        glImpl->unbindGLInternalResources();

#ifdef OSGRIVE_DEBUG_GL_ERRORS
        drainGLErrors("after Rive render");
#endif
    }

    void ensureInitialized()
    {
        if (m_renderContext)
        {
            return;
        }

        ensureRiveGLLoaded();

        glGenTextures(1, &m_textureID);
        glBindTexture(GL_TEXTURE_2D, m_textureID);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_RGBA8,
                     static_cast<GLsizei>(m_width),
                     static_cast<GLsizei>(m_height),
                     0,
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);

        m_renderContext = rive::gpu::RenderContextGLImpl::MakeContext();
        if (!m_renderContext)
        {
            throw std::runtime_error(
                "Rive GL RenderContext creation failed; OpenGL 4.2+ is "
                "required by this backend");
        }

        m_renderTarget =
            rive::make_rcp<rive::gpu::TextureRenderTargetGL>(m_width,
                                                             m_height);
        m_renderTarget->setTargetTexture(m_textureID);

        auto rivBytes = readBinaryFile(m_rivPath);
        m_file = rive::File::import(rivBytes, m_renderContext.get());
        if (!m_file)
        {
            throw std::runtime_error("Rive import failed for " + m_rivPath);
        }

        m_artboard = m_file->artboardDefault();
        if (!m_artboard)
        {
            throw std::runtime_error("Rive file has no default artboard");
        }

        m_scene = m_artboard->defaultStateMachine();
        if (!m_scene && m_artboard->animationCount() > 0)
        {
            m_scene = m_artboard->animationAt(0);
        }

        if (m_scene)
        {
            m_scene->advanceAndApply(0.0f);
        }
        else
        {
            m_artboard->advance(0.0f);
        }
    }

    std::string m_rivPath;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    GLuint m_textureID = 0;

    std::unique_ptr<rive::gpu::RenderContext> m_renderContext;
    rive::rcp<rive::gpu::TextureRenderTargetGL> m_renderTarget;
    rive::rcp<rive::gpu::TextureRenderTargetGL> m_externalTextureTarget;
    rive::rcp<rive::File> m_file;
    std::unique_ptr<rive::ArtboardInstance> m_artboard;
    std::unique_ptr<rive::Scene> m_scene;
};

RiveTextureRenderer::RiveTextureRenderer(std::string rivPath,
                                         uint32_t width,
                                         uint32_t height) :
    m_impl(std::make_unique<Impl>(std::move(rivPath), width, height))
{}

RiveTextureRenderer::~RiveTextureRenderer() = default;

uint32_t RiveTextureRenderer::textureID() const { return m_impl->textureID(); }

uint32_t RiveTextureRenderer::width() const { return m_impl->width(); }

uint32_t RiveTextureRenderer::height() const { return m_impl->height(); }

void RiveTextureRenderer::render(float elapsedSeconds)
{
    m_impl->render(elapsedSeconds);
}

void RiveTextureRenderer::renderToCurrentFramebuffer(float elapsedSeconds)
{
    m_impl->renderToCurrentFramebuffer(elapsedSeconds);
}

void RiveTextureRenderer::renderToCurrentFramebuffer(float elapsedSeconds,
                                                     RiveDrawMode drawMode)
{
    m_impl->renderToCurrentFramebuffer(elapsedSeconds, drawMode);
}

void RiveTextureRenderer::renderToTexture(uint32_t textureID,
                                          float elapsedSeconds)
{
    m_impl->renderToTexture(textureID, elapsedSeconds);
}

void RiveTextureRenderer::renderToTexture(uint32_t textureID,
                                          float elapsedSeconds,
                                          RiveDrawMode drawMode)
{
    m_impl->renderToTexture(textureID, elapsedSeconds, drawMode);
}

void RiveTextureRenderer::clearTexture(uint32_t textureID)
{
    m_impl->clearTexture(textureID);
}
