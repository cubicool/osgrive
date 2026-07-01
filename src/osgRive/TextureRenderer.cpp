#include <osgRive/TextureRenderer>

#include "TextureRendererBackend.hpp"

#include <osg/RenderInfo>
#include <osg/State>
#include <osg/Texture2D>

#ifdef OSGRIVE_ENABLE_OSGDEBUG_TRACE
#include <osgDebug.hpp>
#endif

#include <utility>

namespace osgRive
{

class TextureRenderer::Impl
{
public:
    Impl(std::string rivPath, uint32_t width, uint32_t height) :
        m_renderer(std::move(rivPath), width, height)
    {}

    uint32_t width() const { return m_renderer.width(); }
    uint32_t height() const { return m_renderer.height(); }

    void render(osg::RenderInfo& renderInfo,
                osg::Texture2D& target,
                float elapsedSeconds,
                DrawMode drawMode)
    {
        osg::State* state = renderInfo.getState();
        state->applyTextureAttribute(0, &target);

        osg::Texture::TextureObject* textureObject =
            target.getTextureObject(state->getContextID());
        if (!textureObject)
        {
            return;
        }

        {
#ifdef OSGRIVE_ENABLE_OSGDEBUG_TRACE
            osgDebug::Scoped riveScope(100, "osgRive: Rive renderToTexture");
            osgDebug::messageInsert(
                osgDebug::Type::MARKER,
                101,
                osgDebug::Severity::NOTIFICATION,
                "STARTING RIVE");
#endif
            m_renderer.renderToTexture(textureObject->id(),
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
    TextureRendererBackend m_renderer;
};

TextureRenderer::TextureRenderer(std::string rivPath,
                                 uint32_t width,
                                 uint32_t height) :
    m_impl(std::make_unique<Impl>(std::move(rivPath), width, height))
{}

TextureRenderer::~TextureRenderer() = default;

uint32_t TextureRenderer::width() const { return m_impl->width(); }

uint32_t TextureRenderer::height() const { return m_impl->height(); }

void TextureRenderer::render(osg::RenderInfo& renderInfo,
                             osg::Texture2D& target,
                             float elapsedSeconds,
                             DrawMode drawMode)
{
    m_impl->render(renderInfo, target, elapsedSeconds, drawMode);
}

} // namespace osgRive
