#include <osgRive/Artboard>

#include "RiveGLSupport.hpp"

#include "rive/artboard.hpp"
#include "rive/file.hpp"
#include "utils/no_op_factory.hpp"

#include <stdexcept>

namespace osgRive {

ArtboardBounds readArtboardBounds(const std::string& rivPath) {
	const auto bytes = readBinaryFile(rivPath);

	rive::NoOpFactory factory;

	auto file = rive::File::import(bytes, &factory);

	if(!file) throw std::runtime_error("Rive import failed for " + rivPath);

	auto artboard = file->artboardDefault();

	if(!artboard) throw std::runtime_error("Rive file has no default artboard: " + rivPath);

	const rive::AABB bounds = artboard->bounds();

	return {bounds.minX, bounds.minY, bounds.maxX, bounds.maxY};
}

}
