// Standalone demo (no Rive, no OSG -- just GLFW + GL) of the buffer-update
// technique found in Rive's renderer: per-frame dynamic geometry written
// into a GPU buffer via glMapBufferRange(GL_MAP_WRITE_BIT |
// GL_MAP_INVALIDATE_BUFFER_BIT) ("orphan and remap") instead of a full
// glBufferData reupload every frame.
//
// The SSBO holds points sampled along an animated curve -- the same shape
// of data osgSlug already keeps in an SSBO from path.sample() -- so this is
// an isolated test of the *update* technique, independent of any Rive or
// slughorn integration. See slughorn/ai/context-todo-rive.md.
//
// Press T to toggle between the naive (glBufferData every frame) and
// streaming (map + invalidate) update paths; average update-only CPU time
// is printed to stdout every 120 frames so the difference is measured, not
// just asserted.
//
// A rolling graph along the bottom of the window plots per-frame update
// time (auto-scaled to whatever's currently in view, so a spike dominates
// the scale for a few seconds and then fades out as it scrolls left) --
// green samples were taken in streaming mode, orange in naive mode, so a
// toggle shows up as a visible color change in the trace. This is the part
// worth watching: average CPU time alone (the stdout prints) can hide
// occasional GPU-stall spikes that orphan-and-remap is specifically meant
// to avoid -- the graph is where those would actually show up.

#include "glad_custom.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
constexpr int kWindowWidth = 900;
constexpr int kWindowHeight = 900;
constexpr size_t kDefaultPointCount = 20000;
constexpr size_t kHistoryLength = 240; // ~4s of history at 60Hz
constexpr float kGraphLeftX = -0.92f;
constexpr float kGraphRightX = 0.92f;
constexpr float kGraphBaselineY = -0.92f;
constexpr float kGraphHeight = 0.18f;
constexpr float kGraphMinScaleMicros = 20.0f; // avoid blowing up the scale at startup

struct Point
{
    float x, y;
    float r, g, b;
};

// IMPORTANT: this struct is declared as 5 separate floats, not
// `vec2 position; vec3 color;`, on purpose. std430 layout rules give vec3
// a 16-byte base alignment (even though its size is 12), which would pad
// this struct out to 32 bytes on the GPU side -- silently mismatched
// against the CPU's tightly-packed 20-byte `Point` (5x float, no padding
// since every member shares the same 4-byte alignment). That mismatch was
// the actual bug behind the "giant garbage loop" artifact: the GPU read
// every point at the wrong byte offset, and once gl_VertexID pushed it
// past where the (20-byte-stride) buffer actually ended, it read
// uninitialized GPU memory beyond the allocation. All-scalar-float layout
// guarantees byte-for-byte agreement with the CPU struct.
const char* kVertexShader = R"(
#version 430 core

struct Point
{
    float x;
    float y;
    float r;
    float g;
    float b;
};

layout(std430, binding = 0) readonly buffer PointBuffer
{
    Point points[];
};

out vec3 vColor;

void main()
{
    Point p = points[gl_VertexID];
    gl_Position = vec4(p.x, p.y, 0.0, 1.0);
    gl_PointSize = 3.0;
    vColor = vec3(p.r, p.g, p.b);
}
)";

const char* kFragmentShader = R"(
#version 430 core

uniform bool uCircularPoints;

in vec3 vColor;
out vec4 fragColor;

