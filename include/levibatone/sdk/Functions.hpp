#pragma once

#include <levibatone/sdk/Signatures.hpp>

namespace levibatone::sdk {

template <class Function>
Function function(memory::SignatureId id) {
    const auto address = memory::resolve(id);
    return address ? reinterpret_cast<Function>(address) : nullptr;
}

}
