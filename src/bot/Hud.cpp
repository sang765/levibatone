#include "Hud.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include <pl/ModMenu.hpp>

#include "../core/GameActions.hpp"
#include "../core/GameHooks.hpp"
#include "Bot.hpp"
#include "Settings.hpp"
#include "levibatone/Version.hpp"
#include "levibatone/sdk/world/Actor.hpp"

namespace levibatone::bot {

namespace {

constexpr const char *kModuleId = "levibatone.status";
constexpr const char *kDebugModuleId = "levibatone.debug";
constexpr const char *kStopButtonId = "levibatone.stop";
constexpr const char *kCommandKey = "command";
constexpr const char *kShowKey = "show";
constexpr const char *kFontId = "levibatone.font";

bool gShow = true;
bool gDebugShow = false;
std::string gLastCommand;
sdk::Player *gDebugPlayer = nullptr;

std::vector<unsigned char> readFileBytes(const std::string &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(file),
                                      std::istreambuf_iterator<char>());
}

pl::modmenu::DrawCommand textLine(float x, float y, std::string text, std::uint32_t color) {
    pl::modmenu::DrawCommand command;
    command.type = pl::modmenu::DrawCommandType::Text;
    command.x = x;
    command.y = y;
    command.size = 10.0f;
    command.color = color;
    command.text = std::move(text);
    command.fontId = kFontId;
    return command;
}

void onToggle(std::string_view moduleId, bool enabled) {
    (void)moduleId;
    gShow = enabled;
}

void onConfigChanged(std::string_view moduleId, std::string_view key, std::string_view value) {
    (void)moduleId;
    if (key != kCommandKey) return;
    // Commands must be prefixed with "!" so a restored config value at game
    // start never auto-triggers the bot; repeated values are ignored.
    std::string text(value);
    if (text.size() < 2 || text[0] != '!') return;
    if (text == gLastCommand) return;
    gLastCommand = text;
    for (auto &line : Bot::get().executeCommand(text.substr(1))) {
        Bot::get().log(line);
    }
}

void onStopButton(std::string_view buttonId, pl::modmenu::ButtonEvent event, float value) {
    (void)buttonId;
    (void)value;
    if (event != pl::modmenu::ButtonEvent::Click) return;
    gLastCommand.clear();
    Bot::get().stop();
}

} // namespace

bool Hud::enable(const std::string &modId, const std::string &fontPath) {
    // Register the bundled Minecraft font for overlay text; the HUD renders
    // without it when the file is missing (fallback system font).
    const auto fontData = readFileBytes(fontPath);
    if (!fontData.empty()) {
        pl::modmenu::registerFont(kFontId, fontData);
    }

    if (!pl::modmenu::ModuleBuilder(kModuleId, "LeviBatone")
             .modId(modId)
             .description("Baritone-style pathfinding bot. Type commands prefixed with '!' "
                          "in the command field, e.g. '!goto 100 64 -200' or '!mine iron_ore'.")
             .defaultEnabled(true)
             .onToggle(onToggle)
             .onConfigChanged(onConfigChanged)
             .config(kShowKey, "Show HUD", pl::modmenu::ConfigType::Toggle, "true")
             .config(kCommandKey, "Command (prefix '!')", pl::modmenu::ConfigType::Text, "")
             .registerModule()) {
        return false;
    }

    pl::modmenu::ModuleBuilder(kDebugModuleId, "LeviBatone Debug")
        .modId(modId)
        .description("Live engine probe: turn-delta semantics, state vector, "
                     "actor flag bits. Used to verify offsets for elytra.")
        .defaultEnabled(false)
        .onToggle([](std::string_view, bool enabled) { gDebugShow = enabled; })
        .registerModule();

    return pl::modmenu::ButtonBuilder(kStopButtonId, "Stop Bot")
        .modId(modId)
        .moduleId(kModuleId)
        .label("STOP")
        .androidKeyCode(0)
        .behavior(pl::modmenu::ButtonBehavior::Click)
        .stylePreset(pl::modmenu::ButtonStylePreset::Accent)
        .onEvent(onStopButton)
        .registerButton();
}

void Hud::disable() {
    pl::modmenu::unregisterButton(kStopButtonId);
    pl::modmenu::unregisterModule(kModuleId);
    pl::modmenu::unregisterModule(kDebugModuleId);
}

