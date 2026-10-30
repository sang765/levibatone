#include "Gzip.hpp"

#include <zlib.h>

namespace levibatone::bot::gzip {

bool inflate(const std::uint8_t *data, std::size_t size, std::vector<std::uint8_t> &out) {
    if (!data || size < 2) return false;

    z_stream stream{};
    // 15 + 32: auto-detect gzip/zlib wrapper.
    if (inflateInit2(&stream, 15 + 32) != Z_OK) return false;

    out.clear();
    out.reserve(size * 4);
    stream.next_in = const_cast<std::uint8_t *>(data);
    stream.avail_in = static_cast<uInt>(size);

    constexpr std::size_t kMaxOutput = 64u << 20; // 64 MiB cap against zip bombs
    std::vector<std::uint8_t> chunk(65536);
    int status = Z_OK;
    do {
        stream.next_out = chunk.data();
        stream.avail_out = static_cast<uInt>(chunk.size());
        status = inflate(&stream, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END && status != Z_BUF_ERROR) {
            inflateEnd(&stream);
            return false;
        }
        out.insert(out.end(), chunk.data(), chunk.data() + (chunk.size() - stream.avail_out));
        if (out.size() > kMaxOutput) {
            inflateEnd(&stream);
            return false;
        }
        // Truncated or corrupt stream: no input left and no end marker.
        if (status == Z_BUF_ERROR && stream.avail_in == 0) {
            inflateEnd(&stream);
            return false;
        }
    } while (status != Z_STREAM_END);

    inflateEnd(&stream);
    return status == Z_STREAM_END;
}

}
