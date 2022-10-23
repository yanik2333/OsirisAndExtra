#include "AimbotFunctions.h"
#include "Animations.h"
#include "Resolver.h"

#include "AntiAim.h"
#include "../SDK/UserCmd.h"
#include "../Logger.h"

#include "../SDK/GameEvent.h"
#include "../GameData.h"

std::deque<Resolver::SnapShot> snapshots;

float desyncAngle{ 0 };
UserCmd* command;

void Resolver::reset() noexcept
{
	snapshots.clear();
}

void Resolver::saveRecord(const int playerIndex, const float playerSimulationTime) noexcept
{
	const auto entity = interfaces->entityList->getEntity(playerIndex);
	const auto player = Animations::getPlayer(playerIndex);
	if (!player.gotMatrix || !entity)
		return;

	SnapShot snapshot;
	snapshot.player = player;
	snapshot.playerIndex = playerIndex;
	snapshot.eyePosition = localPlayer->getEyePosition();
	snapshot.model = entity->getModel();

	if (player.simulationTime == playerSimulationTime)
	{
		snapshots.push_back(snapshot);
		return;
	}

	for (int i = 0; i < static_cast<int>(player.backtrackRecords.size()); i++)
	{
		if (player.backtrackRecords.at(i).simulationTime == playerSimulationTime)
		{
			snapshot.backtrackRecord = i;
			snapshots.push_back(snapshot);
			return;
		}
	}
}

void Resolver::getEvent(GameEvent* event) noexcept
{
	if (!event || !localPlayer || interfaces->engine->isHLTV())
		return;

	switch (fnv::hashRuntime(event->getName())) {
	case fnv::hash("round_start"):
	{
		//Reset all
		const auto players = Animations::setPlayers();
		if (players->empty())
			break;

		for (auto& player : *players)
		{
			player.misses = 0;
		}
		snapshots.clear();
		desyncAngle = 0;
		break;
	}
	case fnv::hash("weapon_fire"):
	{
		//Reset player
		if (snapshots.empty())
			break;
		const auto playerId = event->getInt("userid");
		if (playerId == localPlayer->getUserId())
			break;

		const auto index = interfaces->engine->getPlayerForUserID(playerId);
		Animations::setPlayer(index)->shot = true;
		break;
	}
	case fnv::hash("player_hurt"):
	{
		if (snapshots.empty())
			break;
		if (!localPlayer || !localPlayer->isAlive())
		{
			snapshots.clear();
			return;
		}

		if (event->getInt("attacker") != localPlayer->getUserId())
			break;

		const auto hitgroup = event->getInt("hitgroup");
		if (hitgroup < HitGroup::Head || hitgroup > HitGroup::RightLeg)
			break;
		const auto index{ interfaces->engine->getPlayerForUserID(event->getInt("userid")) };
		const auto& [player, model, eyePosition, bulletImpact, gotImpact, time, playerIndex, backtrackRecord] = snapshots.front();
		if (desyncAngle != 0.f && hitgroup == HitGroup::Head)
			Animations::setPlayer(index)->workingangle = desyncAngle;
		const auto entity = interfaces->entityList->getEntity(playerIndex);
		Logger::addLog("Resolver: Hit " + entity->getPlayerName() + ", using Angle: " + std::to_string(desyncAngle));
		if (!entity->isAlive())
			desyncAngle = 0.f;
		snapshots.pop_front(); //Hit somebody so dont calculate
		break;
	}
	case fnv::hash("bullet_impact"):
	{
		if (snapshots.empty())
			break;

		auto& [player, model, eyePosition, bulletImpact, gotImpact, time, playerIndex, backtrackRecord] = snapshots.front();
		if (event->getInt("userid") == localPlayer->getUserId())
		{
			if (!gotImpact)
			{
				time = memory->globalVars->serverTime();
				bulletImpact = Vector{ event->getFloat("x"), event->getFloat("y"), event->getFloat("z") };
				gotImpact = true;
			}
		}
		else if (player.shot)
			antiOnetap(event->getInt("userid"), interfaces->entityList->getEntity(playerIndex), Vector{ event->getFloat("x"),event->getFloat("y"),event->getFloat("z") });
		break;
	}
	default:
		break;
	}
	if (!config->ragebot[0].resolver)
		snapshots.clear();
}

