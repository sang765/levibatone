#pragma once

#include <entt/entt.hpp>
#include <array>
#include <cstddef>
#include <cstdint>

// Movement input injection into the local player. Layout verified on
// Minecraft 1.26.60 arm64 by BedrockTools (github.com/QYCottage/BedrockTools):
// the player's EntityContext (Actor+0x8) carries an enTT registry with a
// MoveInputComponent; flag bits on the raw input state drive walk/jump/sneak.
namespace levibatone::sdk {

enum class BedrockEntityId : std::uint32_t {};

template <std::size_t N, class T>
struct BedrockBitset {
    T value{};

    bool test(std::size_t index) const {
        return (value & (static_cast<T>(1) << index)) != 0;
    }

    void set(std::size_t index, bool state) {
        const T mask = static_cast<T>(static_cast<T>(1) << index);
        value = state ? static_cast<T>(value | mask) : static_cast<T>(value & ~mask);
    }
};

struct BedrockEntityIdTraits {
    using value_type = BedrockEntityId;
    using entity_type = std::uint32_t;
    using version_type = std::uint16_t;
    static constexpr std::uint32_t entity_mask = 0x3FFFF;
    static constexpr std::uint32_t version_mask = 0x3FFF;
};

}

template <>
struct entt::entt_traits<levibatone::sdk::BedrockEntityId>
    : entt::basic_entt_traits<levibatone::sdk::BedrockEntityIdTraits> {
    static constexpr std::size_t page_size = ENTT_SPARSE_PAGE;
};

namespace levibatone::sdk {

enum class MoveInputFlag : std::size_t {
    SneakDown = 0,
    JumpDown = 7,
    SprintDown = 8,
    Up = 13,
    Down = 14,
    Left = 15,
    Right = 16
};

struct MoveInputState {
    BedrockBitset<27, std::uint32_t> mFlagValues;
    Vec2 mAnalogMoveVector;
    std::uint8_t mLookSlightDirField;
    std::uint8_t mLookNormalDirField;
    std::uint8_t mLookSmoothDirField;
    std::uint8_t mPad;
};

struct MoveInputComponent {
    MoveInputState mInputState;
    MoveInputState mRawInputState;
    std::uint8_t mHoldAutoJumpInWaterTicks;
    std::uint8_t mPad[3];
    Vec2 mMove;
    Vec2 mLookDelta;
    Vec2 mInteractDir;
    Vec3 mDisplacement;
    Vec3 mDisplacementDelta;
    Vec3 mCameraOrientation;
    BedrockBitset<11, std::uint16_t> mFlagValues;
    std::array<bool, 2> mIsPaddling;
};

class EntityContext {
public:
    entt::basic_registry<BedrockEntityId> &registry() { return mEnTTRegistry; }

    template <class T>
    T *tryGetComponent() {
        return mEnTTRegistry.try_get<T>(mEntity);
    }

    void *&mRegistry;
    entt::basic_registry<BedrockEntityId> &mEnTTRegistry;
    BedrockEntityId const mEntity;
};

inline MoveInputComponent *getMoveInputComponent(void *player) {
    if (!player) return nullptr;
    auto *context = reinterpret_cast<EntityContext *>(
        reinterpret_cast<std::uintptr_t>(player) + offsets::Actor::mEntityContext);
    return context ? context->tryGetComponent<MoveInputComponent>() : nullptr;
}

inline bool testFlag(const MoveInputState &state, MoveInputFlag flag) {
    return state.mFlagValues.test(static_cast<std::size_t>(flag));
}

inline void setFlag(MoveInputState &state, MoveInputFlag flag, bool value) {
    state.mFlagValues.set(static_cast<std::size_t>(flag), value);
}

}
