#include "utils/no_op_factory.hpp"

#include "rive/artboard.hpp"
#include "rive/assets/file_asset.hpp"
#include "rive/file.hpp"
#include "rive/math/raw_path.hpp"
#include "rive/shapes/path.hpp"
#include "rive/shapes/shape.hpp"
#include "rive/shapes/paint/shape_paint.hpp"
#include "rive/shapes/paint/fill.hpp"
#include "rive/shapes/paint/stroke.hpp"
#include "rive/shapes/paint/solid_color.hpp"
#include "rive/shapes/paint/linear_gradient.hpp"
#include "rive/shapes/paint/radial_gradient.hpp"
#include "rive/shapes/paint/gradient_stop.hpp"
#include "rive/shapes/paint/color.hpp"
#include "rive/animation/state_machine_instance.hpp"
#include "rive/animation/linear_animation_instance.hpp"

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

// This tool is a CPU-only path/paint/animation decomposer. It never touches
// the GPU PLS renderer (rive::NoOpFactory) -- the goal is to find out how
// much of Rive's vector content (geometry, fill rule, paint, per-frame
// deformation) can be pulled out independent of Rive's own renderer, for
// comparison against Slughorn's own contour/Atlas model. See
// slughorn/ai/context-todo-rive.md for the planning doc this feeds.

static bool readFile(const char* path, std::vector<uint8_t>* bytes)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        return false;
    }
    *bytes = std::vector<uint8_t>(std::istreambuf_iterator<char>(stream), {});
    return true;
}

static const char* importResultName(rive::ImportResult result)
{
    switch (result)
    {
        case rive::ImportResult::success:
            return "success";
        case rive::ImportResult::unsupportedVersion:
            return "unsupportedVersion";
        case rive::ImportResult::malformed:
            return "malformed";
    }
    return "unknown";
}

static void printPoint(const rive::Vec2D& p)
{
    std::cout << p.x << " " << p.y;
}

static void printRawPath(const rive::RawPath& rawPath, const char* indent)
{
    for (auto [verb, pts] : rawPath)
    {
        std::cout << indent;
        switch (verb)
        {
            case rive::PathVerb::move:
                std::cout << "M ";
                printPoint(pts[0]);
                break;
            case rive::PathVerb::line:
                std::cout << "L ";
                printPoint(pts[1]);
                break;
            case rive::PathVerb::quad:
                std::cout << "Q ";
                printPoint(pts[1]);
                std::cout << " ";
                printPoint(pts[2]);
                break;
            case rive::PathVerb::cubic:
                std::cout << "C ";
                printPoint(pts[1]);
                std::cout << " ";
                printPoint(pts[2]);
                std::cout << " ";
                printPoint(pts[3]);
                break;
            case rive::PathVerb::close:
                std::cout << "Z";
                break;
        }
        std::cout << "\n";
    }
}

static const char* fillRuleName(rive::FillRule rule)
{
    switch (rule)
    {
        case rive::FillRule::nonZero:
            return "nonZero";
        case rive::FillRule::evenOdd:
            return "evenOdd";
        case rive::FillRule::clockwise:
            return "clockwise";
    }
    return "unknown";
}

static rive::AABB computeAABB(const rive::RawPath& rawPath)
{
    rive::AABB box(std::numeric_limits<float>::max(),
                   std::numeric_limits<float>::max(),
                   std::numeric_limits<float>::lowest(),
                   std::numeric_limits<float>::lowest());
    for (const auto& pt : rawPath.points())
    {
        box.minX = std::min(box.minX, pt.x);
        box.minY = std::min(box.minY, pt.y);
        box.maxX = std::max(box.maxX, pt.x);
        box.maxY = std::max(box.maxY, pt.y);
    }
    return box;
}

static void printColor(rive::ColorInt c)
{
    std::ios::fmtflags flags(std::cout.flags());
    std::cout << "#" << std::hex << std::setfill('0') << std::setw(8) << c;
    std::cout.flags(flags);
}

// Fill/Stroke are Core objects attached as children somewhere under the
// owning Shape; walk up the parent chain to find it (Shape itself never
// appears in find<Fill/Stroke>() results).
static rive::Shape* findOwningShape(rive::Component* component)
{
    rive::ContainerComponent* p = component->parent();
    while (p != nullptr)
    {
        if (p->is<rive::Shape>())
        {
            return p->as<rive::Shape>();
        }
        p = p->parent();
    }
    return nullptr;
}

