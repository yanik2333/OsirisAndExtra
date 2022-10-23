#pragma once

#include "Animations.h"

#include "../SDK/GameEvent.h"
#include "../SDK/Entity.h"

namespace Resolver
{
	void reset() noexcept;

	void processMissedShots() noexcept;
	void saveRecord(int playerIndex, float playerSimulationTime) noexcept;
	void getEvent(GameEvent* event) noexcept;

	void runPreUpdate(Animations::Players player, Entity* entity) noexcept;
	void runPostUpdate(Animations::Players player, Entity* entity) noexcept;

	float resolveShot(Animations::Players player, Entity* entity);

	void detectSide(Entity* entity, int* side);

	void setupDetect(Animations::Players player, Entity* entity);

	void antiOnetap(int userid, Entity* entity, Vector shot);

	void cmdGrabber(UserCmd* cmd);

	void resolveEntity(Animations::Players player, Entity* entity);

	void updateEventListeners(bool forceRemove = false) noexcept;

	struct SnapShot
	{
		Animations::Players player;
		const Model* model{ };
		Vector eyePosition{};
		Vector bulletImpact{};
		bool gotImpact{ false };
		float time{ -1 };
		int playerIndex{ -1 };
		int backtrackRecord{ -1 };
	};
}