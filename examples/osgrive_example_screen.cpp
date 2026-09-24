// osgrive-example-screen <file.riv>
//
// A .riv artboard placed in a 3D world - on the XY plane, centered on the origin, its longest side
// 1 world unit - but drawn by Rive at the WINDOW's resolution, through the camera: every frame a
// cull callback maps artboard coordinates to window pixels and hands that to Scene::setTransform(),
// so Rive re-tessellates for the artboard's true on-screen size. Zoom/rotate with the trackball.
//
// Rive's renderer only takes 2D affine transforms, so the projective mapping is linearized at the
// artboard's center: exact while the artboard faces the camera (any zoom, pan, in-plane rotation);
// under perspective tilt the approximation drifts with distance from the center - Rive's real
// limit, not a bug. The thin outline is the artboard rect drawn by OSG with the TRUE projection,
// so the drift is visible against it.
//
// The window is a fixed size: Rive's target is allocated to match it at startup.

#include <osgRive/Artboard>
#include <osgRive/Scene>

#include <osg/BlendFunc>
#include <osg/Camera>
#include <osg/Geode>
#include <osg/Geometry>
#include <osg/Group>
#include <osg/Program>
#include <osg/Texture2D>
#include <osgGA/TrackballManipulator>
#include <osgUtil/CullVisitor>
#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
	constexpr int kWindowWidth = 1024;
	constexpr int kWindowHeight = 768;

	// Panel-filling quad: positions are already NDC.
	const char* kScreenVert = R"(
#version 330 core

in vec4 osg_Vertex;
in vec2 osg_MultiTexCoord0;

out vec2 uv;

void main() {
	uv = osg_MultiTexCoord0;
	gl_Position = vec4(osg_Vertex.xy, 0.0, 1.0);
}
)";

	const char* kScreenFrag = R"(
#version 330 core

uniform sampler2D riveTexture;

in vec2 uv;

out vec4 color;

void main() {
	color = texture(riveTexture, uv);
}
)";

	const char* kOutlineVert = R"(
#version 330 core

in vec4 osg_Vertex;

uniform mat4 osg_ModelViewProjectionMatrix;

void main() {
	gl_Position = osg_ModelViewProjectionMatrix * osg_Vertex;
}
)";

	const char* kOutlineFrag = R"(
#version 330 core

out vec4 color;

void main() {
	color = vec4(1.0, 0.8, 0.3, 1.0);
}
)";
}

// Artboard (x, y down) <-> world (XY plane, y up): centered on the origin, longest side 1 unit.
struct ArtboardPlacement {
	float centerX = 0.0f;
	float centerY = 0.0f;
	float scale = 1.0f;

	explicit ArtboardPlacement(const osgRive::ArtboardBounds& b):
	centerX((b.minX + b.maxX) * 0.5f),
	centerY((b.minY + b.maxY) * 0.5f),
	scale(1.0f / std::max({b.width(), b.height(), 1e-6f})) {
	}

	osg::Vec3d toWorld(double x, double y) const {
		return osg::Vec3d((x - centerX) * scale, -(y - centerY) * scale, 0.0);
	}
};

// Lives on a Group directly under the main camera, so at cull time the CullVisitor's matrices are
// the main camera's view and projection for THIS frame; the producer stage beneath it then draws
// Rive with the transform computed here, before the main camera samples the result.
struct ScreenTransformCallback: public osg::NodeCallback {
	osg::ref_ptr<osgRive::Scene> scene;
	ArtboardPlacement placement;

	ScreenTransformCallback(osgRive::Scene* s, const ArtboardPlacement& p):
	scene(s),
	placement(p) {
	}

	// Artboard point -> window pixel (origin top-left, y down); false behind the camera.
	bool toPixel(const osg::Matrixd& mvp, double x, double y, osg::Vec2d& pixel) const {
		const osg::Vec3d w = placement.toWorld(x, y);
		const osg::Vec4d clip = osg::Vec4d(w, 1.0) * mvp;

		if(clip.w() <= 1e-6) return false;

		pixel.set(
			(clip.x() / clip.w() * 0.5 + 0.5) * kWindowWidth,
			(0.5 - clip.y() / clip.w() * 0.5) * kWindowHeight
		);

		return true;
	}

