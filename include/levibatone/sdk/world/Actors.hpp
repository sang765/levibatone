#pragma once

#include <levibatone/sdk/Signatures.hpp>
#include <levibatone/sdk/Types.hpp>

// Nearby-actor access, ABI verified against BedrockTools' hitbox module
// (1.26.60 arm64): FetchNearbyActorsSorted returns an ActorVec over
// DistanceSortedActor entries (16-byte stride), already sorted by distance.
namespace levibatone::sdk {

constexpr int kMobActorTypeFilter = 0x100;

struct DistanceSortedActor {
    void *mActor;
    float mDistance;
    float mPad;
};

struct ActorVec {
    DistanceSortedActor *begin;
    DistanceSortedActor *end;
    DistanceSortedActor *cap;
};

using FetchNearbyActorsFn = ActorVec (*)(void *actor, const Vec3 *extent, int actorType);
using ActorIsPlayerFn = bool (*)(void *actor);

inline FetchNearbyActorsFn fetchNearbyActors() {
    return function<FetchNearbyActorsFn>(memory::SignatureId::ActorFetchNearbyActorsSorted);
}

inline ActorIsPlayerFn actorIsPlayer() {
    return function<ActorIsPlayerFn>(memory::SignatureId::ActorIsPlayer);
}

}
