#include <osgRive/Scene>

#include "rive_texture_renderer.hpp"

#include <osg/FrameStamp>
#include <osg/GL>
#include <osg/State>

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

osg::ref_ptr<osg::Texture2D> makeRiveTexture(unsigned int width, unsigned int height)
{
    osg::ref_ptr<osg::Texture2D> texture = new osg::Texture2D;
    texture->setTextureSize(static_cast<int>(width), static_cast<int>(height));
    // GL_RGBA8 is required: Rive's PLS renderer calls glBindImageTexture with
    // GL_RGBA8, which needs a sized (not unsized) internal format. GL_RGBA
    // also fails glCheckFramebufferStatus in strict core profiles.
    texture->setInternalFormat(GL_RGBA8);
    texture->setSourceFormat(GL_RGBA);
    texture->setSourceType(GL_UNSIGNED_BYTE);
    texture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
    texture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
    texture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
    texture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
    return texture;
}

} // namespace

class Scene::Impl
{
public:
    Impl(std::string rivPath, unsigned int width, unsigned int height) :
        m_renderer(std::move(rivPath), width, height)
    {}

    RiveTextureRenderer m_renderer;
};

Scene::Scene() {}

Scene::Scene(const std::string& rivPath, unsigned int width, unsigned int height) :
    m_rivPath(rivPath),
    m_width(width),
    m_height(height)
{
    // This Drawable produces no visible geometry of its own — it's a
    // side-effecting texture producer with no meaningful location, so it
    // must never be frustum-culled away.
    setCullingActive(false);
    setUseDisplayList(false);
    setUseVertexBufferObjects(false);

    m_texture = makeRiveTexture(width, height);
    m_impl = std::make_unique<Impl>(rivPath, width, height);
}

Scene::Scene(const Scene& scene, const osg::CopyOp& copyop) :
    osg::Drawable(scene, copyop),
    m_rivPath(scene.m_rivPath),
    m_width(scene.m_width),
    m_height(scene.m_height),
    m_drawMode(scene.m_drawMode)
{
    setCullingActive(false);
    setUseDisplayList(false);
    setUseVertexBufferObjects(false);

    // Deliberately not a deep copy: m_impl owns live GL resources (a Rive
    // RenderContext, its own GL texture, an open .riv File), which cannot be
    // meaningfully duplicated. A "copy" of a Scene is a fresh, independent
    // instance configured the same way (same .riv path/dimensions/draw
    // mode), not a shared or cloned Rive renderer.
    if (!m_rivPath.empty())
    {
        m_texture = makeRiveTexture(m_width, m_height);
        m_impl = std::make_unique<Impl>(m_rivPath, m_width, m_height);
    }
}

Scene::~Scene() = default;

void Scene::setDrawMode(DrawMode mode) { m_drawMode = mode; }

DrawMode Scene::getDrawMode() const { return m_drawMode; }

osg::Texture2D* Scene::getTexture() { return m_texture.get(); }

const osg::Texture2D* Scene::getTexture() const { return m_texture.get(); }

void Scene::drawImplementation(osg::RenderInfo& renderInfo) const
{
    if (!m_impl || !m_texture)
    {
        return;
    }

    osg::State* state = renderInfo.getState();
    state->applyTextureAttribute(0, m_texture.get());

    osg::Texture::TextureObject* textureObject =
        m_texture->getTextureObject(state->getContextID());
    if (!textureObject)
    {
        return;
    }

    const osg::FrameStamp* frameStamp = state->getFrameStamp();
    double simTime = frameStamp ? frameStamp->getSimulationTime() : 0.0;
    // Sentinel-initialized so frame 0 computes dt = 0 rather than a bogus
    // large jump from simTime - 0.
    double dt = (m_lastSimTime >= 0.0) ? (simTime - m_lastSimTime) : 0.0;
    m_lastSimTime = simTime;

    m_impl->m_renderer.renderToTexture(textureObject->id(),
                                       static_cast<float>(dt),
                                       toRiveDrawMode(m_drawMode));
    // Nothing else. No forceOSGMainPassState, no raw glEnable/glDisable/
    // glBlendFunc. The next Drawable in the RenderBin gets its own StateSet
    // applied via osg::State's normal per-leaf path in RenderLeaf::render(),
    // which reliably re-issues whatever GL state it needs — because it's a
    // real nested apply(), not a standalone one.
}

osg::BoundingBox Scene::computeBoundingBox() const
{
    // Deliberately invalid/empty: this Drawable has no visible geometry of
    // its own (it only writes into a texture sampled elsewhere), so it must
    // not contribute to the scene's bounding sphere -- an arbitrary non-empty
    // box here would skew camera auto-framing (e.g. TrackballManipulator's
    // home position) for whatever actual geometry samples the texture.
    return osg::BoundingBox();
}

} // namespace osgRive