void Resolver::processMissedShots() noexcept
{
	if (!config->ragebot[0].resolver)
	{
		snapshots.clear();
		return;
	}

	if (!localPlayer || !localPlayer->isAlive())
	{
		snapshots.clear();
		return;
	}

	if (snapshots.empty())
		return;

	if (snapshots.front().time == -1) //Didnt get data yet
		return;

	auto [player, smodel, eyePosition, bulletImpact, gotImpact, time, playerIndex, backtrackRecord] = snapshots.front();
	snapshots.pop_front(); //got the info no need for this
	if (!player.gotMatrix)
		return;

	const auto entity = interfaces->entityList->getEntity(playerIndex);
	if (!entity)
		return;

	const Model* model = smodel;
	if (!model)
		return;

	StudioHdr* hdr = interfaces->modelInfo->getStudioModel(model);
	if (!hdr)
		return;

	StudioHitboxSet* set = hdr->getHitboxSet(0);
	if (!set)
		return;

	const auto angle = AimbotFunction::calculateRelativeAngle(eyePosition, bulletImpact, Vector{ });
	const auto end = bulletImpact + Vector::fromAngle(angle) * 2000.f;

	const auto matrix = backtrackRecord == -1 ? player.matrix.data() : player.backtrackRecords.at(backtrackRecord).matrix;

	bool resolverMissed = false;

	for (int hitbox = 0; hitbox < Max; hitbox++)
	{
		if (AimbotFunction::hitboxIntersection(matrix, hitbox, set, eyePosition, end))
		{
			resolverMissed = true;
			if (desyncAngle == player.workingangle && desyncAngle != 0.f)
				player.workingangle = 0.f;
			std::string missed{ "Resolver: Missed " + std::to_string(player.misses + 1) + " shots to " + entity->getPlayerName() + " due to resolver" };
			if (backtrackRecord > 0)
				missed += ", BT[" + std::to_string(backtrackRecord) + "]";
			missed += ", Angle: " + std::to_string(desyncAngle);
			Animations::setPlayer(playerIndex)->misses++;
			if (!std::ranges::count(player.blacklisted, desyncAngle))
				player.blacklisted.push_back(desyncAngle);
			Logger::addLog(missed);
			desyncAngle = 0;
			break;
		}
	}
	if (!resolverMissed)
		Logger::addLog("Resolver: Missed " + entity->getPlayerName() + " due to spread");
}

float getBackwardSide(Entity* entity) {
	if (!entity->isAlive())
		return -1.f;
	const float result = Helpers::angleDiff(localPlayer->origin().y, entity->origin().y);
	return result;
}
float getAngle(Entity* entity) {
	return Helpers::angleNormalize(entity->eyeAngles().y);
}
float getForwardYaw(Entity* entity) {
	return Helpers::angleNormalize(getBackwardSide(entity) - 180.f);
}

