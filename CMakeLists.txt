cmake_minimum_required(VERSION 3.19)

project(osgRive)

set(RIVE_RUNTIME_DIR "/home/cubicool/dev/rive-runtime" CACHE PATH "Rive runtime checkout")
set(RIVE_BUILD_DIR "${RIVE_RUNTIME_DIR}/tests/out/debug" CACHE PATH "Built Rive debug output directory")

find_package(OpenSceneGraph REQUIRED
	osg
	osgDB
	osgGA
	osgViewer
	osgUtil
	osgFX
	osgText
	osgWidget
	osgAnimation
	osgShadow
)

# osgRive nodekit library: renders a Rive .riv scene into an OSG-owned texture
# via osgRive::Scene, an osg::Group that owns its producer drawable and display
# geometry (see include/osgRive/Scene).
add_library(osgRive
	src/osgRive/Scene.cpp
	src/osgRive/TextureRenderer.cpp
	src/rive_texture_renderer.cpp
)

target_compile_features(osgRive PUBLIC cxx_std_20)

target_include_directories(osgRive
	PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}/include
		${OPENSCENEGRAPH_INCLUDE_DIRS}
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src
		${RIVE_RUNTIME_DIR}/include
		${RIVE_RUNTIME_DIR}/renderer/include
		${RIVE_RUNTIME_DIR}/renderer/glad
		${RIVE_RUNTIME_DIR}/renderer/glad/include
		${RIVE_RUNTIME_DIR}/tests/dependencies/rive-app_yoga_rive_changes_v2_0_1_2
		${RIVE_RUNTIME_DIR}/tests/dependencies/rive-app_miniaudio_rive_changes_5
)

target_compile_definitions(osgRive PRIVATE
	OSGRIVE_LIBRARY
	WITH_RIVE_TOOLS
	WITH_RIVE_TEXT
	DRIVE_CANVAS
	WITH_RIVE_LAYOUT
	DEBUG
	DRIVE_DESKTOP_GL
	DRIVE_ORE
	ORE_BACKEND_GL
	YOGA_EXPORT=
	_RIVE_INTERNAL_
)

target_compile_options(osgRive PRIVATE
	-w
)

target_link_libraries(osgRive
	PUBLIC
		${OPENSCENEGRAPH_LIBRARIES}
	PRIVATE
		-Wl,--start-group
		${RIVE_BUILD_DIR}/librive_pls_renderer.a
		${RIVE_BUILD_DIR}/librive_decoders.a
		${RIVE_BUILD_DIR}/librive.a
		${RIVE_BUILD_DIR}/liblibpng.a
		${RIVE_BUILD_DIR}/libzlib.a
		${RIVE_BUILD_DIR}/liblibwebp.a
		${RIVE_BUILD_DIR}/librive_yoga.a
		${RIVE_BUILD_DIR}/librive_harfbuzz.a
		${RIVE_BUILD_DIR}/librive_sheenbidi.a
		${RIVE_BUILD_DIR}/libminiaudio.a
		${RIVE_BUILD_DIR}/liblibjpeg.a
		-Wl,--end-group
		m
		z
		dl
		pthread
		GL
		EGL
		X11
		gbm
		drm
)

# Clean, minimal example using the osgRive library (osgRive::Scene). Needs no
# Rive/GL include dirs or compile definitions of its own -- everything it
# needs is hidden behind osgRive's public/private split.
add_executable(osgrive-example-quad
	examples/osgrive_example_quad.cpp
)
target_link_libraries(osgrive-example-quad PRIVATE osgRive)

# Reference implementation predating the osgRive library: does everything by
# hand (raw GL state resets around a Camera::DrawCallback) instead of using
# osgRive::Scene. Kept working as a known-good baseline / for framebuffer-mode
# (--framebuffer) reference, which osgRive::Scene does not implement yet.
add_executable(osgrive-example-monolith
	examples/osgrive_reference_monolith.cpp
	src/rive_texture_renderer.cpp
)

