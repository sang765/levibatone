#pragma once

#include <string>

namespace levibatone::bot {

// Mod Menu surface: status HUD overlay, the command text entry, and the
// floating STOP button. Registered during enable(), removed on disable().
class Hud {
public:
    static bool enable(const std::string &modId, const std::string &fontPath);
    static void disable();
    // Called once per tick from Runtime after the bot tick; refreshes the HUD.
    static void tick();
};

}
