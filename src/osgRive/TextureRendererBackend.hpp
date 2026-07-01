#pragma once

#include <osgRive/TextureRenderer>

#include <cstdint>
#include <memory>
#include <string>

namespace osgRive {

class TextureRendererBackend {
public:
	TextureRendererBackend(std::string rivPath, uint32_t width, uint32_t height);
	~TextureRendererBackend();

	TextureRendererBackend(const TextureRendererBackend&) = delete;
	TextureRendererBackend& operator=(const TextureRendererBackend&) = delete;

	uint32_t width() const;
	uint32_t height() const;

	void renderToTexture(uint32_t textureID, float elapsedSeconds, DrawMode drawMode);

private:
	class Impl;

	std::unique_ptr<Impl> m_impl;
};

}
