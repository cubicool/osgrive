#include "FramebufferRendererBackend.hpp"

#include "RiveGLSupport.hpp"

#include "rive/animation/state_machine_instance.hpp"
#include "rive/artboard.hpp"
#include "rive/file.hpp"
#include "rive/renderer/gl/render_context_gl_impl.hpp"
#include "rive/renderer/gl/render_target_gl.hpp"
#include "rive/renderer.hpp"
#include "rive/renderer/rive_renderer.hpp"
#include "rive/renderer/texture.hpp"
#include "rive/scene.hpp"

#include <stdexcept>
#include <utility>

namespace osgRive {

class FramebufferRendererBackend::Impl {
public:
	Impl(std::string rivPath, uint32_t width, uint32_t height):
	m_rivPath(std::move(rivPath)), m_width(width), m_height(height) {}

	uint32_t width() const { return m_width; }
	uint32_t height() const { return m_height; }

	void renderToCurrentFramebuffer(
		uint32_t drawFramebufferID,
		float elapsedSeconds,
		DrawMode drawMode
	) {
		ensureRiveGLLoaded();
		ensureInitialized();

		// Reuse the previous frame's FramebufferRenderTargetGL as long as
		// nothing it depends on has changed -- constructing a fresh one
		// every frame was flagged as an unnecessary cost in earlier
		// benchmarking (see CODEX.md). Only the FBO id can realistically
		// change frame to frame (e.g. the default framebuffer swapping, or
		// a resize); width/height come from this backend's construction
		// and sampleCount is always 0 here.
		if(!m_framebufferTarget || m_lastFramebufferID != drawFramebufferID) {
			m_framebufferTarget = rive::make_rcp<rive::gpu::FramebufferRenderTargetGL>(
				m_width,
				m_height,
				static_cast<GLuint>(drawFramebufferID),
				0
			);

			m_lastFramebufferID = drawFramebufferID;
		}

		// preserveRenderTarget: composite on top of whatever OSG already
		// rendered into this framebuffer this frame, instead of clearing it.
		renderToTarget(
			elapsedSeconds,
			m_framebufferTarget.get(),
			drawMode,
			rive::gpu::LoadAction::preserveRenderTarget,
			0x00000000u
		);
	}

private:
	void renderToTarget(
		float elapsedSeconds,
		rive::gpu::RenderTarget* target,
		DrawMode drawMode,
		rive::gpu::LoadAction loadAction,
		uint32_t clearColor
	) {
#ifdef OSGRIVE_DEBUG_GL_ERRORS
		drainGLErrors("before Rive render");
#endif

		auto* glImpl = m_renderContext->static_impl_cast<rive::gpu::RenderContextGLImpl>();

		glImpl->invalidateGLState();

		rive::gpu::RenderContext::FrameDescriptor frameDescriptor = {
			.renderTargetWidth = m_width,
			.renderTargetHeight = m_height,
			.loadAction = loadAction,
			.clearColor = clearColor,
		};

		m_renderContext->beginFrame(frameDescriptor);

		if(m_scene && drawMode != DrawMode::NONE) m_scene->advanceAndApply(elapsedSeconds);
		else if (drawMode != DrawMode::NONE) m_artboard->advance(elapsedSeconds);

		rive::RiveRenderer renderer(m_renderContext.get());

		renderer.save();
		renderer.transform(rive::computeAlignment(
			rive::Fit::contain,
			rive::Alignment::center,
			rive::AABB(
				0.0f,
				0.0f,
				static_cast<float>(m_width),
				static_cast<float>(m_height)
			),
			m_artboard->bounds())
		);

		switch(drawMode) {
			case DrawMode::SCENE:
				if(m_scene) m_scene->draw(&renderer);
				else m_artboard->draw(&renderer);
				break;

			case DrawMode::ARTBOARD:
				m_artboard->draw(&renderer);
				break;

			case DrawMode::ARTBOARD_INTERNAL:
				m_artboard->drawInternal(&renderer);
				break;

			case DrawMode::NONE:
				break;
		}

		renderer.restore();

		m_renderContext->flush({.renderTarget = target});

		glImpl->unbindGLInternalResources();

#ifdef OSGRIVE_DEBUG_GL_ERRORS
		drainGLErrors("after Rive render");
#endif
	}

	void ensureInitialized() {
		if(m_renderContext) return;

		ensureRiveGLLoaded();

		m_renderContext = rive::gpu::RenderContextGLImpl::MakeContext();

		if(!m_renderContext) throw std::runtime_error(
			"Rive GL RenderContext creation failed; OpenGL 4.2+ is "
			"required by this backend"
		);

		auto rivBytes = readBinaryFile(m_rivPath);

		m_file = rive::File::import(rivBytes, m_renderContext.get());

		if(!m_file) throw std::runtime_error("Rive import failed for " + m_rivPath);

		m_artboard = m_file->artboardDefault();

		if(!m_artboard) throw std::runtime_error("Rive file has no default artboard");

		m_scene = m_artboard->defaultStateMachine();

		if(!m_scene && m_artboard->animationCount() > 0) m_scene = m_artboard->animationAt(0);

		if(m_scene) m_scene->advanceAndApply(0.0f);
		else m_artboard->advance(0.0f);
	}

	std::string m_rivPath;
	uint32_t m_width = 0;
	uint32_t m_height = 0;

	std::unique_ptr<rive::gpu::RenderContext> m_renderContext;
	rive::rcp<rive::gpu::FramebufferRenderTargetGL> m_framebufferTarget;
	uint32_t m_lastFramebufferID = 0;
	rive::rcp<rive::File> m_file;
	std::unique_ptr<rive::ArtboardInstance> m_artboard;
	std::unique_ptr<rive::Scene> m_scene;
};

FramebufferRendererBackend::FramebufferRendererBackend(
	std::string rivPath,
	uint32_t width,
	uint32_t height
):
m_impl(std::make_unique<Impl>(std::move(rivPath), width, height)) {}

FramebufferRendererBackend::~FramebufferRendererBackend() = default;

uint32_t FramebufferRendererBackend::width() const { return m_impl->width(); }
uint32_t FramebufferRendererBackend::height() const { return m_impl->height(); }

void FramebufferRendererBackend::renderToCurrentFramebuffer(
	uint32_t drawFramebufferID,
	float elapsedSeconds,
	DrawMode drawMode)
{
	m_impl->renderToCurrentFramebuffer(drawFramebufferID, elapsedSeconds, drawMode);
}

}