target_compile_features(osgrive-example-monolith PUBLIC cxx_std_20)
target_link_libraries(osgrive-example-monolith PRIVATE
	${OPENSCENEGRAPH_LIBRARIES}
	-Wl,--start-group
	${RIVE_BUILD_DIR}/librive_pls_renderer.a
	${RIVE_BUILD_DIR}/librive_decoders.a
	${RIVE_BUILD_DIR}/librive.a
	${RIVE_BUILD_DIR}/liblibpng.a
	${RIVE_BUILD_DIR}/libzlib.a
	${RIVE_BUILD_DIR}/liblibwebp.a
	${RIVE_BUILD_DIR}/librive_yoga.a
	${RIVE_BUILD_DIR}/librive_harfbuzz.a
	${RIVE_BUILD_DIR}/librive_sheenbidi.a
	${RIVE_BUILD_DIR}/libminiaudio.a
	${RIVE_BUILD_DIR}/liblibjpeg.a
	-Wl,--end-group
	m
	z
	dl
	pthread
	GL
	EGL
	X11
	gbm
	drm
)
target_include_directories(osgrive-example-monolith PRIVATE
	${OPENSCENEGRAPH_INCLUDE_DIRS}
	${RIVE_RUNTIME_DIR}/include
	${RIVE_RUNTIME_DIR}/renderer/include
	${RIVE_RUNTIME_DIR}/renderer/glad
	${RIVE_RUNTIME_DIR}/renderer/glad/include
	${RIVE_RUNTIME_DIR}/tests/dependencies/rive-app_yoga_rive_changes_v2_0_1_2
	${RIVE_RUNTIME_DIR}/tests/dependencies/rive-app_miniaudio_rive_changes_5
	${CMAKE_SOURCE_DIR}/src
)
target_compile_definitions(osgrive-example-monolith PRIVATE
	WITH_RIVE_TOOLS
	WITH_RIVE_TEXT
	DRIVE_CANVAS
	WITH_RIVE_LAYOUT
	DEBUG
	DRIVE_DESKTOP_GL
	DRIVE_ORE
	ORE_BACKEND_GL
	YOGA_EXPORT=
	_RIVE_INTERNAL_
)
target_compile_options(osgrive-example-monolith PRIVATE
	-w
)

add_executable(glfwRiveTexture
	examples/glfw_rive_texture.cpp
	src/rive_texture_renderer.cpp
)

target_compile_features(glfwRiveTexture PUBLIC cxx_std_20)
target_link_libraries(glfwRiveTexture PRIVATE
	-Wl,--start-group
	${RIVE_BUILD_DIR}/librive_pls_renderer.a
	${RIVE_BUILD_DIR}/librive_decoders.a
	${RIVE_BUILD_DIR}/librive.a
	${RIVE_BUILD_DIR}/liblibpng.a
	${RIVE_BUILD_DIR}/libzlib.a
	${RIVE_BUILD_DIR}/liblibwebp.a
	${RIVE_BUILD_DIR}/librive_yoga.a
	${RIVE_BUILD_DIR}/librive_harfbuzz.a
	${RIVE_BUILD_DIR}/librive_sheenbidi.a
	${RIVE_BUILD_DIR}/libminiaudio.a
	${RIVE_BUILD_DIR}/liblibjpeg.a
	${RIVE_BUILD_DIR}/libglfw3.a
	-Wl,--end-group
	m
	z
	dl
	pthread
	GL
	EGL
	X11
	gbm
	drm
)
target_include_directories(glfwRiveTexture PRIVATE
	${RIVE_RUNTIME_DIR}/include
	${RIVE_RUNTIME_DIR}/renderer/include
	${RIVE_RUNTIME_DIR}/renderer/glad
	${RIVE_RUNTIME_DIR}/renderer/glad/include
	${RIVE_RUNTIME_DIR}/tests/dependencies/glfw_glfw_3.4/include
	${RIVE_RUNTIME_DIR}/tests/dependencies/rive-app_yoga_rive_changes_v2_0_1_2
	${RIVE_RUNTIME_DIR}/tests/dependencies/rive-app_miniaudio_rive_changes_5
)
target_compile_definitions(glfwRiveTexture PRIVATE
	WITH_RIVE_TOOLS
	WITH_RIVE_TEXT
	DRIVE_CANVAS
	WITH_RIVE_LAYOUT
	DEBUG
	DRIVE_DESKTOP_GL
	DRIVE_ORE
	ORE_BACKEND_GL
	YOGA_EXPORT=
	_RIVE_INTERNAL_
)
target_compile_options(glfwRiveTexture PRIVATE
	-w
)

# Standalone, no Rive/OSG dependency -- isolated test of the GPU
# buffer-streaming technique documented in slughorn/ai/context-todo-rive.md.
# Reuses the GLFW/glad already vendored for the Rive examples purely for
# convenience; does not link or include any Rive code.
add_executable(ssboStreamDemo
	examples/ssbo_stream_demo.cpp
	${RIVE_RUNTIME_DIR}/renderer/glad/glad_custom.c
	${RIVE_RUNTIME_DIR}/renderer/glad/src/gles2.c
	${RIVE_RUNTIME_DIR}/renderer/glad/src/egl.c
)
target_compile_features(ssboStreamDemo PUBLIC cxx_std_20)
target_link_libraries(ssboStreamDemo PRIVATE
	${RIVE_BUILD_DIR}/libglfw3.a
	m
	dl
	pthread
	GL
	X11
)
target_include_directories(ssboStreamDemo PRIVATE
	${RIVE_RUNTIME_DIR}/renderer/glad
	${RIVE_RUNTIME_DIR}/renderer/glad/include
	${RIVE_RUNTIME_DIR}/tests/dependencies/glfw_glfw_3.4/include
)
target_compile_options(ssboStreamDemo PRIVATE
	-w
)