void Resolver::runPreUpdate(Animations::Players player, Entity* entity) noexcept
{
	if (!config->ragebot[0].resolver)
		return;

	const auto misses = player.misses;
	if (!entity || !entity->isAlive())
		return;

	if (player.chokedPackets <= 0)
		return;
	if (!localPlayer || !localPlayer->isAlive())
		return;
	if (snapshots.empty())
		return;
	auto& [snapshot_player, model, eyePosition, bulletImpact, gotImpact, time, playerIndex, backtrackRecord] = snapshots.front();
	setupDetect(player, entity);
	resolveEntity(player, entity);
	desyncAngle = entity->getAnimstate()->footYaw;
	const auto animstate = entity->getAnimstate();
	animstate->footYaw = desyncAngle;
	if (snapshot_player.workingangle != 0.f && fabs(desyncAngle) > fabs(snapshot_player.workingangle))
	{
		if (snapshot_player.workingangle < 0.f && player.side == 1)
			snapshot_player.workingangle = fabs(snapshot_player.workingangle);
		else if (snapshot_player.workingangle > 0.f && player.side == -1)
			snapshot_player.workingangle = snapshot_player.workingangle * -1.f;
		desyncAngle = snapshot_player.workingangle;
		animstate->footYaw = desyncAngle;
	}
}

void Resolver::runPostUpdate(Animations::Players player, Entity* entity) noexcept
{
	if (!config->ragebot[0].resolver)
		return;

	const auto misses = player.misses;
	if (!entity || !entity->isAlive())
		return;

	if (player.chokedPackets <= 0)
		return;
	if (!localPlayer || !localPlayer->isAlive())
		return;

	if (snapshots.empty())
		return;

	auto& [snapshot_player, model, eyePosition, bulletImpact, gotImpact, time, playerIndex, backtrackRecord] = snapshots.front();
	const auto animstate = entity->getAnimstate();
	setupDetect(player, entity);
	resolveEntity(player, entity);
	desyncAngle = animstate->footYaw;
	if (snapshot_player.workingangle != 0.f && fabs(desyncAngle) > fabs(snapshot_player.workingangle))
	{
		if (snapshot_player.workingangle < 0.f && player.side == 1)
			snapshot_player.workingangle = fabs(snapshot_player.workingangle);
		else if (snapshot_player.workingangle > 0.f && player.side == -1)
			snapshot_player.workingangle = snapshot_player.workingangle * -1.f;
		desyncAngle = snapshot_player.workingangle;
		animstate->footYaw = desyncAngle;
	}
}

