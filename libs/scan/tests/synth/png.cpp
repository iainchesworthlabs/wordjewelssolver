// SPDX-License-Identifier: GPL-3.0-or-later
#include "png.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace wjs::scan::synth {

namespace {

std::uint32_t crc32(const std::vector<std::uint8_t>& bytes) {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1U) != 0U ? 0xEDB88320U ^ (c >> 1U) : c >> 1U;
            }
            t[n] = c;
        }
        return t;
    }();
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const std::uint8_t b : bytes) {
        crc = table[(crc ^ b) & 0xFFU] ^ (crc >> 8U);
    }
    return crc ^ 0xFFFFFFFFU;
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 24U));
    out.push_back(static_cast<std::uint8_t>(v >> 16U));
    out.push_back(static_cast<std::uint8_t>(v >> 8U));
    out.push_back(static_cast<std::uint8_t>(v));
}

void chunk(std::vector<std::uint8_t>& out, const char* type,
           const std::vector<std::uint8_t>& data) {
    put32(out, static_cast<std::uint32_t>(data.size()));
    std::vector<std::uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    out.insert(out.end(), body.begin(), body.end());
    put32(out, crc32(body));
}

}  // namespace

bool write_png(const std::filesystem::path& path, const GrayImage& image) {
    // The scanlines, each with a filter byte of 0.
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(image.height) *
                (static_cast<std::size_t>(image.width) + 1));
    for (int y = 0; y < image.height; ++y) {
        raw.push_back(0);
        const auto first = image.pixels.begin() + static_cast<std::ptrdiff_t>(image.index(0, y));
        raw.insert(raw.end(), first, first + image.width);
    }

    // zlib: a header, stored blocks of at most 65535 bytes, and the Adler-32.
    std::vector<std::uint8_t> z = {0x78, 0x01};
    std::size_t at = 0;
    do {
        const std::size_t n = std::min<std::size_t>(65535, raw.size() - at);
        const bool last = at + n == raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<std::uint8_t>(n & 0xFFU));
        z.push_back(static_cast<std::uint8_t>(n >> 8U));
        z.push_back(static_cast<std::uint8_t>(~n & 0xFFU));
        z.push_back(static_cast<std::uint8_t>((~n >> 8U) & 0xFFU));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(at),
                 raw.begin() + static_cast<std::ptrdiff_t>(at + n));
        at += n;
    } while (at < raw.size());
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (const std::uint8_t byte : raw) {
        a = (a + byte) % 65521U;
        b = (b + a) % 65521U;
    }
    put32(z, (b << 16U) | a);

    std::vector<std::uint8_t> file = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<std::uint8_t> header;
    put32(header, static_cast<std::uint32_t>(image.width));
    put32(header, static_cast<std::uint32_t>(image.height));
    header.insert(header.end(), {8, 0, 0, 0, 0});  // 8 bits, grey, deflate, filter 0, no interlace
    chunk(file, "IHDR", header);
    chunk(file, "IDAT", z);
    chunk(file, "IEND", {});

    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(file.data()),
                 static_cast<std::streamsize>(file.size()));
    return stream.good();
}

namespace {

// The next field of a PGM header: white space and comments skipped, then the characters
// up to the next white space. Empty at the end of the file.
std::string next_field(std::istream& stream) {
    std::string field;
    for (int c = stream.peek(); c != std::char_traits<char>::eof(); c = stream.peek()) {
        if (c == '#') {
            std::string comment;
            std::getline(stream, comment);
        } else if (std::isspace(c) != 0) {
            stream.get();
        } else {
            break;
        }
    }
    for (int c = stream.peek();
         c != std::char_traits<char>::eof() && std::isspace(c) == 0 && c != '#';
         c = stream.peek()) {
        field.push_back(static_cast<char>(stream.get()));
    }
    return field;
}

std::optional<int> as_int(const std::string& field) {
    int value = 0;
    const char* end = field.data() + field.size();
    const auto [stop, error] = std::from_chars(field.data(), end, value);
    if (error != std::errc{} || stop != end) {
        return std::nullopt;
    }
    return value;
}

}  // namespace

bool write_pgm(const std::filesystem::path& path, const GrayImage& image) {
    std::ofstream stream(path, std::ios::binary);
    stream << "P5\n" << image.width << ' ' << image.height << "\n255\n";
    stream.write(reinterpret_cast<const char*>(image.pixels.data()),
                 static_cast<std::streamsize>(image.pixels.size()));
    return stream.good();
}

GrayImage read_pgm(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (next_field(stream) != "P5") {
        return {};
    }
    const std::optional<int> width = as_int(next_field(stream));
    const std::optional<int> height = as_int(next_field(stream));
    const std::optional<int> maxval = as_int(next_field(stream));
    constexpr int kMaxSide = 1 << 16;
    if (!width || !height || !maxval || *maxval != 255 || *width <= 0 || *height <= 0 ||
        *width > kMaxSide || *height > kMaxSide) {
        return {};
    }
    stream.get();  // the one white space after maxval
    const auto count = static_cast<std::uintmax_t>(*width) * static_cast<std::uintmax_t>(*height);
    std::error_code ignored;
    const std::uintmax_t size = std::filesystem::file_size(path, ignored);
    const auto offset = static_cast<std::uintmax_t>(std::max<std::streamoff>(stream.tellg(), 0));
    if (ignored || size < offset + count) {
        return {};  // cut short: do not make a picture for it to be read into
    }
    GrayImage image(*width, *height);
    stream.read(reinterpret_cast<char*>(image.pixels.data()),
                static_cast<std::streamsize>(image.pixels.size()));
    if (stream.gcount() != static_cast<std::streamsize>(image.pixels.size())) {
        return {};
    }
    return image;
}

}  // namespace wjs::scan::synth
