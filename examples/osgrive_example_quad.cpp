#include <osgRive/Scene>

#include <osg/BlendFunc>
#include <osg/ColorMask>
#include <osg/Geode>
#include <osg/Geometry>
#include <osg/GL>
#include <osg/Group>
#include <osg/Program>
#include <osg/Shader>
#include <osg/StateSet>
#include <osg/Texture2D>
#include <osg/Vec3>
#include <osgGA/TrackballManipulator>
#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>

#include <cstdint>
#include <string>

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
    // Required: Rive's own internal GLState tracks glColorMask/glDepthMask/
    // glStencilMask separately from anything ScopedGLRestore captures, and
    // may leave color writes disabled after its last internal render pass.
    // OSG only re-applies write masks per-leaf if a StateSet explicitly
    // declares this attribute -- without it, nothing resets color writes
    // before this quad draws.
    stateSet->setAttributeAndModes(new osg::ColorMask(true, true, true, true));
    // Draw after the osgRive::Scene that renders into this texture -- see
    // the matching setRenderBinDetails(0, ...) call in main() below.
    stateSet->setRenderBinDetails(1, "RenderBin");

    auto* program = new osg::Program;
    program->addShader(new osg::Shader(osg::Shader::VERTEX, kQuadVert));
    program->addShader(new osg::Shader(osg::Shader::FRAGMENT, kQuadFrag));
    stateSet->setAttributeAndModes(program);

    osg::ref_ptr<osg::Geode> geode = new osg::Geode;
    geode->addDrawable(quad.get());
    return geode;
}
} // namespace

int main(int argc, char** argv)
{
    const std::string rivPath =
        argc > 1
            ? argv[1]
            : "/home/cubicool/dev/rive-runtime/renderer/webgpu_player/"
              "rivs/towersDemo.riv";

    osg::ref_ptr<osgRive::Scene> riveScene =
        new osgRive::Scene(rivPath, kRiveTextureWidth, kRiveTextureHeight);
    // Draw before the quad that samples this Scene's texture -- see the
    // matching setRenderBinDetails(1, ...) call in makeTexturedQuad() above.
    riveScene->getOrCreateStateSet()->setRenderBinDetails(0, "RenderBin");

    // osg::Drawable does not override accept(NodeVisitor&) itself (it
    // inherits Node's, which dispatches to the generic apply(Node&) rather
    // than apply(Drawable&)) -- a bare Drawable added directly to a Group is
    // never recognized as drawable content by CullVisitor. A Geode is the
    // universal, correct way to host a Drawable in the scene graph.
    osg::ref_ptr<osg::Geode> riveGeode = new osg::Geode;
    riveGeode->addDrawable(riveScene.get());

    osg::ref_ptr<osg::Group> root = new osg::Group;
    root->addChild(riveGeode.get());
    root->addChild(makeTexturedQuad(riveScene->getTexture()));

    osgViewer::Viewer viewer;
    // SingleThreaded is mandatory -- multi-threaded causes an NVIDIA driver
    // crash when OSG's VAO state is invalidated across threads.
    viewer.setThreadingModel(osgViewer::Viewer::SingleThreaded);
    viewer.getCamera()->setClearColor(osg::Vec4(0.2f, 0.2f, 0.2f, 1.0f));
    viewer.setSceneData(root.get());
    viewer.addEventHandler(new osgViewer::StatsHandler());
    viewer.setCameraManipulator(new osgGA::TrackballManipulator());
    viewer.setUpViewInWindow(50, 50, kWindowWidth, kWindowHeight);
    return viewer.run();
}
