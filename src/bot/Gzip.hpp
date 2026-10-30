#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Gzip decompression via Android/Termux zlib. Returns false on malformed input.
namespace levibatone::bot::gzip {

bool inflate(const std::uint8_t *data, std::size_t size, std::vector<std::uint8_t> &out);

}
