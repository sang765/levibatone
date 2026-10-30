#include "Hud.hpp"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include <pl/ModMenu.hpp>

#include "Bot.hpp"
#include "Settings.hpp"
#include "levibatone/Version.hpp"

namespace levibatone::bot {

namespace {

constexpr const char *kModuleId = "levibatone.status";
constexpr const char *kStopButtonId = "levibatone.stop";
constexpr const char *kCommandKey = "command";
constexpr const char *kShowKey = "show";
constexpr const char *kFontId = "levibatone.font";

bool gShow = true;
std::string gLastCommand;

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
}

void Hud::tick() {
    if (!gShow) return;

    Bot &bot = Bot::get();
    const auto *goal = bot.goal();
    const auto snap = bot.executorSnapshot();

    std::string status = std::string("LeviBatone ") + std::string(Version);
    status += " - " + bot.modeLabel();
    if (bot.mode() != Bot::Mode::Idle && !snap.waitingForPath && !bot.executorHasPath() && goal) {
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
