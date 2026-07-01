#include <osgRive/Scene>

#include <osg/Group>
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

    osg::ref_ptr<osg::Group> root = new osg::Group;
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
    return viewer.run();
}