float buildServerAbsYaw([[maybe_unused]] Animations::Players player, Entity* entity, float angle)
{
	Vector velocity = entity->velocity();
	auto anim_state = entity->getAnimstate();
	float m_flEyeYaw = angle;
	float m_flGoalFeetYaw = 0.f;

	float eye_feet_delta = Helpers::angleDiff(m_flEyeYaw, m_flGoalFeetYaw);

	static auto GetSmoothedVelocity = [](const float min_delta, const Vector a, const Vector b) {
		const Vector delta = a - b;
		const float delta_length = delta.length();

		if (delta_length <= min_delta)
		{
			Vector result{};

			if (-min_delta <= delta_length)
				return a;
			const float iradius = 1.0f / (delta_length + FLT_EPSILON);
			return b - delta * iradius * min_delta;
		}
		const float iradius = 1.0f / (delta_length + FLT_EPSILON);
		return b + delta * iradius * min_delta;
	};

	float spd = velocity.squareLength();;

	if (spd > std::powf(1.2f * 260.0f, 2.f))
	{
		Vector velocity_normalized = velocity.normalized();
		velocity = velocity_normalized * (1.2f * 260.0f);
	}

	float m_flChokedTime = anim_state->lastUpdateTime;
	float v25 = std::clamp(entity->duckAmount() + anim_state->duckAdditional, 0.0f, 1.0f);
	float v26 = anim_state->animDuckAmount;
	float v27 = m_flChokedTime * 6.0f;
	float v28;

	// clamp
	if (v25 - v26 <= v27) {
		if (-v27 <= v25 - v26)
			v28 = v25;
		else
			v28 = v26 - v27;
	}
	else {
		v28 = v26 + v27;
	}

	float flDuckAmount = std::clamp(v28, 0.0f, 1.0f);

	Vector animationVelocity = GetSmoothedVelocity(m_flChokedTime * 2000.0f, velocity, entity->velocity());
	float speed = std::fminf(animationVelocity.length(), 260.0f);

	float flMaxMovementSpeed = 260.0f;

	Entity* pWeapon = entity->getActiveWeapon();
	if (pWeapon && pWeapon->getWeaponData())
		flMaxMovementSpeed = std::fmaxf(pWeapon->getWeaponData()->maxSpeedAlt, 0.001f);

	float flRunningSpeed = speed / (flMaxMovementSpeed * 0.520f);
	float flDuckingSpeed = speed / (flMaxMovementSpeed * 0.340f);

	flRunningSpeed = std::clamp(flRunningSpeed, 0.0f, 1.0f);

	float flYawModifier = (anim_state->walkToRunTransition * -0.30000001 - 0.19999999) * flRunningSpeed + 1.0f;

	if (flDuckAmount > 0.0f)
	{
		float flDuckingSpeed = std::clamp(flDuckingSpeed, 0.0f, 1.0f);
		flYawModifier += (flDuckAmount * flDuckingSpeed) * (0.5f - flYawModifier);
	}

	constexpr float v60 = -58.f;
	constexpr float v61 = 58.f;

	float flMinYawModifier = v60 * flYawModifier;
	float flMaxYawModifier = v61 * flYawModifier;

	if (eye_feet_delta <= flMaxYawModifier)
	{
		if (flMinYawModifier > eye_feet_delta)
			m_flGoalFeetYaw = fabs(flMinYawModifier) + m_flEyeYaw;
	}
	else
	{
		m_flGoalFeetYaw = m_flEyeYaw - fabs(flMaxYawModifier);
	}

	Helpers::normalizeYaw(m_flGoalFeetYaw);

	if (speed > 0.1f || fabs(velocity.z) > 100.0f)
	{
		m_flGoalFeetYaw = Helpers::approachAngle(
			m_flEyeYaw,
			m_flGoalFeetYaw,
			(anim_state->walkToRunTransition * 20.0f + 30.0f)
			* m_flChokedTime);
	}
	else
	{
		m_flGoalFeetYaw = Helpers::approachAngle(
			entity->lby(),
			m_flGoalFeetYaw,
			m_flChokedTime * 100.0f);
	}

	return m_flGoalFeetYaw;
}

void Resolver::detectSide(Entity* entity, int* side) {
	/* externs */
	Vector forward, right, up;
	Trace tr;
	Helpers::angleVectors(Vector(0, getBackwardSide(entity), 0), &forward, &right, &up);
	/* filtering */

	Vector src3D = entity->getEyePosition();
	Vector dst3D = src3D + (forward * 384);

	/* back engine tracers */
	interfaces->engineTrace->traceRay({ src3D, dst3D }, 0x200400B, { entity }, tr);
	float back_two = (tr.endpos - tr.startpos).length();

	/* right engine tracers */
	interfaces->engineTrace->traceRay(Ray(src3D + right * 35, dst3D + right * 35), 0x200400B, { entity }, tr);
	const float right_two = (tr.endpos - tr.startpos).length();

	/* left engine tracers */
	interfaces->engineTrace->traceRay(Ray(src3D - right * 35, dst3D - right * 35), 0x200400B, { entity }, tr);

	/* fix side */
	if (const float left_two = (tr.endpos - tr.startpos).length(); left_two > right_two) {
		*side = -1;
	}
	else if (right_two > left_two) {
		*side = 1;
	}
	else
		*side = 0;
}