// Prints a ShapePaint's (Fill or Stroke) resolved paint -- solid color, or
// gradient with resolved stops. `instance` is needed to look up the
// GradientStop children of a gradient paint component.
static void printPaintInfo(rive::ArtboardInstance* instance,
                           rive::ShapePaint* paint,
                           const char* indent)
{
    const char* kind = paint->is<rive::Fill>() ? "fill" : "stroke";
    rive::Component* paintComponent = paint->paint();
    std::cout << indent << kind << ": ";
    if (paintComponent == nullptr)
    {
        std::cout << "<unresolved>\n";
        return;
    }

    if (paintComponent->is<rive::SolidColor>())
    {
        auto* solid = paintComponent->as<rive::SolidColor>();
        std::cout << "solid ";
        printColor(static_cast<rive::ColorInt>(solid->colorValue()));
        std::cout << "\n";
        return;
    }

    if (paintComponent->is<rive::LinearGradient>())
    {
        // RadialGradient derives from LinearGradient and shares its
        // start/end accessors -- check the more specific type for the label
        // only.
        auto* grad = paintComponent->as<rive::LinearGradient>();
        const char* gradKind = paintComponent->is<rive::RadialGradient>()
                                   ? "radial-gradient"
                                   : "linear-gradient";
        std::cout << gradKind << " start=(" << grad->startX() << ","
                  << grad->startY() << ") end=(" << grad->endX() << ","
                  << grad->endY() << ") stops=[";

        auto stops = instance->find<rive::GradientStop>();
        bool first = true;
        for (auto* stop : stops)
        {
            if (stop->parent() != paintComponent)
            {
                continue;
            }
            if (!first)
            {
                std::cout << " ";
            }
            first = false;
            std::cout << stop->position() << ":";
            printColor(static_cast<rive::ColorInt>(stop->colorValue()));
        }
        std::cout << "]\n";
        return;
    }

    std::cout << "<unknown paint type>\n";
}

// Dumps every Shape's world-space contour geometry (via worldPath(), which
// is what the renderer actually composes/transforms -- not Path::rawPath(),
// which is per-Path local geometry before Shape-level composition), its
// fill rule, and its resolved fill/stroke paint. `verbose` also prints the
// full M/L/C/Z contour listing; otherwise just a bounding-box signature
// (cheap way to prove geometry changed between frames without flooding
// stdout).
static void dumpShapes(rive::ArtboardInstance* instance, bool verbose)
{
    auto shapes = instance->find<rive::Shape>();
    std::cout << "      shapes: " << shapes.size() << "\n";
    for (size_t i = 0; i < shapes.size(); ++i)
    {
        auto* shape = shapes[i];

        // Shape::worldPath() is only composed when something actually
        // flags PathFlags::world (clipping shapes, mostly). Ordinary
        // Fill/Stroke paints request PathFlags::local or ::localClockwise
        // (see Fill::pathFlags() / pickPath()) -- the renderer applies
        // worldTransform() separately at draw time
        // (Shape::addToRenderPath). Pick whichever path the paints
        // actually populated, matching what the real render path does.
        rive::ShapePaintPath* shapePath = nullptr;
        if (shape->isFlagged(rive::PathFlags::local))
        {
            shapePath = shape->localPath();
        }
        else if (shape->isFlagged(rive::PathFlags::localClockwise))
        {
            shapePath = shape->localClockwisePath();
        }
        else if (shape->isFlagged(rive::PathFlags::world))
        {
            shapePath = shape->worldPath();
        }

        if (shapePath == nullptr || shapePath->empty())
        {
            std::cout << "        [" << i << "] " << shape->name()
                      << " <no composed path>\n";
            continue;
        }

        // Re-apply worldTransform() ourselves so the dumped geometry is in
        // world space -- directly comparable to slughorn's own world-space
        // contour model, instead of shape-local coordinates.
        rive::RawPath worldRawPath =
            shapePath->rawPath()->transform(shape->worldTransform());
        auto box = computeAABB(worldRawPath);
        std::cout << "        [" << i << "] " << shape->name()
                  << " fillRule=" << fillRuleName(shapePath->fillRule())
                  << " points=" << worldRawPath.points().size()
                  << " bbox=(" << box.minX << "," << box.minY << ")-("
                  << box.maxX << "," << box.maxY << ")\n";

        // Fill/Stroke list lives behind ShapePaintContainer::shapePaints(),
        // which is gated behind #ifdef TESTING in the upstream header (and
        // we deliberately do NOT build with -DTESTING -- that flag also
        // adds real data members to other classes like RawTextInput and
        // ImageAsset, which would desync this binary's object layout from
        // the prebuilt librive.a it links against). Walk Fill/Stroke ->
        // owning Shape instead, both reachable via the public find<>() API.
        auto fills = instance->find<rive::Fill>();
        for (auto* fill : fills)
        {
            if (findOwningShape(fill) == shape)
            {
                printPaintInfo(instance, fill, "          ");
            }
        }
        auto strokes = instance->find<rive::Stroke>();
        for (auto* stroke : strokes)
        {
            if (findOwningShape(stroke) == shape)
            {
                printPaintInfo(instance, stroke, "          ");
            }
        }

        if (verbose)
        {
            printRawPath(worldRawPath, "          ");
        }
    }
}

