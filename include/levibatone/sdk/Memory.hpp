#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
#include <utility>

// Memory access helpers modeled on the verified BedrockTools SDK layer
// (github.com/QYCottage/BedrockTools, MIT, targeting Minecraft 1.26.60 arm64).
namespace levibatone::sdk {

template <class T>
T &field(void *object, std::size_t offset) {
    return *reinterpret_cast<T *>(reinterpret_cast<std::uintptr_t>(object) + offset);
}

template <class T>
const T &field(const void *object, std::size_t offset) {
    return *reinterpret_cast<const T *>(reinterpret_cast<std::uintptr_t>(object) + offset);
}

template <class Return, class... Args>
Return virtualCall(void *instance, std::size_t index, Args &&...args) {
    auto table = *reinterpret_cast<void ***>(instance);
    auto function = reinterpret_cast<Return (*)(void *, Args...)>(table[index]);
    return function(instance, std::forward<Args>(args)...);
}

}
