#include "Runtime.hpp"

#include <string>

#include <pl/Mod.hpp>

#include "../bot/Bot.hpp"
#include "../bot/Hud.hpp"
#include "GameActions.hpp"
#include "GameHooks.hpp"
#include "levibatone/sdk/Functions.hpp"
#include "levibatone/Version.hpp"
#include "levibatone/sdk/Signatures.hpp"
#include "levibatone/sdk/world/BlockSource.hpp"

namespace levibatone::core {

namespace {

ll::mod::NativeMod *self = nullptr;

std::string missingSignatures() {
    std::string missing;
    static const memory::SignatureId kRequired[] = {
        memory::SignatureId::NormalTick,
        memory::SignatureId::ClientInstanceUpdate,
        memory::SignatureId::ClientInstanceGetLocalPlayer,
        memory::SignatureId::BlockSourceGetBlock,
        memory::SignatureId::GameModeStartDestroyBlock,
        memory::SignatureId::SurvivalModeStartDestroyBlock,
        memory::SignatureId::GameModeStartBuildBlock,
        memory::SignatureId::SurvivalModeStartBuildBlock,
        memory::SignatureId::LocalPlayerApplyTurnDelta,
    };
    for (const auto id : kRequired) {
        if (memory::resolve(id) != 0) continue;
        if (!missing.empty()) missing += ", ";
        missing += std::to_string(static_cast<int>(id));
    }
    return missing;
}

} // namespace

bool load() {
    self = ll::mod::NativeMod::current();
    if (self) {
        self->getLogger().info("LeviBatone {} loaded", std::string(Version));
    }
    return true;
}

bool enable() {
    if (!memory::resolveAll()) {
        if (self) self->getLogger().error("signature resolution failed entirely; is this Minecraft 1.26.60?");
        return false;
    }

    const std::string missing = missingSignatures();
    if (!missing.empty()) {
        if (self) self->getLogger().warn("some signatures unresolved (ids: {}); affected features disabled", missing);
    }

    // Wire SDK function pointers.
    sdk::blockSourceGetBlock = sdk::function<sdk::BlockSourceGetBlockFn>(memory::SignatureId::BlockSourceGetBlock);
    actions.applyTurnDelta =
        sdk::function<GameActions::TurnDeltaFn>(memory::SignatureId::LocalPlayerApplyTurnDelta);

    if (!installHooks()) {
        if (self) self->getLogger().error("could not install the player tick hook; the bot cannot run");
        return false;
    }

    if (self) {
        const std::string fontPath = (self->getResourceDir() / "minecraft.ttf").string();
        if (!bot::Hud::enable(self->getId(), fontPath)) {
            self->getLogger().warn("mod menu registration failed; HUD unavailable");
        }
    }

    if (self) self->getLogger().info("LeviBatone enabled");
    return true;
}

bool disable() {
    bot::Bot::get().stop();
    bot::Bot::get().onWorldLeft();
    bot::Hud::disable();
    uninstallHooks();
    memory::clear();
    sdk::blockSourceGetBlock = nullptr;
    actions.applyTurnDelta = nullptr;
    actions.startDestroy.store(nullptr, std::memory_order_release);
    actions.stopDestroy.store(nullptr, std::memory_order_release);
    actions.startBuild.store(nullptr, std::memory_order_release);
    actions.gameMode.store(nullptr, std::memory_order_release);
    if (self) self->getLogger().info("LeviBatone disabled");
    return true;
}

bool unload() {
    self = nullptr;
    return true;
}

}
