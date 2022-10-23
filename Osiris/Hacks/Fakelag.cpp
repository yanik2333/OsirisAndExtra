#include "Fakelag.h"
#include "EnginePrediction.h"
#include "Tickbase.h"
#include "../random_generator.h"

#include "../SDK/Entity.h"
#include "../SDK/Localplayer.h"
#include "../SDK/NetworkChannel.h"
#include "../SDK/UserCmd.h"
#include "../SDK/Vector.h"

namespace Fakelag
{
    random_generator<int> random{ static_cast<unsigned>(std::chrono::high_resolution_clock::now().time_since_epoch().count()) };
}

void Fakelag::run(bool& sendPacket) noexcept
{
    if (!localPlayer || !localPlayer->isAlive())
        return;

    const auto netChannel = interfaces->engine->getNetworkChannel();
    if (!netChannel)
        return;

    auto chokedPackets = config->legitAntiAim.enabled || config->fakeAngle.enabled ? 2 : 0;
    if (config->fakelag.enabled)
    {
        const float speed = EnginePrediction::getVelocity().length2D() >= 15.0f ? EnginePrediction::getVelocity().length2D() : 0.0f;
        switch (config->fakelag.mode) {
        case 0: //Static
            chokedPackets = config->fakelag.limit;
            break;
        case 1: //Adaptive
            chokedPackets = std::clamp(static_cast<int>(std::ceilf(64 / (speed * memory->globalVars->intervalPerTick))), 1, config->fakelag.limit);
            break;
        case 2: // Random
            random.set_range(1, config->fakelag.limit);
            chokedPackets = random.get();
            break;
        }
    }

    chokedPackets = std::clamp(chokedPackets, 0, maxUserCmdProcessTicks - Tickbase::getTargetTickShift());

#undef min
    if (interfaces->engine->isVoiceRecording())
        sendPacket = netChannel->chokedPackets >= std::min(3, chokedPackets);
    else
        sendPacket = netChannel->chokedPackets >= chokedPackets;
}