#include <osgRive/TextureRenderer>

#include "TextureRendererBackend.hpp"

#include <osg/GL>
#include <osg/RenderInfo>
#include <osg/State>
#include <osg/Texture2D>
#include <osg/Viewport>

#ifdef OSGRIVE_ENABLE_OSGDEBUG_TRACE
#include <osgDebug.hpp>
#endif

#include <utility>

namespace osgRive
{

namespace
{

// Rive's own GL calls change GL_VIEWPORT/GL_CULL_FACE/GL_SCISSOR_TEST and
// GL_ACTIVE_TEXTURE/unit-0's texture binding behind OSG's back (outside any
// osg::StateAttribute/StateSet apply). Captures what OSG's state cache
// currently believes is applied so it can be restored through OSG's own
// public State API afterward -- no raw gl* calls needed.
class ScopedOSGStateRestore
{
public:
    ScopedOSGStateRestore(osg::State* state, osg::Texture2D* texture) :
        m_state(state),
        m_texture(texture),
        m_cullFace(state->getLastAppliedMode(GL_CULL_FACE)),
        m_scissorTest(state->getLastAppliedMode(GL_SCISSOR_TEST)),
        m_viewport(state->getCurrentViewport()
                       ? new osg::Viewport(*state->getCurrentViewport())
                       : nullptr)
    {}

    ~ScopedOSGStateRestore()
    {
        // haveAppliedMode(mode) flips the state cache's notion of the last
        // applied value without issuing a GL call -- exactly the "this was
        // set externally" hook OSG provides for this situation. Without it,
        // applyMode() would see its cached value already matching the
        // restore target and skip the real glEnable/glDisable call.
        m_state->haveAppliedMode(GL_CULL_FACE);
        m_state->applyMode(GL_CULL_FACE, m_cullFace);
        m_state->haveAppliedMode(GL_SCISSOR_TEST);
        m_state->applyMode(GL_SCISSOR_TEST, m_scissorTest);

        // applyAttribute() diffs by pointer identity, so a freshly cloned
        // Viewport always forces a real glViewport call regardless of what
        // the cache's last-applied pointer was.
        if (m_viewport.valid())
        {
            m_state->applyAttribute(m_viewport.get());
        }

        // Rive leaves the real active texture unit on a high unit (observed
        // GL_TEXTURE14) and unbinds unit 0's texture entirely
        // (unbindGLInternalResources loops units 0-14). Null out OSG's
        // cached texture-attribute pointer for unit 0 so applyTextureAttribute
        // below is not a same-pointer no-op, then bounce the active unit
        // through a different value so setActiveTextureUnit(0) is guaranteed
        // to issue a real glActiveTexture regardless of what OSG's cache
        // currently (incorrectly) believes.
        m_state->haveAppliedTextureAttribute(0, osg::StateAttribute::TEXTURE);
        m_state->setActiveTextureUnit(1);
        m_state->setActiveTextureUnit(0);
        m_state->applyTextureAttribute(0, m_texture);
    }

private:
    osg::State* m_state;
    osg::Texture2D* m_texture;
    bool m_cullFace;
    bool m_scissorTest;
    osg::ref_ptr<osg::Viewport> m_viewport;
};

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

        {
            ScopedOSGStateRestore restoreOSGState(state, &target);
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
