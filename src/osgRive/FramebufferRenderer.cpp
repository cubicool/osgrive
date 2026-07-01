#include <osgRive/FramebufferRenderer>

#include "FramebufferRendererBackend.hpp"
#include "ScopedOSGStateRestore.hpp"

#include <osg/GL>
#include <osg/RenderInfo>
#include <osg/State>

#ifdef OSGRIVE_ENABLE_OSGDEBUG_TRACE
#include <osgDebug.hpp>
#endif

#include <utility>

namespace osgRive
{

class FramebufferRenderer::Impl
{
public:
    Impl(std::string rivPath, uint32_t width, uint32_t height) :
        m_renderer(std::move(rivPath), width, height)
    {}

    uint32_t width() const { return m_renderer.width(); }
    uint32_t height() const { return m_renderer.height(); }

    void render(osg::RenderInfo& renderInfo,
                float elapsedSeconds,
                DrawMode drawMode)
    {
        osg::State* state = renderInfo.getState();

        // No OSG-native way to ask "what framebuffer is currently bound" --
        // this is the one raw GL read left in the framebuffer-mode path,
        // mirroring the equivalent query in the legacy monolith's
        // RiveFramebufferCallback.
        GLint drawFramebuffer = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);

        {
            ScopedOSGStateRestore restoreOSGState(state);
#ifdef OSGRIVE_ENABLE_OSGDEBUG_TRACE
            osgDebug::Scoped riveScope(100, "osgRive: Rive renderToCurrentFramebuffer");
            osgDebug::messageInsert(
                osgDebug::Type::MARKER,
                101,
                osgDebug::Severity::NOTIFICATION,
                "STARTING RIVE");
#endif
            m_renderer.renderToCurrentFramebuffer(
                static_cast<uint32_t>(drawFramebuffer),
                elapsedSeconds,
                drawMode);
#ifdef OSGRIVE_ENABLE_OSGDEBUG_TRACE
            osgDebug::messageInsert(
                osgDebug::Type::MARKER,
                102,
                osgDebug::Severity::NOTIFICATION,
                "STOPPING RIVE");
#endif
        }
    }

private:
    FramebufferRendererBackend m_renderer;
};

FramebufferRenderer::FramebufferRenderer(std::string rivPath,
                                         uint32_t width,
                                         uint32_t height) :
    m_impl(std::make_unique<Impl>(std::move(rivPath), width, height))
{}

FramebufferRenderer::~FramebufferRenderer() = default;

uint32_t FramebufferRenderer::width() const { return m_impl->width(); }

uint32_t FramebufferRenderer::height() const { return m_impl->height(); }

void FramebufferRenderer::render(osg::RenderInfo& renderInfo,
                                 float elapsedSeconds,
                                 DrawMode drawMode)
{
    m_impl->render(renderInfo, elapsedSeconds, drawMode);
}

} // namespace osgRive