void Resolver::resolveEntity(Animations::Players player, Entity* entity) {
	// get the players max rotation.
	float max_rotation = entity->getMaxDesyncAngle();
	int index = 0;
	float eye_yaw = entity->getAnimstate()->eyeYaw;
	if (const bool extended = player.extended; !extended && fabs(max_rotation) > 60.f)
	{
		max_rotation = max_rotation / 1.8f;
	}

	// resolve shooting players separately.
	if (player.shot) {
		entity->getAnimstate()->footYaw = eye_yaw + resolveShot(player, entity);
		return;
	}
	else {
		if (entity->velocity().length2D() <= 0.1) {
			const float angle_difference = Helpers::angleDiff(eye_yaw, entity->getAnimstate()->footYaw);
			index = 2 * angle_difference <= 0.0f ? 1 : -1;
		}
		else
		{
			if (!(static_cast<int>(player.layers[12].weight) * 1000.f) && entity->velocity().length2D() > 0.1) {
				const auto m_layer_delta1 = abs(player.layers[6].playbackRate - player.oldlayers[6].playbackRate);
				const auto m_layer_delta2 = abs(player.layers[6].playbackRate - player.oldlayers[6].playbackRate);

				if (const auto m_layer_delta3 = abs(player.layers[6].playbackRate - player.oldlayers[6].playbackRate); m_layer_delta1 < m_layer_delta2
					|| m_layer_delta3 <= m_layer_delta2
					|| static_cast<signed int>(static_cast<float>(m_layer_delta2 * 1000.0)))
				{
					if (m_layer_delta1 >= m_layer_delta3
						&& m_layer_delta2 > m_layer_delta3
						&& !static_cast<signed int>(static_cast<float>(m_layer_delta3 * 1000.0)))
					{
						index = 1;
					}
				}
				else
				{
					index = -1;
				}
			}
		}
	}

	switch (player.misses % 3) {
	case 0: //default
		entity->getAnimstate()->footYaw = buildServerAbsYaw(player, entity, entity->eyeAngles().y + max_rotation * index);
		break;
	case 1: //reverse
		entity->getAnimstate()->footYaw = buildServerAbsYaw(player, entity, entity->eyeAngles().y + max_rotation * -index);
		break;
	case 2: //middle
		entity->getAnimstate()->footYaw = buildServerAbsYaw(player, entity, entity->eyeAngles().y);
		break;
	}

}

float Resolver::resolveShot(const Animations::Players player, Entity* entity) {
	/* fix unrestricted shot */
	const float flPseudoFireYaw = Helpers::angleNormalize(Helpers::angleDiff(localPlayer->origin().y, player.matrix[8].origin().y));
	if (player.extended) {
		const float flLeftFireYawDelta = fabsf(Helpers::angleNormalize(flPseudoFireYaw - (entity->eyeAngles().y + 58.f)));
		const float flRightFireYawDelta = fabsf(Helpers::angleNormalize(flPseudoFireYaw - (entity->eyeAngles().y - 58.f)));

		return flLeftFireYawDelta > flRightFireYawDelta ? -58.f : 58.f;
	}
	else {
		const float flLeftFireYawDelta = fabsf(Helpers::angleNormalize(flPseudoFireYaw - (entity->eyeAngles().y + 29.f)));
		const float flRightFireYawDelta = fabsf(Helpers::angleNormalize(flPseudoFireYaw - (entity->eyeAngles().y - 29.f)));

		return flLeftFireYawDelta > flRightFireYawDelta ? -29.f : 29.f;
	}
}

void Resolver::setupDetect(Animations::Players player, Entity* entity) {

	// detect if player is using maximum desync.
	if (player.layers[3].cycle == 0.f)
	{
		if (player.layers[3].weight = 0.f)
		{
			player.extended = true;
		}
	}
	/* calling detect side */
	detectSide(entity, &player.side);
	const int side = player.side;
	/* bruting vars */
	float resolve_value = 50.f;
	static float brute = 0.f;
	const float fl_max_rotation = entity->getMaxDesyncAngle();
	const float fl_eye_yaw = entity->getAnimstate()->eyeYaw;
	const float perfect_resolve_yaw = resolve_value;
	const bool fl_forward = fabsf(Helpers::angleNormalize(getAngle(entity) - getForwardYaw(entity))) < 90.f;
	const int fl_shots = player.misses;

	/* clamp angle */
	if (fl_max_rotation < resolve_value) {
		resolve_value = fl_max_rotation;
	}

	/* detect if entity is using max desync angle */
	if (player.extended) {
		resolve_value = fl_max_rotation;
	}
	/* setup bruting */
	if (fl_shots == 0) {
		brute = perfect_resolve_yaw * (fl_forward ? -side : side);
	}
	else {
		switch (fl_shots % 3) {
		case 0: {
			brute = perfect_resolve_yaw * (fl_forward ? -side : side);
		} break;
		case 1: {
			brute = perfect_resolve_yaw * (fl_forward ? side : -side);
		} break;
		case 2: {
			brute = 0;
		} break;
		}
	}

	/* fix goalfeet yaw */
	entity->getAnimstate()->footYaw = fl_eye_yaw + brute;
}

