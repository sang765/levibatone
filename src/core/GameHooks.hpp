#pragma once

#include <string_view>

#include "levibatone/sdk/Types.hpp"

namespace levibatone::core {

// Installs the detours the bot needs: the local player tick, the client
// instance update, and the GameMode interaction entry points used to learn the
// player's GameMode instance (plus destroy/build original functions).
bool installHooks();
void uninstallHooks();

// Live observation of ApplyTurnDelta semantics: every call (including the
// game's own touch-look) records the passed delta and the rotation change it
// produced. arg == diff means delta semantics; arg ~ rot means absolute.
struct TurnObservation {
    bool valid = false;
    sdk::Vec2 arg{0, 0};
    sdk::Vec2 diff{0, 0};
};

TurnObservation lastTurnObservation();

}
