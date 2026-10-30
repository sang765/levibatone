#include <pl/Mod.hpp>

#include "core/Runtime.hpp"

namespace {
class LeviBatoneMod {
public:
    bool load() { return levibatone::core::load(); }
    bool enable() { return levibatone::core::enable(); }
    bool disable() { return levibatone::core::disable(); }
    bool unload() { return levibatone::core::unload(); }
};

LeviBatoneMod &modInstance() {
    static LeviBatoneMod instance;
    return instance;
}
} // namespace

PL_REGISTER_MOD(LeviBatoneMod, modInstance())
