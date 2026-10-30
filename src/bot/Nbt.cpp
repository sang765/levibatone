#include "Nbt.hpp"

#include <cstring>

namespace levibatone::bot::nbt {

namespace {
constexpr std::uint8_t kTagEnd = 0;
constexpr std::uint8_t kTagByte = 1;
constexpr std::uint8_t kTagShort = 2;
constexpr std::uint8_t kTagInt = 3;
constexpr std::uint8_t kTagLong = 4;
constexpr std::uint8_t kTagFloat = 5;
constexpr std::uint8_t kTagDouble = 6;
constexpr std::uint8_t kTagByteArray = 7;
constexpr std::uint8_t kTagString = 8;
constexpr std::uint8_t kTagList = 9;
constexpr std::uint8_t kTagCompound = 10;
constexpr std::uint8_t kTagIntArray = 11;
constexpr std::uint8_t kTagLongArray = 12;
} // namespace

bool Reader::u8(std::uint8_t &out) {
    if (mPos + 1 > mSize) return false;
    out = mData[mPos++];
    return true;
}

bool Reader::i16(std::int16_t &out) {
    if (mPos + 2 > mSize) return false;
    out = static_cast<std::int16_t>(mData[mPos] | mData[mPos + 1] << 8);
    mPos += 2;
    return true;
}

bool Reader::i32(std::int32_t &out) {
    if (mPos + 4 > mSize) return false;
    out = static_cast<std::int32_t>(static_cast<std::uint32_t>(mData[mPos]) |
                                    static_cast<std::uint32_t>(mData[mPos + 1]) << 8 |
                                    static_cast<std::uint32_t>(mData[mPos + 2]) << 16 |
                                    static_cast<std::uint32_t>(mData[mPos + 3]) << 24);
    mPos += 4;
    return true;
}

bool Reader::i64(std::int64_t &out) {
    if (mPos + 8 > mSize) return false;
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) value |= static_cast<std::uint64_t>(mData[mPos + i]) << (8 * i);
    out = static_cast<std::int64_t>(value);
    mPos += 8;
    return true;
}

bool Reader::f32(float &out) {
    std::int32_t raw = 0;
    if (!i32(raw)) return false;
    std::memcpy(&out, &raw, 4);
    return true;
}

bool Reader::f64(double &out) {
    std::int64_t raw = 0;
    if (!i64(raw)) return false;
    std::memcpy(&out, &raw, 8);
    return true;
}

bool Reader::bytes(std::vector<std::uint8_t> &out, std::size_t count) {
    if (mPos + count > mSize) return false;
    out.assign(mData + mPos, mData + mPos + count);
    mPos += count;
    return true;
}

bool Reader::string(std::string &out) {
    std::int16_t length = 0;
    if (!i16(length) || length < 0) return false;
    if (mPos + static_cast<std::size_t>(length) > mSize) return false;
    out.assign(reinterpret_cast<const char *>(mData + mPos), static_cast<std::size_t>(length));
    mPos += static_cast<std::size_t>(length);
    return true;
}

bool Reader::readPayload(std::uint8_t type, Value &out) {
    switch (type) {
    case kTagByte: {
        std::uint8_t v = 0;
        if (!u8(v)) return false;
        out.data = static_cast<std::int8_t>(v);
        return true;
    }
    case kTagShort: {
        std::int16_t v = 0;
        if (!i16(v)) return false;
        out.data = v;
        return true;
    }
    case kTagInt: {
        std::int32_t v = 0;
        if (!i32(v)) return false;
        out.data = v;
        return true;
    }
    case kTagLong: {
        std::int64_t v = 0;
        if (!i64(v)) return false;
        out.data = v;
        return true;
    }
    case kTagFloat: {
        float v = 0;
        if (!f32(v)) return false;
        out.data = v;
        return true;
    }
    case kTagDouble: {
        double v = 0;
        if (!f64(v)) return false;
        out.data = v;
        return true;
    }
    case kTagString: {
        std::string v;
        if (!string(v)) return false;
        out.data = std::move(v);
        return true;
    }
    case kTagByteArray: {
        std::int32_t count = 0;
        if (!i32(count) || count < 0) return false;
        std::vector<std::uint8_t> v;
        if (!bytes(v, static_cast<std::size_t>(count))) return false;
        List list;
        list.reserve(v.size());
        for (std::uint8_t b : v) list.push_back(Value{static_cast<std::int8_t>(b)});
        out.data = std::move(list);
        return true;
    }
    case kTagIntArray:
    case kTagLongArray: {
        std::int32_t count = 0;
        if (!i32(count) || count < 0) return false;
        List list;
        list.reserve(static_cast<std::size_t>(count));
        for (std::int32_t i = 0; i < count; ++i) {
            Value element;
            if (type == kTagIntArray) {
                std::int32_t v = 0;
                if (!i32(v)) return false;
                element.data = v;
            } else {
                std::int64_t v = 0;
                if (!i64(v)) return false;
                element.data = v;
            }
            list.push_back(std::move(element));
        }
        out.data = std::move(list);
        return true;
    }
    case kTagList: {
        std::uint8_t elementType = 0;
        std::int32_t count = 0;
        if (!u8(elementType) || !i32(count) || count < 0) return false;
        List list;
        list.reserve(static_cast<std::size_t>(count));
        for (std::int32_t i = 0; i < count; ++i) {
            Value element;
            if (elementType == kTagCompound) {
                Compound compound;
                while (true) {
                    std::uint8_t childType = 0;
                    if (!u8(childType)) return false;
                    if (childType == kTagEnd) break;
                    std::string name;
                    if (!string(name)) return false;
                    Value child;
                    if (!readPayload(childType, child)) return false;
                    compound.emplace(std::move(name), std::move(child));
                }
                element.data = std::move(compound);
            } else if (!readPayload(elementType, element)) {
                return false;
            }
            list.push_back(std::move(element));
        }
        out.data = std::move(list);
        return true;
    }
    case kTagCompound: {
        Compound compound;
        while (true) {
            std::uint8_t childType = 0;
            if (!u8(childType)) return false;
            if (childType == kTagEnd) break;
            std::string name;
            if (!string(name)) return false;
            Value child;
            if (!readPayload(childType, child)) return false;
            compound.emplace(std::move(name), std::move(child));
        }
        out.data = std::move(compound);
        return true;
    }
    default:
        return false;
    }
}

bool Reader::readTag(std::uint8_t type, Value &out) {
    return readPayload(type, out);
}

bool Reader::readRoot(Value &out) {
    std::uint8_t type = 0;
    if (!u8(type) || type != kTagCompound) return false;
    std::string name;
    if (!string(name)) return false;
    return readPayload(kTagCompound, out);
}

bool parseMcstructure(const std::uint8_t *data, std::size_t size, Value &root) {
    Reader reader(data, size);
    return reader.readRoot(root);
}

}
