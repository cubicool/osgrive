#include "rive_texture_renderer.hpp"

#include <osg/BlendFunc>
#include <osg/Camera>
#include <osg/Geode>
#include <osg/Geometry>
#include <osg/Group>
#include <osg/GL>
#include <osg/Image>
#include <osg/Program>
#include <osg/Shader>
#include <osg/State>
#include <osg/StateSet>
#include <osg/Texture>
#include <osg/Texture2D>
#include <osg/Vec3>
#include <osg/Viewport>
#include <osgGA/TrackballManipulator>
#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>

namespace
{
constexpr uint32_t kWindowWidth = 800;
constexpr uint32_t kWindowHeight = 600;
constexpr uint32_t kRiveTextureWidth = 512;
constexpr uint32_t kRiveTextureHeight = 512;

static const char* kQuadVert = R"(
#version 330 core

in vec4 osg_Vertex;
in vec2 osg_MultiTexCoord0;

uniform mat4 osg_ModelViewProjectionMatrix;

out vec2 uv;

void main()
{
    uv = osg_MultiTexCoord0;
    gl_Position = osg_ModelViewProjectionMatrix * osg_Vertex;
}
)";

static const char* kQuadFrag = R"(
#version 330 core

uniform sampler2D colorTex;

in vec2 uv;

out vec4 color;

void main()
{
    color = texture(colorTex, uv);
}
)";

void drainGLErrors(const char* label)
{
#ifdef OSGRIVE_DEBUG_GL_ERRORS
    bool printedHeader = false;
    for (;;)
    {
        GLenum err = glGetError();
        if (err == GL_NO_ERROR)
        {
            return;
        }

        if (!printedHeader)
        {
            OSG_NOTICE << "GL errors drained at " << label << ":" << std::endl;
            printedHeader = true;
        }
        OSG_NOTICE << "  0x" << std::hex << static_cast<unsigned int>(err)
                   << std::dec << std::endl;
    }
#else
    (void)label;
#endif
}

void forceOSGMainPassState(osg::State* state)
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    GLenum backBuffer = GL_BACK;
    glDrawBuffers(1, &backBuffer);
    glReadBuffer(GL_BACK);

    const osg::Viewport* viewport =
        state != nullptr ? state->getCurrentViewport() : nullptr;
    if (viewport != nullptr)
    {
        glViewport(static_cast<GLint>(viewport->x()),
                   static_cast<GLint>(viewport->y()),
                   static_cast<GLsizei>(viewport->width()),
                   static_cast<GLsizei>(viewport->height()));
    }

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glStencilMask(0xff);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glUseProgram(0);

    if (state != nullptr)
    {
        // Dirty first so OSG's mode/attribute cache no longer matches
        // whatever it last believed — otherwise the forced apply() below
        // could see a cache entry that already (coincidentally) says
        // GL_BLEND=ON and skip re-issuing the real GL call, leaving actual
        // GL state out of sync with what raw calls elsewhere (Rive's
        // ScopedGLRestore) may have left it as.
        state->dirtyAllModes();
        state->dirtyAllAttributes();
        state->dirtyAllVertexArrays();

        // Apply the blend function via a real osg::BlendFunc/StateSet so OSG's
        // attribute cache stays authoritative for it (confirmed via apitrace:
        // this reliably reissues glBlendFunc). The GL_BLEND *mode* toggle
        // itself is a separate, unreliable case here — apitrace showed
        // dirtyAllModes() + this same apply() not reissuing glEnable(GL_BLEND)
        // even though the real GL state was left disabled by Rive's
        // ScopedGLRestore, so that one specific toggle is forced directly.
        static osg::ref_ptr<osg::StateSet> sBlendFuncStateSet = [] {
            osg::ref_ptr<osg::StateSet> stateSet = new osg::StateSet;
            stateSet->setAttributeAndModes(
                new osg::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA),
                osg::StateAttribute::ON);
            return stateSet;
        }();
        state->apply(sBlendFuncStateSet.get());
        glEnable(GL_BLEND);
    }
}

class RiveTextureUpdateCallback : public osg::Camera::DrawCallback
{
public:
    RiveTextureUpdateCallback(std::string rivPath,
                              osg::Texture2D* texture,
                              uint32_t width,
                              uint32_t height,
                              bool enabled,
                              bool clearOnly,
                              RiveDrawMode drawMode) :
        m_texture(texture),
        m_enabled(enabled),
        m_clearOnly(clearOnly),
        m_drawMode(drawMode),
        m_rive(std::move(rivPath), width, height)
    {}