	void operator()(osg::Node* node, osg::NodeVisitor* nv) override {
		if(auto* cv = nv->asCullVisitor()) update(*cv->getModelViewMatrix() * *cv->getProjectionMatrix());

		traverse(node, nv);
	}

	// Linearizes the projective mapping at the artboard's center with central differences.
	void update(const osg::Matrixd& mvp) {
		const double step = 1e-3 / placement.scale;
		const double cx = placement.centerX;
		const double cy = placement.centerY;

		osg::Vec2d p0, px0, px1, py0, py1;

		const bool visible =
			toPixel(mvp, cx, cy, p0) &&
			toPixel(mvp, cx - step, cy, px0) &&
			toPixel(mvp, cx + step, cy, px1) &&
			toPixel(mvp, cx, cy - step, py0) &&
			toPixel(mvp, cx, cy + step, py1)
		;

		scene->setDrawMode(visible ? osgRive::DrawMode::SCENE : osgRive::DrawMode::NONE);

		if(!visible) return;

		const osg::Vec2d xAxis = (px1 - px0) / (2.0 * step);
		const osg::Vec2d yAxis = (py1 - py0) / (2.0 * step);
		const osg::Vec2d t = p0 - xAxis * cx - yAxis * cy;

		scene->setTransform({
			static_cast<float>(xAxis.x()),
			static_cast<float>(xAxis.y()),
			static_cast<float>(yAxis.x()),
			static_cast<float>(yAxis.y()),
			static_cast<float>(t.x()),
			static_cast<float>(t.y())
		});
	}
};

// Rive binds its own FBO while drawing, so the producer gets a PRE_RENDER FBO stage of its own
// (1x1 dummy attachment, nothing else in it): OSG rebinds after every FBO stage, and it runs before
// the main camera samples Rive's texture.
osg::ref_ptr<osg::Camera> makeProducerStage(osgRive::Scene* scene) {
	osg::ref_ptr<osg::Texture2D> dummy = new osg::Texture2D();

	dummy->setTextureSize(1, 1);
	dummy->setInternalFormat(GL_RGBA8);

	osg::ref_ptr<osg::Camera> stage = new osg::Camera();

	stage->setRenderOrder(osg::Camera::PRE_RENDER);
	stage->setRenderTargetImplementation(osg::Camera::FRAME_BUFFER_OBJECT);
	stage->setReferenceFrame(osg::Transform::ABSOLUTE_RF);
	stage->setViewport(0, 0, 1, 1);
	stage->setClearMask(0);
	stage->setCullingActive(false);
	stage->attach(osg::Camera::COLOR_BUFFER, dummy.get());
	stage->addChild(scene);

	return stage;
}

osg::ref_ptr<osg::Geode> makeScreenQuad(osg::Texture2D* texture) {
	osg::ref_ptr<osg::Geometry> quad = osg::createTexturedQuadGeometry(
		osg::Vec3(-1.0f, -1.0f, 0.0f),
		osg::Vec3(2.0f, 0.0f, 0.0f),
		osg::Vec3(0.0f, 2.0f, 0.0f)
	);

	// NDC geometry: its bound means nothing to the frustum, and the CullVisitor tests both the
	// Geode and each Drawable.
	quad->setCullingActive(false);

	osg::ref_ptr<osg::Program> program = new osg::Program();

	program->addShader(new osg::Shader(osg::Shader::VERTEX, kScreenVert));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, kScreenFrag));

	auto* ss = quad->getOrCreateStateSet();

	ss->setAttributeAndModes(program.get());
	ss->setTextureAttributeAndModes(0, texture, osg::StateAttribute::ON);
	ss->addUniform(new osg::Uniform("riveTexture", 0));
	ss->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
	ss->setMode(GL_BLEND, osg::StateAttribute::ON);
	// Rive writes premultiplied color.
	ss->setAttributeAndModes(new osg::BlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));
	ss->setRenderBinDetails(1, "RenderBin");

	osg::ref_ptr<osg::Geode> geode = new osg::Geode();

	geode->setCullingActive(false);
	geode->addDrawable(quad.get());

	return geode;
}

