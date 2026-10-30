#pragma once

#include <string_view>

namespace levibatone::core {

// Installs the detours the bot needs: the local player tick, the client
// instance update, and the GameMode interaction entry points used to learn the
// player's GameMode instance (plus destroy/build original functions).
bool installHooks();
void uninstallHooks();

}