    void operator()(osg::RenderInfo& renderInfo) const override
    {
        if (!m_enabled)
        {
            return;
        }

        osg::State* state = renderInfo.getState();
        if (state == nullptr || !m_texture.valid())
        {
            return;
        }

        drainGLErrors("pre-draw entry");

        state->applyTextureAttribute(0, m_texture.get());
        drainGLErrors("after applyTextureAttribute");

        osg::Texture::TextureObject* textureObject =
            m_texture->getTextureObject(state->getContextID());
        if (textureObject == nullptr)
        {
            std::fprintf(stderr,
                         "[osgRive] ERROR: texture object null for contextID=%u"
                         " — Rive skipped\n",
                         state->getContextID());
            return;
        }

        auto* self = const_cast<RiveTextureUpdateCallback*>(this);

        if (m_frameCount < 3)
        {
            GLuint tid = textureObject->id();
            std::fprintf(stderr,
                         "[osgRive] frame %u: texture id=%u contextID=%u\n",
                         m_frameCount, tid, state->getContextID());

            // FBO completeness probe using the OSG texture
            GLuint probeFBO = 0;
            glGenFramebuffers(1, &probeFBO);
            glBindFramebuffer(GL_FRAMEBUFFER, probeFBO);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D, tid, 0);
            GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &probeFBO);
            drainGLErrors("after FBO probe");

            const char* statusStr = (status == GL_FRAMEBUFFER_COMPLETE)
                ? "COMPLETE"
                : (status == GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT)
                    ? "INCOMPLETE_ATTACHMENT"
                    : (status == GL_FRAMEBUFFER_UNSUPPORTED) ? "UNSUPPORTED"
                                                              : "OTHER";
            std::fprintf(stderr,
                         "[osgRive] frame %u: FBO status=0x%x (%s)\n",
                         m_frameCount, status, statusStr);
            ++self->m_frameCount;
        }

        if (m_clearOnly)
        {
            self->m_rive.clearTexture(textureObject->id());
        }
        else
        {
            self->m_rive.renderToTexture(textureObject->id(),
                                         1.0f / 60.0f,
                                         m_drawMode);
        }

        drainGLErrors("after Rive render in OSG");
        forceOSGMainPassState(state);
        drainGLErrors("after OSG state reset");
    }

private:
    osg::ref_ptr<osg::Texture2D> m_texture;
    bool m_enabled = true;
    bool m_clearOnly = false;
    RiveDrawMode m_drawMode = RiveDrawMode::scene;
    RiveTextureRenderer m_rive;
    mutable uint32_t m_frameCount = 0;
};

// Post-draw callback: Rive renders directly into the current framebuffer after
// OSG has finished drawing its scene. No texture intermediary — Rive composites
// on top of whatever OSG left in the back buffer.
class RiveFramebufferCallback : public osg::Camera::DrawCallback
{
public:
    RiveFramebufferCallback(std::string rivPath,
                            uint32_t width,
                            uint32_t height,
                            RiveDrawMode drawMode) :
        m_rive(std::move(rivPath), width, height),
        m_drawMode(drawMode)
    {}

    void operator()(osg::RenderInfo&) const override
    {
        auto* self = const_cast<RiveFramebufferCallback*>(this);
        // OSG has finished drawing; render Rive on top of the back buffer.
        // ScopedGLRestore inside renderToCurrentFramebuffer handles all GL cleanup.
        // No OSG dirty calls needed — OSG won't draw again before the swap.
        self->m_rive.renderToCurrentFramebuffer(1.0f / 60.0f, m_drawMode);
    }

private:
    RiveTextureRenderer m_rive;
    RiveDrawMode m_drawMode;
};

osg::ref_ptr<osg::Texture2D> makeRiveTexture()
{
    osg::ref_ptr<osg::Image> image = new osg::Image;
    image->allocateImage(static_cast<int>(kRiveTextureWidth),
                         static_cast<int>(kRiveTextureHeight),
                         1,
                         GL_RGBA,
                         GL_UNSIGNED_BYTE);

    unsigned char* pixels = image->data();
    for (uint32_t y = 0; y < kRiveTextureHeight; ++y)
    {
        for (uint32_t x = 0; x < kRiveTextureWidth; ++x)
        {
            size_t i = static_cast<size_t>((y * kRiveTextureWidth + x) * 4);
            pixels[i + 0] = 255;
            pixels[i + 1] = x < kRiveTextureWidth / 2 ? 0 : 255;
            pixels[i + 2] = y < kRiveTextureHeight / 2 ? 0 : 255;
            pixels[i + 3] = 255;
        }
    }

    osg::ref_ptr<osg::Texture2D> texture = new osg::Texture2D;
    texture->setImage(image.get());
    texture->setTextureSize(static_cast<int>(kRiveTextureWidth),
                            static_cast<int>(kRiveTextureHeight));
    // GL_RGBA8 is required: Rive's PLS renderer calls glBindImageTexture with
    // GL_RGBA8, which needs a sized (not unsized) internal format. GL_RGBA also
    // fails glCheckFramebufferStatus in strict core profiles.
    texture->setInternalFormat(GL_RGBA8);
    texture->setSourceFormat(GL_RGBA);
    texture->setSourceType(GL_UNSIGNED_BYTE);
    texture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
    texture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
    texture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
    texture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
    return texture;
}

