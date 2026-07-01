#include <osgRive/Scene>

#include <osgRive/FramebufferRenderer>

#include <osg/BlendFunc>
#include <osg/BoundingBox>
#include <osg/ColorMask>
#include <osg/Drawable>
#include <osg/FrameStamp>
#include <osg/GL>
#include <osg/Geometry>
#include <osg/Program>
#include <osg/Shader>
#include <osg/State>
#include <osg/StateSet>
#include <osg/Uniform>
#include <osg/Vec3>

#include <memory>

namespace osgRive {

namespace {

static const char* kQuadVert = R"(
#version 330 core

in vec4 osg_Vertex;
in vec2 osg_MultiTexCoord0;

uniform mat4 osg_ModelViewProjectionMatrix;

out vec2 uv;

void main() {
	uv = osg_MultiTexCoord0;
	gl_Position = osg_ModelViewProjectionMatrix * osg_Vertex;
}
)";

static const char* kQuadFrag = R"(
#version 330 core

uniform sampler2D colorTex;

in vec2 uv;

out vec4 color;

void main(){
	color = texture(colorTex, uv);
}
)";

osg::ref_ptr<osg::Texture2D> makeRiveTexture(unsigned int width, unsigned int height) {
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

osg::ref_ptr<osg::Geometry> makeDisplayQuad(osg::Texture2D* texture) {
	osg::ref_ptr<osg::Geometry> quad = osg::createTexturedQuadGeometry(
		osg::Vec3(0.0f, 0.0f, 0.0f),
		osg::Vec3(1.0f, 0.0f, 0.0f),
		osg::Vec3(0.0f, 1.0f, 0.0f)
	);

	quad->setUseDisplayList(false);
	quad->setUseVertexBufferObjects(true);

	auto* stateSet = quad->getOrCreateStateSet();

	stateSet->setTextureAttributeAndModes(0, texture, osg::StateAttribute::ON);
	stateSet->addUniform(new osg::Uniform("colorTex", 0));
	stateSet->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
	stateSet->setMode(GL_CULL_FACE, osg::StateAttribute::OFF);
	stateSet->setMode(GL_BLEND, osg::StateAttribute::ON);
	// Rive's renderer writes premultiplied color into the texture, so use
	// premultiplied alpha blending when compositing that texture in OSG.
	stateSet->setAttributeAndModes( new osg::BlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));
	stateSet->setAttributeAndModes(new osg::ColorMask(true, true, true, true));
	stateSet->setRenderBinDetails(1, "RenderBin");

	auto* program = new osg::Program;

	program->addShader(new osg::Shader(osg::Shader::VERTEX, kQuadVert));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, kQuadFrag));
	stateSet->setAttributeAndModes(program);

	return quad;
}

}

class Scene::TextureRenderDrawable : public osg::Drawable {
public:
	TextureRenderDrawable() = default;
	TextureRenderDrawable(const std::string& rivPath, unsigned int width, unsigned int height);
	TextureRenderDrawable(const TextureRenderDrawable& drawable, const osg::CopyOp& co=osg::CopyOp::SHALLOW_COPY);

	META_Object(osgRive, TextureRenderDrawable)

	void setDrawMode(DrawMode mode);
	DrawMode getDrawMode() const;

	osg::Texture2D* getTexture();
	const osg::Texture2D* getTexture() const;

	void drawImplementation(osg::RenderInfo& renderInfo) const override;
	osg::BoundingBox computeBoundingBox() const override;

protected:
	~TextureRenderDrawable() override;

private:
	class Impl {
	public:
		Impl(std::string rivPath, unsigned int width, unsigned int height):
		m_renderer(std::move(rivPath), width, height) {}

		TextureRenderer m_renderer;
	};

	std::unique_ptr<Impl> m_impl;
	osg::ref_ptr<osg::Texture2D> m_texture;
	std::string m_rivPath;
	unsigned int m_width = 0;
	unsigned int m_height = 0;
	DrawMode m_drawMode = DrawMode::SCENE;
	mutable double m_lastSimTime = -1.0;
};

// RenderTarget::FRAMEBUFFER producer: renders directly into whatever
// framebuffer is bound when its turn comes up in the traversal (typically
// OSG's own back buffer) instead of into an owned texture. No visible
// geometry of its own, same as RenderDrawable, and for the same reason
// (see computeBoundingBox() below) -- but unlike RenderDrawable, it is the
// *only* child Scene owns in this mode: there is no display quad to sample
// a texture, since Rive is compositing directly into the framebuffer.
class Scene::FramebufferRenderDrawable: public osg::Drawable {
public:
	FramebufferRenderDrawable() = default;
	FramebufferRenderDrawable(const std::string& rivPath, unsigned int width, unsigned int height);
	FramebufferRenderDrawable(
		const FramebufferRenderDrawable& drawable,
		const osg::CopyOp& copyop = osg::CopyOp::SHALLOW_COPY
	);

	META_Object(osgRive, FramebufferRenderDrawable)

	void setDrawMode(DrawMode mode);
	DrawMode getDrawMode() const;

