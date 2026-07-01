#include "../src/rive_texture_renderer.hpp"

#include "glad_custom.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>

namespace
{
constexpr int kWindowWidth = 800;
constexpr int kWindowHeight = 600;
constexpr int kTextureWidth = 512;
constexpr int kTextureHeight = 512;

const char* kQuadVertexShader = R"(
#version 330 core

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texcoord;

out vec2 uv;

void main()
{
    uv = texcoord;
    gl_Position = vec4(position, 0.0, 1.0);
}
)";

const char* kQuadFragmentShader = R"(
#version 330 core

uniform sampler2D colorTex;

in vec2 uv;
out vec4 fragColor;

void main()
{
    fragColor = texture(colorTex, uv);
}
)";

GLuint compileShader(GLenum type, const char* source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[4096] = {};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "shader compile failed:\n%s\n", log);
        std::exit(1);
    }
    return shader;
}

GLuint makeProgram()
{
    GLuint vertex = compileShader(GL_VERTEX_SHADER, kQuadVertexShader);
    GLuint fragment = compileShader(GL_FRAGMENT_SHADER, kQuadFragmentShader);
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[4096] = {};
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::fprintf(stderr, "program link failed:\n%s\n", log);
        std::exit(1);
    }
    return program;
}

GLuint makeRiveTexture()
{
    std::vector<unsigned char> pixels(kTextureWidth * kTextureHeight * 4);
    for (int y = 0; y < kTextureHeight; ++y)
    {
        for (int x = 0; x < kTextureWidth; ++x)
        {
            size_t i = static_cast<size_t>((y * kTextureWidth + x) * 4);
            pixels[i + 0] = 255;
            pixels[i + 1] = x < kTextureWidth / 2 ? 0 : 255;
            pixels[i + 2] = y < kTextureHeight / 2 ? 0 : 255;
            pixels[i + 3] = 255;
        }
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA8,
                 kTextureWidth,
                 kTextureHeight,
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

GLuint makeQuadVAO()
{
    const float vertices[] = {
        -0.45f, -0.45f, 0.0f, 0.0f,
         0.45f, -0.45f, 1.0f, 0.0f,
        -0.45f,  0.45f, 0.0f, 1.0f,
         0.45f,  0.45f, 1.0f, 1.0f,
    };

    GLuint vao = 0;
    GLuint vbo = 0;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1,
                          2,
                          GL_FLOAT,
                          GL_FALSE,
                          4 * sizeof(float),
                          reinterpret_cast<const void*>(2 * sizeof(float)));
    glBindVertexArray(0);
    return vao;
}

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
            std::fprintf(stderr, "GL errors drained at %s:\n", label);
            printedHeader = true;
        }
        std::fprintf(stderr, "  0x%04x\n", static_cast<unsigned int>(err));
    }
#else
    (void)label;
#endif
}

void forceMainPassState(int framebufferWidth, int framebufferHeight)
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    GLenum backBuffer = GL_BACK;
    glDrawBuffers(1, &backBuffer);
    glReadBuffer(GL_BACK);
    glViewport(0, 0, framebufferWidth, framebufferHeight);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glStencilMask(0xff);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
}
} // namespace

int main(int argc, char** argv)
{
    bool renderRive = true;
    bool riveClearOnly = false;
    RiveDrawMode drawMode = RiveDrawMode::scene;
    const char* rivPathArg = nullptr;
    for (int i = 1; i < argc; ++i)
    {
        if (std::string(argv[i]) == "--no-rive")
        {
            renderRive = false;
        }
        else if (std::string(argv[i]) == "--rive-clear-only")
        {
            riveClearOnly = true;
        }
        else if (std::string(argv[i]) == "--draw-scene")
        {
            drawMode = RiveDrawMode::scene;
        }
        else if (std::string(argv[i]) == "--draw-artboard")
        {
            drawMode = RiveDrawMode::artboard;
        }
        else if (std::string(argv[i]) == "--draw-internal")
        {
            drawMode = RiveDrawMode::artboardInternal;
        }
        else if (std::string(argv[i]) == "--draw-none")
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

    if (!glfwInit())
    {
        std::fprintf(stderr, "glfwInit failed\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window =
        glfwCreateWindow(kWindowWidth, kWindowHeight, "Rive Texture", nullptr, nullptr);
    if (window == nullptr)
    {
        std::fprintf(stderr, "glfwCreateWindow failed\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);

    if (!gladLoadCustomLoader(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress)))
    {
        std::fprintf(stderr, "gladLoadCustomLoader failed\n");
        return 1;
    }

    GLuint riveTexture = makeRiveTexture();
    GLuint program = makeProgram();
    GLuint vao = makeQuadVAO();

    RiveTextureRenderer rive(rivPath, kTextureWidth, kTextureHeight);

    using Clock = std::chrono::steady_clock;
    constexpr double kReportIntervalSeconds = 5.0;
    uint64_t frames = 0;
    auto fpsStart = Clock::now();

    double previousTime = glfwGetTime();
    while (!glfwWindowShouldClose(window))
    {
        double now = glfwGetTime();
        float dt = static_cast<float>(now - previousTime);
        previousTime = now;

        if (renderRive)
        {
            if (riveClearOnly)
            {
                rive.clearTexture(riveTexture);
            }
            else
            {
                rive.renderToTexture(riveTexture, dt, drawMode);
            }
        }

        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
        drainGLErrors("after optional Rive render");
        forceMainPassState(framebufferWidth, framebufferHeight);
        glClearColor(0.0f, 0.85f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, riveTexture);
        glUniform1i(glGetUniformLocation(program, "colorTex"), 0);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindVertexArray(0);

        glfwSwapBuffers(window);
        glfwPollEvents();

        ++frames;
        const auto fpsNow = Clock::now();
        const double elapsed =
            std::chrono::duration_cast<std::chrono::duration<double>>(
                fpsNow - fpsStart)
                .count();
        if (elapsed >= kReportIntervalSeconds)
        {
            std::printf("[glfwRiveTexture] %.3f FPS (%llu frames / %.3fs)\n",
                        static_cast<double>(frames) / elapsed,
                        static_cast<unsigned long long>(frames),
                        elapsed);
            std::fflush(stdout);
            frames = 0;
            fpsStart = fpsNow;
        }
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