osg::ref_ptr<osg::Geode> makeTexturedQuad(osg::Texture2D* texture)
{
    osg::ref_ptr<osg::Geometry> quad = osg::createTexturedQuadGeometry(
        osg::Vec3(0.0f, 0.0f, 0.0f),
        osg::Vec3(1.0f, 0.0f, 0.0f),
        osg::Vec3(0.0f, 1.0f, 0.0f));

    quad->setUseDisplayList(false);
    quad->setUseVertexBufferObjects(true);
    auto* stateSet = quad->getOrCreateStateSet();
    stateSet->setTextureAttributeAndModes(0, texture, osg::StateAttribute::ON);
    stateSet->addUniform(new osg::Uniform("colorTex", 0));
    stateSet->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
    stateSet->setMode(GL_BLEND, osg::StateAttribute::ON);
    stateSet->setAttributeAndModes(
        new osg::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

    auto* program = new osg::Program;
    program->addShader(new osg::Shader(osg::Shader::VERTEX, kQuadVert));
    program->addShader(new osg::Shader(osg::Shader::FRAGMENT, kQuadFrag));
    stateSet->setAttributeAndModes(program);

    osg::ref_ptr<osg::Geode> geode = new osg::Geode;
    geode->addDrawable(quad.get());
    return geode;
}

int runWithConsoleFPS(osgViewer::Viewer& viewer)
{
    using Clock = std::chrono::steady_clock;
    constexpr double kReportIntervalSeconds = 5.0;

    uint64_t frames = 0;
    auto start = Clock::now();

    while (!viewer.done())
    {
        viewer.frame();
        ++frames;

        const auto now = Clock::now();
        const double elapsed =
            std::chrono::duration_cast<std::chrono::duration<double>>(
                now - start)
                .count();
        if (elapsed >= kReportIntervalSeconds)
        {
            std::printf("[osgRive monolith] %.3f FPS (%llu frames / %.3fs)\n",
                        static_cast<double>(frames) / elapsed,
                        static_cast<unsigned long long>(frames),
                        elapsed);
            std::fflush(stdout);
            frames = 0;
            start = now;
        }
    }

    return 0;
}
} // namespace

int main(int argc, char** argv)
{
    bool renderRive = true;
    bool riveClearOnly = false;
    bool framebufferMode = false;
    bool showStats = false;
    RiveDrawMode drawMode = RiveDrawMode::scene;
    const char* rivPathArg = nullptr;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--no-rive")
        {
            renderRive = false;
        }
        else if (arg == "--rive-clear-only")
        {
            riveClearOnly = true;
        }
        else if (arg == "--framebuffer")
        {
            framebufferMode = true;
        }
        else if (arg == "--stats")
        {
            showStats = true;
        }
        else if (arg == "--draw-scene")
        {
            drawMode = RiveDrawMode::scene;
        }
        else if (arg == "--draw-artboard")
        {
            drawMode = RiveDrawMode::artboard;
        }
        else if (arg == "--draw-internal")
        {
            drawMode = RiveDrawMode::artboardInternal;
        }
        else if (arg == "--draw-none")
        {
            drawMode = RiveDrawMode::none;
        }
        else
        {
            rivPathArg = argv[i];
        }
    }

    const std::string rivPath =
        rivPathArg != nullptr
            ? rivPathArg
            : "/home/cubicool/dev/rive-runtime/renderer/webgpu_player/"
              "rivs/towersDemo.riv";

    osgViewer::Viewer viewer;
    viewer.setThreadingModel(osgViewer::Viewer::SingleThreaded);
    viewer.getCamera()->setClearColor(osg::Vec4(0.2f, 0.2f, 0.2f, 1.0f));

    if (framebufferMode)
    {
        // Framebuffer mode: Rive renders directly into the back buffer after
        // OSG draws. No texture intermediary — Rive composites on top of the
        // gray background. TrackballManipulator is kept so the modes feel
        // comparable, but Rive content does not respond to camera transforms.
        viewer.setSceneData(new osg::Group);
        if (renderRive)
        {
            viewer.getCamera()->setPostDrawCallback(new RiveFramebufferCallback(
                rivPath,
                kWindowWidth,
                kWindowHeight,
                drawMode));
        }
    }
    else
    {
        // Texture mode: Rive renders into an OSG-owned GL_RGBA8 texture each
        // pre-draw, and OSG samples that texture on a rotating 3D quad.
        osg::ref_ptr<osg::Texture2D> riveTexture = makeRiveTexture();
        osg::ref_ptr<osg::Group> root = new osg::Group;
        root->addChild(makeTexturedQuad(riveTexture.get()));
        viewer.setSceneData(root.get());
        viewer.getCamera()->setPreDrawCallback(new RiveTextureUpdateCallback(
            rivPath,
            riveTexture.get(),
            kRiveTextureWidth,
            kRiveTextureHeight,
            renderRive,
            riveClearOnly,
            drawMode));
    }

    if (showStats)
    {
        viewer.addEventHandler(new osgViewer::StatsHandler());
    }
    viewer.setCameraManipulator(new osgGA::TrackballManipulator());
    viewer.setUpViewInWindow(50, 50, kWindowWidth, kWindowHeight);
    return runWithConsoleFPS(viewer);
}