	void drawImplementation(osg::RenderInfo& renderInfo) const override;
	osg::BoundingBox computeBoundingBox() const override;

protected:
	~FramebufferRenderDrawable() override;

private:
	class Impl {
	public:
		Impl(std::string rivPath, unsigned int width, unsigned int height):
		m_renderer(std::move(rivPath), width, height) {}

		FramebufferRenderer m_renderer;
	};

	std::unique_ptr<Impl> m_impl;
	std::string m_rivPath;
	unsigned int m_width = 0;
	unsigned int m_height = 0;
	DrawMode m_drawMode = DrawMode::SCENE;
	mutable double m_lastSimTime = -1.0;
};

Scene::Scene() {}

Scene::Scene(
	const std::string& rivPath,
	unsigned int width,
	unsigned int height,
	RenderTarget renderTarget
):
m_rivPath(rivPath),
m_width(width),
m_height(height),
m_renderTarget(renderTarget) {
	init(rivPath, width, height);
}

Scene::Scene(const Scene& scene, const osg::CopyOp& copyop):
osg::Group(scene, copyop),
m_rivPath(scene.m_rivPath),
m_width(scene.m_width),
m_height(scene.m_height),
m_drawMode(scene.m_drawMode),
m_renderTarget(scene.m_renderTarget) {
	removeChildren(0, getNumChildren());

	if(!m_rivPath.empty()) {
		init(m_rivPath, m_width, m_height);
		setDrawMode(m_drawMode);
	}
}

Scene::~Scene() = default;

void Scene::setDrawMode(DrawMode mode) {
	m_drawMode = mode;

	if(m_renderDrawable) m_renderDrawable->setDrawMode(mode);

	if(m_framebufferDrawable) m_framebufferDrawable->setDrawMode(mode);
}

DrawMode Scene::getDrawMode() const { return m_drawMode; }

RenderTarget Scene::getRenderTarget() const { return m_renderTarget; }

osg::Texture2D* Scene::getTexture() {
	return m_renderDrawable ? m_renderDrawable->getTexture() : nullptr;
}

const osg::Texture2D* Scene::getTexture() const {
	return m_renderDrawable ? m_renderDrawable->getTexture() : nullptr;
}

osg::Geode* Scene::getDisplayGeode() { return m_displayGeode.get(); }

const osg::Geode* Scene::getDisplayGeode() const { return m_displayGeode.get(); }

void Scene::init(const std::string& rivPath, unsigned int width, unsigned int height) {
	if(m_renderTarget == RenderTarget::FRAMEBUFFER) {
		m_framebufferDrawable = new FramebufferRenderDrawable(rivPath, width, height);
		m_framebufferDrawable->setDrawMode(m_drawMode);
		// A high bin number so this draws after any other content the
		// caller's scene graph contributes this frame (default OSG content
		// sits at bin 0 in the same "RenderBin" bin group unless it says
		// otherwise) -- mirrors the legacy monolith's post-draw-callback
		// timing ("Rive composites on top of whatever OSG left in the back
		// buffer") using in-graph traversal ordering instead of a
		// Camera::DrawCallback running outside it.
		m_framebufferDrawable->getOrCreateStateSet()->setRenderBinDetails(10, "RenderBin");

		osg::ref_ptr<osg::Geode> producerGeode = new osg::Geode();

		producerGeode->setCullingActive(false);
		producerGeode->addDrawable(m_framebufferDrawable.get());

		addChild(producerGeode.get());

		return;
	}

	m_renderDrawable = new TextureRenderDrawable(rivPath, width, height);

	m_renderDrawable->setDrawMode(m_drawMode);
	m_renderDrawable->getOrCreateStateSet()->setRenderBinDetails(0, "RenderBin");

	osg::ref_ptr<osg::Geode> producerGeode = new osg::Geode();

	producerGeode->setCullingActive(false);
	producerGeode->addDrawable(m_renderDrawable.get());

	m_displayGeode = new osg::Geode();
	m_displayGeode->addDrawable(makeDisplayQuad(m_renderDrawable->getTexture()).get());

	addChild(producerGeode.get());
	addChild(m_displayGeode.get());
}

Scene::TextureRenderDrawable::TextureRenderDrawable(
	const std::string& rivPath,
	unsigned int width,
	unsigned int height
):
m_rivPath(rivPath),
m_width(width),
m_height(height) {
	// This Drawable produces no visible geometry of its own. It exists only
	// to update the texture before the display quad draws, so it must never
	// be frustum-culled away.
	setCullingActive(false);
	setUseDisplayList(false);
	setUseVertexBufferObjects(false);

	m_texture = makeRiveTexture(width, height);
	m_impl = std::make_unique<Impl>(rivPath, width, height);
}