Vector calcAngle(Vector source, Vector entityPos) {
	Vector delta = {};
	delta.x = source.x - entityPos.x;
	delta.y = source.y - entityPos.y;
	delta.z = source.z - entityPos.z;
	Vector angles = {};
	const Vector viewangles = command->viewangles;
	angles.x = Helpers::rad2deg(atan(delta.z / hypot(delta.x, delta.y))) - viewangles.x;
	angles.y = Helpers::rad2deg(atan(delta.y / delta.x)) - viewangles.y;
	angles.z = 0;
	if (delta.x >= 0.f)
		angles.y += 180;

	return angles;
}

void Resolver::antiOnetap(const int userid, Entity* entity, const Vector shot)
{
	std::vector<std::reference_wrapper<const PlayerData>> playersOrdered{ GameData::players().begin(), GameData::players().end() };
	std::ranges::sort(playersOrdered, [](const PlayerData& a, const PlayerData& b) {
		// enemies first
		if (a.enemy != b.enemy)
			return a.enemy && !b.enemy;

		return a.handle < b.handle;
		});
	for (const PlayerData& player : playersOrdered) {
		if (player.userId == userid)
		{
			if (entity->isAlive())
			{
				const Vector pos = shot;
				const Vector eyepos = entity->getEyePosition();
				const Vector ang = calcAngle(eyepos, pos);
				const Vector angToLocal = calcAngle(eyepos, localPlayer->getEyePosition());
				const Vector delta = { angToLocal.x - ang.x, angToLocal.y - ang.y, 0 };
				if (const float fov = sqrt(delta.x * delta.x + delta.y * delta.y); fov < 20.f)
				{
					Logger::addLog("Resolver: " + player.name + " missed");
					if (config->rageAntiAim.roll)
					{
						config->rageAntiAim.roll = false;
					}
					else
					{
						config->rageAntiAim.roll = true;
					}
				}
			}
		}
	}
}

void Resolver::cmdGrabber(UserCmd* cmd)
{
	command = cmd;
}

void Resolver::updateEventListeners(const bool forceRemove) noexcept
{
	class ImpactEventListener : public GameEventListener {
	public:
		void fireGameEvent(GameEvent* event) override
		{
			getEvent(event);
		}
	};

	static ImpactEventListener listener[4];

	if (static bool listenerRegistered = false; config->ragebot[0].resolver && !listenerRegistered) {
		interfaces->gameEventManager->addListener(&listener[0], "bullet_impact");
		interfaces->gameEventManager->addListener(&listener[1], "player_hurt");
		interfaces->gameEventManager->addListener(&listener[2], "round_start");
		interfaces->gameEventManager->addListener(&listener[3], "weapon_fire");
		listenerRegistered = true;
	}
	else if ((!config->ragebot[0].resolver || forceRemove) && listenerRegistered) {
		interfaces->gameEventManager->removeListener(&listener[0]);
		interfaces->gameEventManager->removeListener(&listener[1]);
		interfaces->gameEventManager->removeListener(&listener[2]);
		interfaces->gameEventManager->removeListener(&listener[3]);
		listenerRegistered = false;
	}
}