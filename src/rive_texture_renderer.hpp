#pragma once

#include <cstdint>
#include <memory>
#include <string>

enum class RiveDrawMode
{
    scene,
    artboard,
    artboardInternal,
    none,
};

class RiveTextureRenderer
{
public:
    RiveTextureRenderer(std::string rivPath, uint32_t width, uint32_t height);
    ~RiveTextureRenderer();

    RiveTextureRenderer(const RiveTextureRenderer&) = delete;
    RiveTextureRenderer& operator=(const RiveTextureRenderer&) = delete;

    uint32_t textureID() const;
    uint32_t width() const;
    uint32_t height() const;

    void render(float elapsedSeconds);
    void renderToCurrentFramebuffer(float elapsedSeconds);
    void renderToCurrentFramebuffer(float elapsedSeconds, RiveDrawMode drawMode);
    void renderToTexture(uint32_t textureID, float elapsedSeconds);
    void renderToTexture(uint32_t textureID,
                         float elapsedSeconds,
                         RiveDrawMode drawMode);
    void clearTexture(uint32_t textureID);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
