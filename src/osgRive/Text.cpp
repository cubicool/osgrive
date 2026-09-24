#include <osgRive/Text>

#include "RiveGLSupport.hpp"

#include "rive/factory.hpp"
#include "rive/math/mat2d.hpp"
#include "rive/math/raw_path.hpp"
#include "rive/renderer.hpp"
#include "rive/text/utf.hpp"
#include "rive/text_engine.hpp"

#include <cstdio>
#include <memory>
#include <vector>

namespace osgRive {

struct TextDraw::State {
	std::vector<uint8_t> fontBytes;
	std::vector<rive::Unichar> unichars;

	uint32_t color = 0xFFFFFFFF;

	Affine transform;
	bool visible = true;

	rive::rcp<rive::RenderPath> path;
	rive::rcp<rive::RenderPaint> paint;

	bool built = false;
};

// Shapes the text at size 1 (em units) and builds the one RenderPath every later draw reuses.
void TextDraw::build(State& state, rive::Factory& factory) {
	auto font = factory.decodeFont(rive::Span<const uint8_t>(
		state.fontBytes.data(),
		state.fontBytes.size()
	));

	if(!font) {
		std::fprintf(stderr, "osgRive::TextDraw: Rive could not decode the font\n");

		return;
	}

	// script/level are placeholders: shapeText() resolves both per character itself.
	const rive::TextRun run = {
		font,
		1.0f,
		-1.0f,
		0.0f,
		static_cast<uint32_t>(state.unichars.size()),
		0,
		0,
		0
	};

	const auto paragraphs = font->shapeText(
		rive::Span<const rive::Unichar>(state.unichars.data(), state.unichars.size()),
		rive::Span<const rive::TextRun>(&run, 1)
	);

	rive::RawPath rawPath;

	// Same per-glyph placement as rive::RawText::render(), at size 1: getPath() is already in em
	// units (y down), so each glyph only needs its pen-position translation.
	for(const auto& paragraph : paragraphs) {
		for(const auto& glyphRun : paragraph.runs) {
			for(std::size_t i = 0; i < glyphRun.glyphs.size(); i++) {
				const rive::Vec2D& offset = glyphRun.offsets[i];
				const rive::Mat2D transform(
					glyphRun.size,
					0.0f,
					0.0f,
					glyphRun.size,
					glyphRun.xpos[i] + offset.x,
					offset.y
				);

				rawPath.addPath(glyphRun.font->getPath(glyphRun.glyphs[i]), &transform);
			}
		}
	}

	state.path = factory.makeRenderPath(rawPath, rive::FillRule::nonZero);
	state.paint = factory.makeRenderPaint();
	state.paint->style(rive::RenderPaintStyle::fill);
	state.paint->color(state.color);
}

TextDraw::TextDraw(const std::string& fontPath, const std::string& text, uint32_t color):
m_state(std::make_shared<State>()) {
	m_state->fontBytes = readBinaryFile(fontPath);
	m_state->color = color;

	const auto* utf8 = reinterpret_cast<const uint8_t*>(text.data());
	const auto* end = utf8 + text.size();

	while(utf8 < end) m_state->unichars.push_back(rive::UTF::NextUTF8(&utf8));
}

TextDraw::~TextDraw() = default;

void TextDraw::setTransform(const Affine& transform) { m_state->transform = transform; }

Affine TextDraw::getTransform() const { return m_state->transform; }

void TextDraw::setVisible(bool visible) { m_state->visible = visible; }

bool TextDraw::getVisible() const { return m_state->visible; }

DrawFunction TextDraw::drawFunction() const {
	return [state = m_state](rive::Factory& factory, rive::Renderer& renderer, float) {
		if(!state->built) {
			state->built = true;

			build(*state, factory);
		}

		if(!state->path || !state->visible) return;

		const Affine& t = state->transform;

		renderer.save();
		renderer.transform(rive::Mat2D(t.xAxisX, t.xAxisY, t.yAxisX, t.yAxisY, t.tx, t.ty));
		renderer.drawPath(state->path.get(), state->paint.get());
		renderer.restore();
	};
}

DrawFunction makeTextDraw(
	const std::string& fontPath,
	const std::string& text,
	float pixelsPerEm,
	float originX,
	float originY,
	uint32_t color
) {
	TextDraw textDraw(fontPath, text, color);

	textDraw.setTransform({pixelsPerEm, 0.0f, 0.0f, pixelsPerEm, originX, originY});

	return textDraw.drawFunction();
}

}
