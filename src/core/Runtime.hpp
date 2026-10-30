#pragma once

namespace levibatone::core {

// Mod lifecycle. enable() resolves signatures, wires the SDK function
// pointers, installs hooks, and registers the Mod Menu surface; disable()
// undoes all of it.
bool load();
bool enable();
bool disable();
bool unload();

}