void Hud::renderDebug(sdk::Player *player) {
    if (!gDebugShow || !player) return;

    std::vector<pl::modmenu::DrawCommand> commands;
    float y = 220.0f;
    auto line = [&](const std::string &text, std::uint32_t color) {
        commands.push_back(textLine(12.0f, y, text, color));
        y += 14.0f;
    };

    const sdk::Vec3 pos = player->position();
    const sdk::Vec2 rot = player->rotation();
    line("pos " + std::to_string(pos.x) + " " + std::to_string(pos.y) + " " + std::to_string(pos.z),
         0xFFFFFFFF);
    line("rot pitch " + std::to_string(rot.x) + " yaw " + std::to_string(rot.y), 0xFFFFFFFF);

    // State vector component: pos at +0, then two more Vec3s (old pos / velocity?).
    if (auto *component = *reinterpret_cast<void ***>(
            reinterpret_cast<std::uintptr_t>(player) + sdk::offsets::Actor::mStateVectorComponent)) {
        for (int i = 0; i < 3; ++i) {
            const sdk::Vec3 v = sdk::field<sdk::Vec3>(component, static_cast<std::size_t>(i) * 12);
            line("vec" + std::to_string(i) + " " + std::to_string(v.x) + " " + std::to_string(v.y) +
                     " " + std::to_string(v.z),
                 i == 2 ? 0xFF55FFFF : 0xFFAAAAAA);
        }
    }

    // Actor data flags (data item id 0, u64 value) — watch which bit moves.
    void *wrapper = sdk::field<void *>(player, sdk::offsets::Actor::mEntityDataProbe);
    if (wrapper) {
        void *component = sdk::field<void *>(wrapper, 0);
        if (component) {
            auto **begin = sdk::field<void **>(component, 0);
            auto **end = sdk::field<void **>(component, sizeof(void *));
            if (begin && end && end > begin) {
                const std::size_t count = static_cast<std::size_t>(end - begin);
                if (count > 0 && begin[0]) {
                    const auto item = reinterpret_cast<std::uintptr_t>(begin[0]);
                    const std::uint8_t type = sdk::field<std::uint8_t>(begin[0], 0x8);
                    const std::uint64_t flags = sdk::field<std::uint64_t>(begin[0], 0xC);
                    line("flags id0 type=" + std::to_string(static_cast<int>(type)) +
                             " items=" + std::to_string(count),
                         0xFFAAAAAA);
                    for (int half = 0; half < 2; ++half) {
                        std::string bits;
                        for (int b = 31; b >= 0; --b) {
                            bits += ((flags >> (half * 32 + b)) & 1ULL) ? '1' : '0';
                        }
                        line((half == 0 ? "f63..32 " : "f31..00 ") + bits, 0xFF55FF55);
                    }
                }
            }
        }
    }

    // Turn-delta semantics probe.
    const core::TurnObservation obs = core::lastTurnObservation();
    if (obs.valid) {
        line("turn arg dP " + std::to_string(obs.arg.x) + " dY " + std::to_string(obs.arg.y),
             0xFFFFFF55);
        line("turn diff " + std::to_string(obs.diff.x) + " " + std::to_string(obs.diff.y),
             0xFFFFFF55);
        const bool same = std::abs(obs.arg.x - obs.diff.x) < 0.01f &&
                          std::abs(obs.arg.y - obs.diff.y) < 0.01f;
        const bool sameMagnitude = std::abs(std::abs(obs.arg.x) - std::abs(obs.diff.x)) < 0.01f &&
                                   std::abs(std::abs(obs.arg.y) - std::abs(obs.diff.y)) < 0.01f;
        line(same ? "verdict: DELTA (arg == rotation change)"
             : sameMagnitude ? "verdict: ABSOLUTE-SET (sign flip?)"
                             : "verdict: unclear (move the camera by touch)",
             same ? 0xFF55FF55 : 0xFFAA5555);
    } else {
        line("turn probe: move the camera by touch", 0xFFAAAAAA);
    }

    pl::modmenu::submitDrawCommands(kDebugModuleId, commands);
}

void Hud::tick(sdk::Player *player) {
    gDebugPlayer = player;
    renderDebug(player);
    if (!gShow) return;

    Bot &bot = Bot::get();
    const auto *goal = bot.goal();
    const auto snap = bot.executorSnapshot();

    std::string status = std::string("LeviBatone ") + std::string(Version);
    status += " - " + bot.modeLabel();
    if (bot.mode() != Bot::Mode::Idle && !bot.executorHasPath() && goal) {
        status += " (finding path)";
    }

    std::string line2 = "path " + std::to_string(snap.edgeIndex) + "/" + std::to_string(snap.edgeCount);
    if (snap.breaking) line2 += " | breaking";
    if (snap.placing) line2 += " | placing";
    if (bot.mode() == Bot::Mode::Mine) {
        line2 += " | mined " + std::to_string(bot.minedCount());
        if (bot.mineRemaining() > 0) line2 += "/" + std::to_string(bot.mineRemaining());
    }

    std::vector<pl::modmenu::DrawCommand> commands;
    commands.reserve(10);
    commands.push_back(textLine(12.0f, 12.0f, status, 0xFF55FF55));
    commands.push_back(textLine(12.0f, 26.0f, line2, 0xFFFFFFFF));

    float y = 40.0f;
    for (const auto &message : bot.messages()) {
        commands.push_back(textLine(12.0f, y, message.text, 0xFFFFFF00));
        y += 14.0f;
    }

    const Settings &settings = Settings::get();
    commands.push_back(textLine(
        12.0f, y + 2.0f,
        std::string("break:") + (settings.getBool("allowBreak") ? "on" : "off") +
            " place:" + (settings.getBool("allowPlace") ? "on" : "off") +
            " sprint:" + (settings.getBool("allowSprint") ? "on" : "off"),
        0xFFAAAAAA));

    pl::modmenu::submitDrawCommands(kModuleId, commands);
}

}
