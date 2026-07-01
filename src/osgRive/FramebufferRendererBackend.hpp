#pragma once

#include <osgRive/TextureRenderer>

#include <cstdint>
#include <memory>
#include <string>

namespace osgRive {

// OSG-agnostic Rive backend for framebuffer/back-buffer compositing mode:
// Rive renders directly into whatever GL framebuffer is currently bound
// (typically OSG's own back buffer), preserving its existing contents,
// instead of into an owned offscreen texture. Only a raw GL framebuffer id
// crosses this boundary -- mirrors TextureRendererBackend's shape.
class FramebufferRendererBackend {
public:
	FramebufferRendererBackend(std::string rivPath, uint32_t width, uint32_t height);
	~FramebufferRendererBackend();

	FramebufferRendererBackend(const FramebufferRendererBackend&) = delete;
	FramebufferRendererBackend& operator=(const FramebufferRendererBackend&) = delete;

	uint32_t width() const;
	uint32_t height() const;

	void renderToCurrentFramebuffer(
		uint32_t drawFramebufferID,
		float elapsedSeconds,
		DrawMode drawMode
	);

private:
	class Impl;

	std::unique_ptr<Impl> m_impl;
};

}