void main()
{
    // gl_PointCoord is only meaningful when rendering GL_POINTS -- for the
    // GL_LINE_STRIP graph draw it's implementation-defined, and applying
    // this discard test to it was silently dropping every line fragment.
    if (uCircularPoints)
    {
        vec2 d = gl_PointCoord - vec2(0.5);
        if (dot(d, d) > 0.25)
        {
            discard;
        }
    }
    fragColor = vec4(vColor, 1.0);
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
    GLuint vertex = compileShader(GL_VERTEX_SHADER, kVertexShader);
    GLuint fragment = compileShader(GL_FRAGMENT_SHADER, kFragmentShader);
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

// Fills `points` with a wavy ring of `count` points -- stands in for "N
// curve-sample points, all dirty this frame," the same shape of data
// osgSlug keeps in an SSBO from path.sample().
void computeCurve(std::vector<Point>& points, size_t count, float t)
{
    points.resize(count);
    for (size_t i = 0; i < count; ++i)
    {
        float u = static_cast<float>(i) / static_cast<float>(count);
        float angle = u * 6.28318530718f;
        float radius = 0.55f + 0.12f * std::sin(angle * 5.0f + t * 2.0f) +
                       0.05f * std::sin(angle * 17.0f - t * 3.5f);
        Point& p = points[i];
        p.x = radius * std::cos(angle);
        p.y = radius * std::sin(angle);
        p.r = 0.5f + 0.5f * std::sin(angle + t);
        p.g = 0.5f + 0.5f * std::sin(angle + t + 2.094f);
        p.b = 0.5f + 0.5f * std::sin(angle + t + 4.188f);
    }
}
} // namespace

