#include <osgRive/Scene>

#include <osg/MatrixTransform>
#include <osgGA/TrackballManipulator>
#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>

#ifdef OSGRIVE_ENABLE_OSGDEBUG_TRACE
#include <osgDebug.hpp>
#endif

#include <cstdint>
#include <string>

namespace
{
constexpr uint32_t kWindowWidth = 800;
constexpr uint32_t kWindowHeight = 600;
constexpr uint32_t kRiveTextureWidth = 512;
constexpr uint32_t kRiveTextureHeight = 512;
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

    // The display quad is built flat in the XY plane (normal +Z, quad "up"
    // along +Y). OSG's default trackball home view is set up for a Z-up
    // world, so without this the quad starts out edge-on to the camera
    // instead of facing it. Rotate +90 degrees about X to stand it upright,
    // facing the camera, with the quad's +Y ("up") mapped to world +Z.
    osg::ref_ptr<osg::MatrixTransform> root = new osg::MatrixTransform;
    root->setMatrix(osg::Matrix::rotate(osg::PI_2, osg::Vec3(1.0, 0.0, 0.0)));
    root->addChild(riveScene.get());

    osgViewer::Viewer viewer;
    // SingleThreaded is mandatory -- multi-threaded causes an NVIDIA driver
    // crash when OSG's VAO state is invalidated across threads.
    viewer.setThreadingModel(osgViewer::Viewer::SingleThreaded);
    viewer.getCamera()->setClearColor(osg::Vec4(0.2f, 0.2f, 0.2f, 1.0f));
    viewer.setSceneData(root.get());
    viewer.addEventHandler(new osgViewer::StatsHandler());
    viewer.setCameraManipulator(new osgGA::TrackballManipulator());
    viewer.setUpViewInWindow(50, 50, kWindowWidth, kWindowHeight);
#ifdef OSGRIVE_ENABLE_OSGDEBUG_TRACE
    viewer.setRealizeOperation(new osgDebug::GraphicsOperation());
#endif
    return viewer.run();
}
