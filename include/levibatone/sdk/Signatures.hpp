#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace levibatone::memory {

enum class SignatureId : std::uint16_t {
    NormalTick,
    ClientInstanceUpdate,
    ClientInstanceGetLocalPlayer,
    GameModeStartDestroyBlock,
    SurvivalModeStartDestroyBlock,
    GameModeStopDestroyBlock,
    GameModeStartBuildBlock,
    SurvivalModeStartBuildBlock,
    GameModeUseItem,
    SurvivalModeUseItem,
    GameModeUseItemOn,
    SurvivalModeUseItemOn,
    GameModeAttackInternal,
    GameModeInteract,
    SurvivalModeInteract,
    BlockSourceGetBlock,
    LocalPlayerApplyTurnDelta,
    Count,
};

inline constexpr std::size_t SignatureCount = static_cast<std::size_t>(SignatureId::Count);

struct SignatureDefinition {
    SignatureId id;
    std::string_view pattern;
    std::string_view rtti{};
    std::size_t vtableSlot = 0;
};

// Patterns verified against libminecraftpe.so 1.26.60 arm64 by the BedrockTools
// project (github.com/QYCottage/BedrockTools, MIT). Resolution goes through the
// launcher preloader's pl::memory signature scanner.
bool resolveAll(std::string_view libraryName = "libminecraftpe.so");
std::uintptr_t resolve(SignatureId id);
void clear();

}