int main(int argc, char** argv)
{
    // Line-buffer stdout so the periodic update-mode/timing prints below
    // show up immediately instead of waiting on a full buffer flush.
    std::setvbuf(stdout, nullptr, _IOLBF, 0);

    size_t pointCount = kDefaultPointCount;
    for (int i = 1; i < argc; ++i)
    {
        if (std::string(argv[i]) == "--count" && i + 1 < argc)
        {
            pointCount = static_cast<size_t>(std::atoi(argv[++i]));
        }
    }

    if (!glfwInit())
    {
        std::fprintf(stderr, "glfwInit failed\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(kWindowWidth,
                                          kWindowHeight,
                                          "SSBO Stream Demo (T = toggle update mode)",
                                          nullptr,
                                          nullptr);
    if (window == nullptr)
    {
        std::fprintf(stderr, "glfwCreateWindow failed\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!gladLoadCustomLoader(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress)))
    {
        std::fprintf(stderr, "gladLoadCustomLoader failed\n");
        return 1;
    }

    GLuint program = makeProgram();
    GLint circularPointsLoc = glGetUniformLocation(program, "uCircularPoints");

    // Core profile forbids drawing with the default VAO (id 0). All actual
    // vertex data here comes from the SSBO via gl_VertexID, so an empty,
    // attribute-less VAO is enough to satisfy that requirement.
    GLuint vao = 0;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    GLuint ssbo = 0;
    glGenBuffers(1, &ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
    GLsizeiptr bufferBytes = static_cast<GLsizeiptr>(pointCount * sizeof(Point));
    glBufferData(GL_SHADER_STORAGE_BUFFER, bufferBytes, nullptr, GL_DYNAMIC_DRAW);

    // Second, much smaller SSBO purely for the on-screen timing graph --
    // always updated the streaming way since its own update cost isn't
    // what's being measured.
    GLuint graphSsbo = 0;
    glGenBuffers(1, &graphSsbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, graphSsbo);
    GLsizeiptr graphBufferBytes =
        static_cast<GLsizeiptr>(kHistoryLength * sizeof(Point));
    glBufferData(GL_SHADER_STORAGE_BUFFER, graphBufferBytes, nullptr, GL_DYNAMIC_DRAW);

    bool useStreamingUpdate = true;
    std::printf("update mode: streaming (map + invalidate) -- press T to toggle\n");
    std::printf("bottom strip: rolling per-frame update time, "
               "green = streaming, orange = naive (auto-scaled)\n");

    std::vector<Point> points;
    points.reserve(pointCount);

    std::vector<float> historyMicros(kHistoryLength, 0.0f);
    std::vector<bool> historyStreaming(kHistoryLength, true);
    std::vector<Point> graphPoints(kHistoryLength);
    size_t historyWrite = 0;

    double accumulatedUpdateSeconds = 0.0;
    int framesSinceReport = 0;
    bool tWasDown = false;

    while (!glfwWindowShouldClose(window))
    {
        double now = glfwGetTime();

        bool tIsDown = glfwGetKey(window, GLFW_KEY_T) == GLFW_PRESS;
        if (tIsDown && !tWasDown)
        {
            useStreamingUpdate = !useStreamingUpdate;
            std::printf("update mode: %s\n",
                       useStreamingUpdate ? "streaming (map + invalidate)"
                                          : "naive (glBufferData every frame)");
        }
        tWasDown = tIsDown;

        computeCurve(points, pointCount, static_cast<float>(now));

        auto updateStart = std::chrono::steady_clock::now();
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
        if (useStreamingUpdate)
        {
            // The technique Rive uses for per-frame dirty geometry: tell
            // the driver to hand back a fresh, uncontended region instead
            // of reusing the one the GPU might still be reading from, then
            // write straight into it -- no driver-side copy, no waiting on
            // last frame's draw call.
            void* mapped = glMapBufferRange(GL_SHADER_STORAGE_BUFFER,
                                            0,
                                            bufferBytes,
                                            GL_MAP_WRITE_BIT |
                                                GL_MAP_INVALIDATE_BUFFER_BIT);
            std::memcpy(mapped, points.data(), bufferBytes);
            glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
        }
        else
        {
            // The naive path most code reaches for first: reallocate and
            // copy the whole buffer every frame.
            glBufferData(
                GL_SHADER_STORAGE_BUFFER, bufferBytes, points.data(), GL_DYNAMIC_DRAW);
        }
        auto updateEnd = std::chrono::steady_clock::now();
        double frameUpdateMicros =
            std::chrono::duration<double>(updateEnd - updateStart).count() * 1e6;
        accumulatedUpdateSeconds += frameUpdateMicros * 1e-6;
        ++framesSinceReport;

        historyMicros[historyWrite] = static_cast<float>(frameUpdateMicros);
        historyStreaming[historyWrite] = useStreamingUpdate;
        historyWrite = (historyWrite + 1) % kHistoryLength;

        // Rebuild the graph's vertex list oldest-to-newest (left-to-right)
        // and auto-scale to whatever's currently in the visible window, so
        // a spike dominates the scale for a few seconds and fades out as
        // it scrolls off the left edge.
        float maxMicros = kGraphMinScaleMicros;
        for (float v : historyMicros)
        {
            maxMicros = std::max(maxMicros, v);
        }
        for (size_t i = 0; i < kHistoryLength; ++i)
        {
            size_t idx = (historyWrite + i) % kHistoryLength;
            float u = static_cast<float>(i) / static_cast<float>(kHistoryLength - 1);
            float normalized = std::min(historyMicros[idx] / maxMicros, 1.0f);
            Point& gp = graphPoints[i];
            gp.x = kGraphLeftX + (kGraphRightX - kGraphLeftX) * u;
            gp.y = kGraphBaselineY + kGraphHeight * normalized;
            if (historyStreaming[idx])
            {
                gp.r = 0.25f;
                gp.g = 0.95f;
                gp.b = 0.45f;
            }
            else
            {
                gp.r = 1.0f;
                gp.g = 0.55f;
                gp.b = 0.15f;
            }
        }
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, graphSsbo);
        void* mappedGraph = glMapBufferRange(GL_SHADER_STORAGE_BUFFER,
                                             0,
                                             graphBufferBytes,
                                             GL_MAP_WRITE_BIT |
                                                 GL_MAP_INVALIDATE_BUFFER_BIT);
        std::memcpy(mappedGraph, graphPoints.data(), graphBufferBytes);
        glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);

        if (framesSinceReport >= 120)
        {
            double avgMicros = (accumulatedUpdateSeconds / framesSinceReport) * 1e6;
            std::printf("[%s] avg SSBO update: %.1f us/frame (%zu points)\n",
                       useStreamingUpdate ? "streaming" : "naive   ",
                       avgMicros,
                       pointCount);
            std::fflush(stdout);
            accumulatedUpdateSeconds = 0.0;
            framesSinceReport = 0;
        }

        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
        glViewport(0, 0, framebufferWidth, framebufferHeight);
        glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(program);

        glUniform1i(circularPointsLoc, 1);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo);
        glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(pointCount));

        glUniform1i(circularPointsLoc, 0);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, graphSsbo);
        glDrawArrays(GL_LINE_STRIP, 0, static_cast<GLsizei>(kHistoryLength));

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