int main(int argc, const char** argv)
{
    bool dumpPaths = false;
    bool dumpShapesFlag = false;
    bool preferTimeline = false;
    int animateFrames = 0;
    float animateDt = 1.0f / 60.0f;
    const char* rivPath = nullptr;
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--paths")
        {
            dumpPaths = true;
        }
        else if (arg == "--shapes")
        {
            dumpShapesFlag = true;
        }
        else if (arg == "--animate" && i + 1 < argc)
        {
            animateFrames = std::atoi(argv[++i]);
        }
        else if (arg == "--timeline")
        {
            // State machines often sit in an idle state until driven by
            // pointer/input events, so advanceAndApply(dt) alone may never
            // move geometry. LinearAnimationInstance (a raw timeline) plays
            // back on its own with no input required -- prefer it when
            // proving per-frame deformation actually happens.
            preferTimeline = true;
        }
        else
        {
            rivPath = argv[i];
        }
    }

    if (rivPath == nullptr)
    {
        std::cerr << "usage: " << argv[0]
                  << " [--paths] [--shapes] [--animate N] file.riv\n";
        return 2;
    }

    std::vector<uint8_t> bytes;
    if (!readFile(rivPath, &bytes))
    {
        std::cerr << "failed to open " << rivPath << "\n";
        return 1;
    }

    rive::NoOpFactory factory;
    rive::ImportResult result = rive::ImportResult::malformed;
    auto file = rive::File::import(bytes, &factory, &result, nullptr);

    std::cout << "file: " << rivPath << "\n";
    std::cout << "bytes: " << bytes.size() << "\n";
    std::cout << "import: " << importResultName(result) << "\n";
    if (!file)
    {
        return 1;
    }

    std::cout << "artboards: " << file->artboardCount() << "\n";
    for (size_t i = 0; i < file->artboardCount(); ++i)
    {
        auto artboard = file->artboard(i);
        std::cout << "  [" << i << "] " << file->artboardNameAt(i) << "\n";
        if (!artboard)
        {
            continue;
        }

        std::cout << "      animations: " << artboard->animationCount()
                  << "\n";
        for (size_t j = 0; j < artboard->animationCount(); ++j)
        {
            std::cout << "        [" << j << "] "
                      << artboard->animationNameAt(j) << "\n";
        }

        std::cout << "      state machines: " << artboard->stateMachineCount()
                  << "\n";
        for (size_t j = 0; j < artboard->stateMachineCount(); ++j)
        {
            std::cout << "        [" << j << "] "
                      << artboard->stateMachineNameAt(j) << "\n";
        }

        std::cout << "      default state machine index: "
                  << artboard->defaultStateMachineIndex() << "\n";

        if (dumpPaths)
        {
            artboard->advance(0.0f);
            auto paths = artboard->find<rive::Path>();
            std::cout << "      paths: " << paths.size() << "\n";
            for (size_t j = 0; j < paths.size(); ++j)
            {
                const auto* path = paths[j];
                const auto& rawPath = path->rawPath();
                std::cout << "        [" << j << "] " << path->name()
                          << " points=" << rawPath.points().size()
                          << " verbs=" << rawPath.verbs().size()
                          << " hidden=" << (path->isHidden() ? "true" : "false")
                          << "\n";
                printRawPath(rawPath, "          ");
            }
        }

        if (dumpShapesFlag || animateFrames > 0)
        {
            auto instance = artboard->instance();

            std::unique_ptr<rive::StateMachineInstance> stateMachineInstance;
            std::unique_ptr<rive::LinearAnimationInstance> animationInstance;
            rive::Scene* scene = nullptr;
            if (preferTimeline && instance->animationCount() > 0)
            {
                animationInstance = instance->animationAt(0);
                scene = animationInstance.get();
            }
            else if (instance->stateMachineCount() > 0)
            {
                int smIndex = instance->defaultStateMachineIndex();
                stateMachineInstance = instance->stateMachineAt(
                    smIndex >= 0 ? static_cast<size_t>(smIndex) : 0);
                scene = stateMachineInstance.get();
            }
            else if (instance->animationCount() > 0)
            {
                animationInstance = instance->animationAt(0);
                scene = animationInstance.get();
            }

            if (scene != nullptr)
            {
                scene->advanceAndApply(0.0f);
            }
            else
            {
                instance->advance(0.0f);
            }

            std::cout << "      -- frame 0 (t=0) --\n";
            dumpShapes(instance.get(), dumpShapesFlag);

            for (int frame = 1; frame <= animateFrames; ++frame)
            {
                if (scene != nullptr)
                {
                    scene->advanceAndApply(animateDt);
                }
                else
                {
                    instance->advance(animateDt);
                }
                std::cout << "      -- frame " << frame << " (t="
                          << (frame * animateDt) << ") --\n";
                dumpShapes(instance.get(), false);
            }
        }
    }

    auto assets = file->assets();
    std::cout << "assets: " << assets.size() << "\n";
    for (size_t i = 0; i < assets.size(); ++i)
    {
        const auto& asset = assets[i];
        if (!asset)
        {
            std::cout << "  [" << i << "] <null>\n";
            continue;
        }
        std::cout << "  [" << i << "] " << asset->name() << " ("
                  << asset->fileExtension() << ", id=" << asset->assetId()
                  << ", file=" << asset->uniqueFilename() << ")\n";
    }

    std::cout << "view models: " << file->viewModelCount() << "\n";
    return 0;
}
