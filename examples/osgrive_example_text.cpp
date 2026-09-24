// osgrive-example-text <font.ttf> [text] [--size <px/em>] [--framebuffer]
//
// One line of text drawn through Rive's own font stack (osgRive::makeTextDraw(): HarfBuzz shaping
// and outlines, Rive's renderer) - no .riv file involved.
//
// Default (RenderTarget::TEXTURE): renders into a TEXTURE_W x TEXTURE_H texture displayed on a quad
// with the texture's aspect ratio, placed in 3D and driven by a trackball.
//
// --framebuffer (RenderTarget::FRAMEBUFFER): renders straight into the window's framebuffer in
// window pixels, screen-space - the text is drawn at its true output resolution, not resampled
// from a texture.
//
// --size sets pixels per em in either mode (DEFAULT 128).

#include <osgRive/Scene>
#include <osgRive/Text>

#include <osg/MatrixTransform>
#include <osgGA/TrackballManipulator>
#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
	constexpr uint32_t kWindowWidth = 1024;
	constexpr uint32_t kWindowHeight = 512;
	constexpr uint32_t kTextureWidth = 1024;
	constexpr uint32_t kTextureHeight = 256;

	// Left edge of the pen, in target pixels.
	constexpr float kMargin = 32.0f;

	// 0xAARRGGBB
	constexpr uint32_t kTextColor = 0xFFF2F2F2;
}

int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);
	osgViewer::Viewer viewer(args);

	bool framebufferMode = false;
	float pixelsPerEm = 128.0f;
	std::string fontPath;
	std::string text = "Hello, Rive!";

	for(int i = 1; i < argc; i++) {
		const std::string arg = argv[i];

		if(arg == "--framebuffer") framebufferMode = true;

		else if(arg == "--size" && i + 1 < argc) pixelsPerEm = std::strtof(argv[++i], nullptr);

		else if(fontPath.empty()) fontPath = arg;

		else text = arg;
	}

	if(fontPath.empty() || pixelsPerEm <= 0.0f) {
		std::cerr
			<< "usage: osgrive-example-text <font.ttf> [text] [--size <px/em>] [--framebuffer]"
			<< std::endl
		;

		return 1;
	}

	const uint32_t targetWidth = framebufferMode ? kWindowWidth : kTextureWidth;
	const uint32_t targetHeight = framebufferMode ? kWindowHeight : kTextureHeight;

	// Rive target pixels are y-down; put the baseline 3/4 of the way down the target, which leaves
	// room for ascenders above and descenders below at any sane --size.
	osg::ref_ptr<osgRive::Scene> riveScene;

	try {
		riveScene = new osgRive::Scene(
			osgRive::makeTextDraw(
				fontPath,
				text,
				pixelsPerEm,
				kMargin,
				static_cast<float>(targetHeight) * 0.75f,
				kTextColor
			),
			targetWidth,
			targetHeight,
			framebufferMode
				? osgRive::RenderTarget::FRAMEBUFFER
				: osgRive::RenderTarget::TEXTURE
		);
	}

	catch(const std::exception& e) {
		std::cerr << "osgrive-example-text: " << e.what() << std::endl;

		return 1;
	}

	// The display quad (TEXTURE only) is a unit square in the XY plane; scale X to the texture's
	// aspect ratio, then stand it upright for the trackball's Z-up home view (same as
	// osgrive-example-quad). Harmless in FRAMEBUFFER mode, which ignores transforms.
	osg::ref_ptr<osg::MatrixTransform> root = new osg::MatrixTransform();

	root->setMatrix(
		osg::Matrix::scale(
			static_cast<double>(kTextureWidth) / static_cast<double>(kTextureHeight),
			1.0,
			1.0
		) *
		osg::Matrix::rotate(osg::PI_2, osg::Vec3(1.0, 0.0, 0.0))
	);

	root->addChild(riveScene.get());

	// SingleThreaded is mandatory -- multi-threaded causes an NVIDIA driver
	// crash when OSG's VAO state is invalidated across threads.
	viewer.setThreadingModel(osgViewer::Viewer::SingleThreaded);
	viewer.getCamera()->setClearColor(osg::Vec4(0.1f, 0.1f, 0.1f, 1.0f));
	viewer.setSceneData(root.get());
	viewer.addEventHandler(new osgViewer::StatsHandler());
	viewer.setCameraManipulator(new osgGA::TrackballManipulator());
	viewer.setUpViewInWindow(50, 50, kWindowWidth, kWindowHeight);

	return viewer.run();
}
