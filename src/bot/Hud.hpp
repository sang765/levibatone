#pragma once

#include <string>

#include "levibatone/sdk/world/Actor.hpp"

namespace levibatone::bot {

// Mod Menu surface: status HUD overlay, the command text entry, and the
// floating STOP button. Registered during enable(), removed on disable().
class Hud {
public:
    static bool enable(const std::string &modId, const std::string &fontPath);
    static void disable();
    // Called once per tick from the player tick hook; refreshes both overlays.
    static void tick(sdk::Player *player);

private:
    static void renderDebug(sdk::Player *player);
};

}
