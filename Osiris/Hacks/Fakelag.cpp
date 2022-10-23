#include <random>

#include "Fakelag.h"
#include "EnginePrediction.h"
#include "Tickbase.h"

#include "../SDK/Entity.h"
#include "../SDK/Localplayer.h"
#include "../SDK/NetworkChannel.h"
#include "../SDK/UserCmd.h"
#include "../SDK/Vector.h"
#undef min
#undef max

namespace Fakelag
{
    template <typename T>
    class uniform_int_random_generator
    {
        std::uniform_int_distribution<T> distribution;
        std::default_random_engine random_engine;
    public:
        explicit uniform_int_random_generator(const unsigned seed) : distribution{}, random_engine{ seed }
        {
        }

        uniform_int_random_generator(const T min, const T max, const unsigned seed) : distribution{ min, max }, random_engine{ seed }
        {
        }

        [[nodiscard]] T get()
        {
            return distribution(random_engine);
        }

        void set_range(const T min, const T max)
        {
            if (distribution.min() == min && distribution.max() == max)
                return;
            distribution = std::uniform_int_distribution{ min, max };
        }
    };
	uniform_int_random_generator<int> random{ static_cast<unsigned>(std::chrono::high_resolution_clock::now().time_since_epoch().count()) };
}

void Fakelag::run(bool& sendPacket) noexcept
{
    if (!localPlayer || !localPlayer->isAlive())
        return;

    const auto netChannel = interfaces->engine->getNetworkChannel();
    if (!netChannel)
        return;
    if (EnginePrediction::getVelocity().length2D() < 1)
        return;

    auto chokedPackets = config->legitAntiAim.enabled || config->fakeAngle.enabled ? 2 : 0;
    if (config->fakelag.enabled && !config->tickbase.doubletap.isActive() && !config->tickbase.hideshots.isActive())
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

    if (interfaces->engine->isVoiceRecording())
        sendPacket = netChannel->chokedPackets >= std::min(3, chokedPackets);
    else
        sendPacket = netChannel->chokedPackets >= chokedPackets;
}