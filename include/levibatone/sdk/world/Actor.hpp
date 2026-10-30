#pragma once

#include <levibatone/sdk/Memory.hpp>
#include <levibatone/sdk/Offsets.hpp>
#include <levibatone/sdk/Types.hpp>
#include <levibatone/sdk/world/Dimension.hpp>
#include <string>

namespace levibatone::sdk {

class Level;

class Actor {
public:
    void *entityContext() {
        return reinterpret_cast<void *>(reinterpret_cast<std::uintptr_t>(this) + offsets::Actor::mEntityContext);
    }

    Level *level() const { return field<Level *>(this, offsets::Actor::mLevel); }
    Dimension *dimension() const { return field<Dimension *>(this, offsets::Actor::mDimension); }

    Vec3 position() const {
        auto *component = field<void *>(this, offsets::Actor::mStateVectorComponent);
        return component ? field<Vec3>(component, 0) : Vec3{0, 0, 0};
    }

    Vec3 velocity() const {
        auto *component = field<void *>(this, offsets::Actor::mStateVectorComponent);
        return component ? field<Vec3>(component, sizeof(Vec3) * 2) : Vec3{0, 0, 0};
    }

    Vec2 rotation() const {
        auto *component = field<void *>(this, offsets::Actor::mActorRotationComponent);
        return component ? field<Vec2>(component, 0) : Vec2{0, 0};
    }

    std::string &name() { return field<std::string>(this, offsets::Player::mName); }
};

class Player : public Actor {};

}
