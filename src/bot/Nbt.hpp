#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

// Minimal little-endian NBT reader, sufficient for .mcstructure files
// (uncompressed BED NBT: root compound, no name on the root payload).
namespace levibatone::bot::nbt {

struct Value;
using List = std::vector<Value>;
using Compound = std::unordered_map<std::string, Value>;

struct Value {
    std::variant<std::monostate, std::int8_t, std::int16_t, std::int32_t, std::int64_t, float,
                 double, std::string, List, Compound>
        data;

    bool isCompound() const { return std::holds_alternative<Compound>(data); }
    bool isList() const { return std::holds_alternative<List>(data); }
    bool isString() const { return std::holds_alternative<std::string>(data); }
    bool isInt() const { return std::holds_alternative<std::int32_t>(data); }

    const Compound *asCompound() const {
        const auto *v = std::get_if<Compound>(&data);
        return v ? v : nullptr;
    }
    const List *asList() const {
        const auto *v = std::get_if<List>(&data);
        return v ? v : nullptr;
    }
    const std::string *asString() const {
        const auto *v = std::get_if<std::string>(&data);
        return v ? v : nullptr;
    }
    std::int32_t asInt(std::int32_t fallback = 0) const {
        if (const auto *v = std::get_if<std::int32_t>(&data)) return *v;
        if (const auto *v = std::get_if<std::int8_t>(&data)) return *v;
        if (const auto *v = std::get_if<std::int16_t>(&data)) return *v;
        return fallback;
    }
};

class Reader {
public:
    Reader(const std::uint8_t *data, std::size_t size) : mData(data), mSize(size) {}

    bool readTag(std::uint8_t type, Value &out);
    // Reads the named root compound: 1 byte type + name, then payload.
    bool readRoot(Value &out);

private:
    const std::uint8_t *mData;
    std::size_t mSize;
    std::size_t mPos = 0;

    bool u8(std::uint8_t &out);
    bool i16(std::int16_t &out);
    bool i32(std::int32_t &out);
    bool i64(std::int64_t &out);
    bool f32(float &out);
    bool f64(double &out);
    bool bytes(std::vector<std::uint8_t> &out, std::size_t count);
    bool string(std::string &out);
    bool readPayload(std::uint8_t type, Value &out);
};

// Parses one .mcstructure; returns the root compound or nullptr on failure.
// Caller owns the storage via the out parameter.
bool parseMcstructure(const std::uint8_t *data, std::size_t size, Value &root);

}
