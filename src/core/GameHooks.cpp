#include "GameHooks.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <mutex>

#include <pl/memory/Hook.hpp>

#include "../bot/Bot.hpp"
#include "GameActions.hpp"
#include "levibatone/sdk/Signatures.hpp"
#include "levibatone/sdk/client/ClientInstance.hpp"
#include "levibatone/sdk/world/Actor.hpp"
#include "../bot/Hud.hpp"

namespace levibatone::core {

GameActions actions;

namespace {

using NormalTickFn = void (*)(void *);
using ClientInstanceUpdateFn = void *(*)(void *, bool);
using DestroyFn = bool (*)(void *, const void *, std::uint8_t, bool *);
using StopDestroyFn = void (*)(void *, const void *);
using BuildFn = void (*)(void *, const void *, std::uint8_t, std::uint32_t);
using UseItemFn = bool (*)(void *, void *, std::uint32_t);
using UseItemOnFn = std::uint8_t (*)(void *, void *, const void *, std::uint8_t, const void *,
                                     std::uint32_t, const void *, bool);
using AttackFn = bool (*)(void *, void *, bool, const void *);
using InteractFn = bool (*)(void *, void *, const void *, std::uint32_t);
using TurnDeltaFn = void (*)(sdk::Actor *, const sdk::Vec2 &);

// Function pointer type erased for GameActions storage.
NormalTickFn tickOriginal = nullptr;
ClientInstanceUpdateFn clientUpdateOriginal = nullptr;
DestroyFn gameModeStartDestroyOriginal = nullptr;
DestroyFn survivalModeStartDestroyOriginal = nullptr;
StopDestroyFn gameModeStopDestroyOriginal = nullptr;
BuildFn gameModeStartBuildOriginal = nullptr;
BuildFn survivalModeStartBuildOriginal = nullptr;
UseItemFn gameModeUseItemOriginal = nullptr;
UseItemFn survivalModeUseItemOriginal = nullptr;
UseItemOnFn gameModeUseItemOnOriginal = nullptr;
UseItemOnFn survivalModeUseItemOnOriginal = nullptr;
AttackFn gameModeAttackOriginal = nullptr;
InteractFn gameModeInteractOriginal = nullptr;
InteractFn survivalModeInteractOriginal = nullptr;

// Signature ids paired with each hook, in the same order as kHookTargets.
constexpr memory::SignatureId kHookTargets[] = {
    memory::SignatureId::LocalPlayerApplyTurnDelta,
    memory::SignatureId::NormalTick,
    memory::SignatureId::ClientInstanceUpdate,
    memory::SignatureId::GameModeStartDestroyBlock,
    memory::SignatureId::SurvivalModeStartDestroyBlock,
    memory::SignatureId::GameModeStopDestroyBlock,
    memory::SignatureId::GameModeStartBuildBlock,
    memory::SignatureId::SurvivalModeStartBuildBlock,
    memory::SignatureId::GameModeUseItem,
    memory::SignatureId::SurvivalModeUseItem,
    memory::SignatureId::GameModeUseItemOn,
    memory::SignatureId::SurvivalModeUseItemOn,
    memory::SignatureId::GameModeAttackInternal,
    memory::SignatureId::GameModeInteract,
    memory::SignatureId::SurvivalModeInteract,
};

std::atomic<void *> sClientInstance{nullptr};

void tickDetour(void *actor) {
    levibatone::bot::Bot::get().onTick(reinterpret_cast<sdk::Player *>(actor));
    if (tickOriginal) tickOriginal(actor);
    levibatone::bot::Hud::tick(reinterpret_cast<sdk::Player *>(actor));
}

void *clientUpdateDetour(void *clientInstance, bool value) {
    const void *result = clientUpdateOriginal ? clientUpdateOriginal(clientInstance, value) : nullptr;
    // Fallback tick: when NormalTick could not be hooked, drive the bot from
    // the client update using the local player fetched through the client.
    if (!tickOriginal && clientInstance) {
        auto *client = reinterpret_cast<sdk::ClientInstance *>(clientInstance);
        if (sdk::Player *player = client->localPlayer()) {
            levibatone::bot::Bot::get().onTick(player);
        }
    }
    return const_cast<void *>(result);
}

// Interaction detours only capture the GameMode instance and the matching
// original function; behaviour is otherwise pass-through.
bool gameModeStartDestroyDetour(void *gameMode, const void *pos, std::uint8_t face, bool *destroyed) {
    actions.gameMode.store(gameMode, std::memory_order_release);
    actions.startDestroy.store(gameModeStartDestroyOriginal, std::memory_order_release);
    return gameModeStartDestroyOriginal(gameMode, pos, face, destroyed);
}

bool survivalModeStartDestroyDetour(void *gameMode, const void *pos, std::uint8_t face, bool *destroyed) {
    actions.gameMode.store(gameMode, std::memory_order_release);
    actions.startDestroy.store(survivalModeStartDestroyOriginal, std::memory_order_release);
    return survivalModeStartDestroyOriginal(gameMode, pos, face, destroyed);
}

void gameModeStopDestroyDetour(void *gameMode, const void *pos) {
    actions.gameMode.store(gameMode, std::memory_order_release);
    actions.stopDestroy.store(gameModeStopDestroyOriginal, std::memory_order_release);
    gameModeStopDestroyOriginal(gameMode, pos);
}

void gameModeStartBuildDetour(void *gameMode, const void *pos, std::uint8_t face, std::uint32_t mode) {
    actions.gameMode.store(gameMode, std::memory_order_release);
    actions.startBuild.store(gameModeStartBuildOriginal, std::memory_order_release);
    gameModeStartBuildOriginal(gameMode, pos, face, mode);
}

void survivalModeStartBuildDetour(void *gameMode, const void *pos, std::uint8_t face, std::uint32_t mode) {
    actions.gameMode.store(gameMode, std::memory_order_release);
    actions.startBuild.store(survivalModeStartBuildOriginal, std::memory_order_release);
    survivalModeStartBuildOriginal(gameMode, pos, face, mode);
}

bool gameModeUseItemDetour(void *gameMode, void *item, std::uint32_t mode) {
    actions.gameMode.store(gameMode, std::memory_order_release);
    return gameModeUseItemOriginal(gameMode, item, mode);
}

bool survivalModeUseItemDetour(void *gameMode, void *item, std::uint32_t mode) {
    actions.gameMode.store(gameMode, std::memory_order_release);
    return survivalModeUseItemOriginal(gameMode, item, mode);
}

std::uint8_t gameModeUseItemOnDetour(void *gameMode, void *item, const void *pos, std::uint8_t face,
                                     const void *hit, std::uint32_t mode, const void *block,
                                     bool firstEvent) {
    actions.gameMode.store(gameMode, std::memory_order_release);
    return gameModeUseItemOnOriginal(gameMode, item, pos, face, hit, mode, block, firstEvent);
}

std::uint8_t survivalModeUseItemOnDetour(void *gameMode, void *item, const void *pos, std::uint8_t face,
                                         const void *hit, std::uint32_t mode, const void *block,
                                         bool firstEvent) {
    actions.gameMode.store(gameMode, std::memory_order_release);
    return survivalModeUseItemOnOriginal(gameMode, item, pos, face, hit, mode, block, firstEvent);
}

bool gameModeAttackDetour(void *gameMode, void *target, bool predictive, const void *hitPosition) {
    actions.gameMode.store(gameMode, std::memory_order_release);
    return gameModeAttackOriginal(gameMode, target, predictive, hitPosition);
}

bool gameModeInteractDetour(void *gameMode, void *target, const void *pos, std::uint32_t mode) {
    actions.gameMode.store(gameMode, std::memory_order_release);
    return gameModeInteractOriginal(gameMode, target, pos, mode);
}

bool survivalModeInteractDetour(void *gameMode, void *target, const void *pos, std::uint32_t mode) {
    actions.gameMode.store(gameMode, std::memory_order_release);
    return survivalModeInteractOriginal(gameMode, target, pos, mode);
}

TurnDeltaFn turnDeltaOriginal = nullptr;
std::mutex gTurnMutex;
TurnObservation gTurnObservation;

void turnDeltaDetour(sdk::Actor *actor, const sdk::Vec2 &arg) {
    if (actor) {
        const sdk::Vec2 before = actor->rotation();
        if (turnDeltaOriginal) turnDeltaOriginal(actor, arg);
        const sdk::Vec2 after = actor->rotation();
        std::lock_guard lock(gTurnMutex);
        gTurnObservation.valid = true;
        gTurnObservation.arg = arg;
        gTurnObservation.diff = {after.x - before.x, after.y - before.y};
        return;
    }
    if (turnDeltaOriginal) turnDeltaOriginal(actor, arg);
}

void *detourFor(const std::size_t index) {
    switch (index) {
    case 0: return reinterpret_cast<void *>(&turnDeltaDetour);
    case 1: return reinterpret_cast<void *>(&tickDetour);
    case 2: return reinterpret_cast<void *>(&clientUpdateDetour);
    case 3: return reinterpret_cast<void *>(&gameModeStartDestroyDetour);
    case 4: return reinterpret_cast<void *>(&survivalModeStartDestroyDetour);
    case 5: return reinterpret_cast<void *>(&gameModeStopDestroyDetour);
    case 6: return reinterpret_cast<void *>(&gameModeStartBuildDetour);
    case 7: return reinterpret_cast<void *>(&survivalModeStartBuildDetour);
    case 8: return reinterpret_cast<void *>(&gameModeUseItemDetour);
    case 9: return reinterpret_cast<void *>(&survivalModeUseItemDetour);
    case 10: return reinterpret_cast<void *>(&gameModeUseItemOnDetour);
    case 11: return reinterpret_cast<void *>(&survivalModeUseItemOnDetour);
    case 12: return reinterpret_cast<void *>(&gameModeAttackDetour);
    case 13: return reinterpret_cast<void *>(&gameModeInteractDetour);
    case 14: return reinterpret_cast<void *>(&survivalModeInteractDetour);
    }
    return nullptr;
}

void **originalFor(const std::size_t index) {
    switch (index) {
    case 0: return reinterpret_cast<void **>(&turnDeltaOriginal);
    case 1: return reinterpret_cast<void **>(&tickOriginal);
    case 2: return reinterpret_cast<void **>(&clientUpdateOriginal);
    case 3: return reinterpret_cast<void **>(&gameModeStartDestroyOriginal);
    case 4: return reinterpret_cast<void **>(&survivalModeStartDestroyOriginal);
    case 5: return reinterpret_cast<void **>(&gameModeStopDestroyOriginal);
    case 6: return reinterpret_cast<void **>(&gameModeStartBuildOriginal);
    case 7: return reinterpret_cast<void **>(&survivalModeStartBuildOriginal);
    case 8: return reinterpret_cast<void **>(&gameModeUseItemOriginal);
    case 9: return reinterpret_cast<void **>(&survivalModeUseItemOriginal);
    case 10: return reinterpret_cast<void **>(&gameModeUseItemOnOriginal);
    case 11: return reinterpret_cast<void **>(&survivalModeUseItemOnOriginal);
    case 12: return reinterpret_cast<void **>(&gameModeAttackOriginal);
    case 13: return reinterpret_cast<void **>(&gameModeInteractOriginal);
    case 14: return reinterpret_cast<void **>(&survivalModeInteractOriginal);
    }
    return nullptr;
}

constexpr std::size_t kHookCount = std::size(kHookTargets);
std::array<void *, kHookCount> kHookHandles{};
std::mutex gInstallMutex;
bool gInstalled = false;

} // namespace

bool installHooks() {
    std::lock_guard lock(gInstallMutex);
    if (gInstalled) return true;

    bool tickHooked = false;
    bool clientHooked = false;
    for (std::size_t i = 0; i < kHookCount; ++i) {
        const auto address = memory::resolve(kHookTargets[i]);
        if (!address) continue;
        void *detour = detourFor(i);
        void **original = originalFor(i);
        if (!detour || !original) continue;
        if (pl::memory::hook(reinterpret_cast<void *>(address), detour, original) != 0) continue;
        kHookHandles[i] = detour;
        if (i == 1) tickHooked = true;
        if (i == 2) clientHooked = true;
    }

    // The tick hook is mandatory; interaction hooks are best-effort (they only
    // supply the GameMode instance for block breaking/placing).
    gInstalled = tickHooked;
    return gInstalled;
}

TurnObservation lastTurnObservation() {
    std::lock_guard lock(gTurnMutex);
    return gTurnObservation;
}

void uninstallHooks() {
    std::lock_guard lock(gInstallMutex);
    if (!gInstalled) return;
    for (std::size_t i = 0; i < kHookCount; ++i) {
        if (!kHookHandles[i]) continue;
        const auto address = memory::resolve(kHookTargets[i]);
        if (address) {
            pl::memory::unhook(reinterpret_cast<void *>(address), kHookHandles[i]);
        }
        kHookHandles[i] = nullptr;
    }
    gInstalled = false;
}

}
