#pragma once

#include <osg/GL>
#include <osg/State>
#include <osg/StateAttribute>
#include <osg/Viewport>
#include <osg/ref_ptr>

namespace osgRive
{

// Rive's own GL calls change GL_VIEWPORT/GL_CULL_FACE/GL_SCISSOR_TEST and
// GL_ACTIVE_TEXTURE/unit-0's texture binding behind OSG's back (outside any
// osg::StateAttribute/StateSet apply). This captures what OSG's state cache
// currently believes is applied so it can be restored through OSG's own
// public State API afterward -- no raw gl* calls needed. Shared by
// TextureRenderer.cpp and FramebufferRenderer.cpp; neither backend
// (TextureRendererBackend/FramebufferRendererBackend) needs any GL
// save/restore of its own, since both are deliberately OSG-agnostic (only
// raw GL ids cross that boundary).
//
// Unit 0's texture attribute is captured generically via
// getLastAppliedTextureAttribute() rather than requiring a caller-supplied
// texture pointer: in texture mode, the caller has already applied its
// target texture to unit 0 before constructing this, so the cache already
// holds that pointer; in framebuffer mode there is no OSG texture involved
// at all, and capturing "whatever was there before" (possibly null) is
// exactly the correct restore target either way.
class ScopedOSGStateRestore
{
public:
	explicit ScopedOSGStateRestore(osg::State* state):
	m_state(state),
	m_cullFace(state->getLastAppliedMode(GL_CULL_FACE)),
	m_scissorTest(state->getLastAppliedMode(GL_SCISSOR_TEST)),
	m_viewport(state->getCurrentViewport()
		? new osg::Viewport(*state->getCurrentViewport())
		: nullptr
	),
	m_texture0(state->getLastAppliedTextureAttribute(0, osg::StateAttribute::TEXTURE)) {}

	~ScopedOSGStateRestore() {
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
		if(m_viewport.valid()) m_state->applyAttribute(m_viewport.get());

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

		if(m_texture0.valid()) m_state->applyTextureAttribute(0, m_texture0.get());
		// else: nothing was bound on unit 0 before Rive ran. Real GL is
		// already unit-0-unbound after Rive's own unbindGLInternalResources,
		// which matches "nothing applied" -- no further action needed.
	}

private:
	osg::State* m_state;
	bool m_cullFace;
	bool m_scissorTest;
	osg::ref_ptr<osg::Viewport> m_viewport;
	osg::ref_ptr<const osg::StateAttribute> m_texture0;
};

}
