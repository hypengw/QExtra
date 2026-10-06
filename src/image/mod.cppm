module;
#include "QExtra/image.hpp"

export module qextra:image;
export import :image.service;
export import :image.network;
export import :image.playback;

export namespace qextra {
using ::qextra::AnimatedImage;
using ::qextra::Image;
using ::qextra::ImageCache;
} // namespace qextra