Scene::TextureRenderDrawable::TextureRenderDrawable(const TextureRenderDrawable& drawable, const osg::CopyOp& copyop):
osg::Drawable(drawable, copyop),
m_rivPath(drawable.m_rivPath),
m_width(drawable.m_width),
m_height(drawable.m_height),
m_drawMode(drawable.m_drawMode) {
	setCullingActive(false);
	setUseDisplayList(false);
	setUseVertexBufferObjects(false);

	// Deliberately not a deep copy: m_impl owns live GL resources (a Rive
	// RenderContext, its own GL texture, an open .riv File), which cannot be
	// meaningfully duplicated. A "copy" of a Scene is a fresh, independent
	// instance configured the same way (same .riv path/dimensions/draw
	// mode), not a shared or cloned Rive renderer.
	if(!m_rivPath.empty()) {
		m_texture = makeRiveTexture(m_width, m_height);
		m_impl = std::make_unique<Impl>(m_rivPath, m_width, m_height);
	}
}

Scene::TextureRenderDrawable::~TextureRenderDrawable() = default;

void Scene::TextureRenderDrawable::setDrawMode(DrawMode mode) { m_drawMode = mode; }

DrawMode Scene::TextureRenderDrawable::getDrawMode() const { return m_drawMode; }

osg::Texture2D* Scene::TextureRenderDrawable::getTexture() { return m_texture.get(); }

const osg::Texture2D* Scene::TextureRenderDrawable::getTexture() const { return m_texture.get(); }

void Scene::TextureRenderDrawable::drawImplementation(osg::RenderInfo& renderInfo) const {
	if(!m_impl || !m_texture) return;

	osg::State* state = renderInfo.getState();

	const osg::FrameStamp* frameStamp = state->getFrameStamp();
	double simTime = frameStamp ? frameStamp->getSimulationTime() : 0.0;
	// Sentinel-initialized so frame 0 computes dt = 0 rather than a bogus
	// large jump from simTime - 0.
	double dt = (m_lastSimTime >= 0.0) ? (simTime - m_lastSimTime) : 0.0;
	m_lastSimTime = simTime;

	m_impl->m_renderer.render(
		renderInfo,
		*m_texture,
		static_cast<float>(dt),
		m_drawMode
	);
	// Nothing else. No forceOSGMainPassState, no raw glEnable/glDisable/
	// glBlendFunc. The next Drawable in the RenderBin gets its own StateSet
	// applied via osg::State's normal per-leaf path in RenderLeaf::render(),
	// which reliably re-issues whatever GL state it needs — because it's a
	// real nested apply(), not a standalone one.
}

osg::BoundingBox Scene::TextureRenderDrawable::computeBoundingBox() const {
	// Deliberately invalid/empty: this Drawable has no visible geometry of
	// its own (it only writes into a texture sampled elsewhere), so it must
	// not contribute to the scene's bounding sphere -- an arbitrary non-empty
	// box here would skew camera auto-framing (e.g. TrackballManipulator's
	// home position) for whatever actual geometry samples the texture.
	return osg::BoundingBox();
}

Scene::FramebufferRenderDrawable::FramebufferRenderDrawable(
	const std::string& rivPath,
	unsigned int width,
	unsigned int height
):
m_rivPath(rivPath),
m_width(width),
m_height(height) {
	// Never frustum-culled away, same reasoning as RenderDrawable: this
	// Drawable's visible effect (compositing into the framebuffer) has
	// nothing to do with where it sits in the 3D scene graph.
	setCullingActive(false);
	setUseDisplayList(false);
	setUseVertexBufferObjects(false);

	m_impl = std::make_unique<Impl>(rivPath, width, height);
}

Scene::FramebufferRenderDrawable::FramebufferRenderDrawable(
	const FramebufferRenderDrawable& drawable,
	const osg::CopyOp& copyop
):
osg::Drawable(drawable, copyop),
m_rivPath(drawable.m_rivPath),
m_width(drawable.m_width),
m_height(drawable.m_height),
m_drawMode(drawable.m_drawMode) {
	setCullingActive(false);
	setUseDisplayList(false);
	setUseVertexBufferObjects(false);

	// See RenderDrawable's copy constructor: same reasoning, not a deep copy
	// of live Rive/GL resources.
	if(!m_rivPath.empty()) m_impl = std::make_unique<Impl>(m_rivPath, m_width, m_height);
}

Scene::FramebufferRenderDrawable::~FramebufferRenderDrawable() = default;

void Scene::FramebufferRenderDrawable::setDrawMode(DrawMode mode) { m_drawMode = mode; }

DrawMode Scene::FramebufferRenderDrawable::getDrawMode() const { return m_drawMode; }

void Scene::FramebufferRenderDrawable::drawImplementation(osg::RenderInfo& renderInfo) const {
	if(!m_impl) return;

	osg::State* state = renderInfo.getState();

	const osg::FrameStamp* frameStamp = state->getFrameStamp();
	double simTime = frameStamp ? frameStamp->getSimulationTime() : 0.0;
	double dt = (m_lastSimTime >= 0.0) ? (simTime - m_lastSimTime) : 0.0;
	m_lastSimTime = simTime;

	m_impl->m_renderer.render(renderInfo, static_cast<float>(dt), m_drawMode);
}

osg::BoundingBox Scene::FramebufferRenderDrawable::computeBoundingBox() const {
	// Deliberately invalid/empty -- see RenderDrawable::computeBoundingBox().
	return osg::BoundingBox();
}

}
