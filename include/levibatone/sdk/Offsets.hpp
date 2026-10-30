#pragma once

#include <cstddef>
#include <cstdint>

namespace levibatone::sdk::offsets {

namespace VTable {
inline constexpr std::size_t ClientInstance_getRegion = 30;
inline constexpr std::size_t ClientInstanceGetLocalPlayer = 31;
}

namespace ClientInstance {
inline constexpr std::size_t mGuiData = 0x648;
}

namespace Actor {
inline constexpr std::size_t mEntityContext = 0x8;
inline constexpr std::size_t mLevel = 0x1F0;
inline constexpr std::size_t mDimension = 0x1E0;
inline constexpr std::size_t mStateVectorComponent = 0x228;
inline constexpr std::size_t mActorRotationComponent = 0x238;
inline constexpr std::size_t mCategories = 0x220;
}

namespace Dimension {
inline constexpr std::size_t mBlockSource = 208;
}

namespace GameMode {
inline constexpr std::size_t mDestroyProgress = 0x24;
}

namespace Block {
inline constexpr std::size_t mBlockType = 0x68;
}

namespace BlockType {
inline constexpr std::size_t mNameInfo = 0x88;
}

namespace NameInfo {
inline constexpr std::size_t mFullName = 0x40;
}

namespace HashedString {
inline constexpr std::size_t mString = 0x8;
}

namespace Player {
inline constexpr std::size_t mName = 0xB28;
}

}
