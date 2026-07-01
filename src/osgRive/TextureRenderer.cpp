#include <osgRive/TextureRenderer>

#include "rive_texture_renderer.hpp"

#include <osg/RenderInfo>
#include <osg/State>
#include <osg/Texture2D>

#include <utility>

namespace osgRive
{
namespace
{

RiveDrawMode toRiveDrawMode(DrawMode mode)
{
    switch (mode)
    {
        case DrawMode::SCENE:
            return RiveDrawMode::scene;
        case DrawMode::ARTBOARD:
            return RiveDrawMode::artboard;
        case DrawMode::ARTBOARD_INTERNAL:
            return RiveDrawMode::artboardInternal;
        case DrawMode::NONE:
            return RiveDrawMode::none;
    }
    return RiveDrawMode::scene;
}

} // namespace

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

        m_renderer.renderToTexture(textureObject->id(),
                                   elapsedSeconds,
                                   toRiveDrawMode(drawMode));
    }

private:
    RiveTextureRenderer m_renderer;
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