osg::ref_ptr<osg::Geode> makeOutline(const osgRive::ArtboardBounds& b, const ArtboardPlacement& p) {
	osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array();

	vertices->push_back(p.toWorld(b.minX, b.minY));
	vertices->push_back(p.toWorld(b.maxX, b.minY));
	vertices->push_back(p.toWorld(b.maxX, b.maxY));
	vertices->push_back(p.toWorld(b.minX, b.maxY));

	osg::ref_ptr<osg::Geometry> outline = new osg::Geometry();

	outline->setVertexArray(vertices.get());
	outline->addPrimitiveSet(new osg::DrawArrays(GL_LINE_LOOP, 0, 4));

	osg::ref_ptr<osg::Program> program = new osg::Program();

	program->addShader(new osg::Shader(osg::Shader::VERTEX, kOutlineVert));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, kOutlineFrag));

	auto* ss = outline->getOrCreateStateSet();

	ss->setAttributeAndModes(program.get());
	ss->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
	// After the Rive quad, so the true projection draws on top of the affine approximation.
	ss->setRenderBinDetails(2, "RenderBin");

	osg::ref_ptr<osg::Geode> geode = new osg::Geode();

	geode->addDrawable(outline.get());

	return geode;
}

int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);
	osgViewer::Viewer viewer(args);

	if(argc < 2) {
		std::cerr << "usage: osgrive-example-screen <file.riv>" << std::endl;

		return 1;
	}

	const std::string rivPath = argv[1];

	osgRive::ArtboardBounds bounds;

	try {
		bounds = osgRive::readArtboardBounds(rivPath);
	}

	catch(const std::exception& e) {
		std::cerr << "osgrive-example-screen: " << e.what() << std::endl;

		return 1;
	}

	const ArtboardPlacement placement(bounds);

	osg::ref_ptr<osgRive::Scene> riveScene = new osgRive::Scene(
		rivPath,
		kWindowWidth,
		kWindowHeight
	);

	// The Scene's own unit display quad is replaced by the screen quad below, which leaves the
	// Scene without a valid bound - it must not be culled.
	riveScene->getDisplayGeode()->setNodeMask(0);
	riveScene->setCullingActive(false);

	osg::ref_ptr<osg::Group> producer = new osg::Group();

	producer->setCullingActive(false);
	producer->setCullCallback(new ScreenTransformCallback(riveScene.get(), placement));
	producer->addChild(makeProducerStage(riveScene.get()).get());

	osg::ref_ptr<osg::Group> root = new osg::Group();

	root->addChild(producer.get());
	root->addChild(makeScreenQuad(riveScene->getTexture()).get());
	root->addChild(makeOutline(bounds, placement).get());

	osg::ref_ptr<osgGA::TrackballManipulator> manipulator = new osgGA::TrackballManipulator();

	// The artboard's longest side is 1 unit; 2.2 units back frames it at the default 30 degree FOV.
	manipulator->setHomePosition(
		osg::Vec3d(0.0, 0.0, 2.2),
		osg::Vec3d(0.0, 0.0, 0.0),
		osg::Vec3d(0.0, 1.0, 0.0)
	);

	// SingleThreaded is mandatory -- multi-threaded causes an NVIDIA driver
	// crash when OSG's VAO state is invalidated across threads.
	viewer.setThreadingModel(osgViewer::Viewer::SingleThreaded);
	viewer.getCamera()->setClearColor(osg::Vec4(0.1f, 0.1f, 0.1f, 1.0f));
	viewer.setSceneData(root.get());
	viewer.setCameraManipulator(manipulator.get());
	viewer.addEventHandler(new osgViewer::StatsHandler());
	viewer.setUpViewInWindow(50, 50, kWindowWidth, kWindowHeight);

	return viewer.run();
}
