#pragma once

#include <levibatone/sdk/Functions.hpp>
#include <levibatone/sdk/Signatures.hpp>
#include <levibatone/sdk/Memory.hpp>
#include <levibatone/sdk/Offsets.hpp>
#include <levibatone/sdk/world/Actor.hpp>
#include <levibatone/sdk/world/BlockSource.hpp>

namespace levibatone::sdk {

class ClientInstance;

using GetLocalPlayerFn = Player *(*)(ClientInstance *);

class ClientInstance {
public:
    BlockSource *region() { return virtualCall<BlockSource *>(this, offsets::VTable::ClientInstance_getRegion); }

    Player *localPlayer() {
        auto target = function<GetLocalPlayerFn>(memory::SignatureId::ClientInstanceGetLocalPlayer);
        return target ? target(this) : nullptr;
    }

    void *guiData() { return field<void *>(this, offsets::ClientInstance::mGuiData); }
};

}
