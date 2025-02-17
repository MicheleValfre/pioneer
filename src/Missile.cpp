// Copyright © 2008-2019 Pioneer Developers. See AUTHORS.txt for details
// Licensed under the terms of the GPL v3. See licenses/GPL-3.txt

#include "Missile.h"

#include "Game.h"
#include "Lang.h"
#include "LuaEvent.h"
#include "Pi.h"
#include "Sfx.h"
#include "Ship.h"
#include "ShipAICmd.h"
#include "Space.h"

Missile::Missile(const ShipType::Id &shipId, Body *owner, int power)
{
	AddFeature(Feature::PROPULSION); // add component propulsion
	if (power < 0) {
		m_power = 0;
		if (shipId == ShipType::MISSILE_GUIDED) m_power = 1;
		if (shipId == ShipType::MISSILE_SMART) m_power = 2;
		if (shipId == ShipType::MISSILE_NAVAL) m_power = 3;
	} else
		m_power = power;

	m_owner = owner;
	m_type = &ShipType::types[shipId];

	SetMass(m_type->hullMass * 1000);

	SetModel(m_type->modelName.c_str());
	SetMassDistributionFromModel();

	SetLabel(Lang::MISSILE);

	Disarm();

	GetPropulsion()->SetFuel(1.0);
	GetPropulsion()->SetFuelReserve(0.0);

	m_curAICmd = 0;
	m_aiMessage = AIERROR_NONE;
	m_decelerating = false;

	GetPropulsion()->Init(this, GetModel(), m_type->fuelTankMass, m_type->effectiveExhaustVelocity, m_type->linThrust, m_type->angThrust);
}

Missile::Missile(const Json &jsonObj, Space *space) :
	DynamicBody(jsonObj, space)
{
	AddFeature(Feature::PROPULSION);
	GetPropulsion()->LoadFromJson(jsonObj, space);
	Json missileObj = jsonObj["missile"];

	try {
		m_type = &ShipType::types[missileObj["ship_type_id"]];
		SetModel(m_type->modelName.c_str());

		m_curAICmd = 0;
		m_curAICmd = AICommand::LoadFromJson(missileObj);
		m_aiMessage = AIError(missileObj["ai_message"]);

		m_ownerIndex = missileObj["index_for_body"];
		m_power = missileObj["power"];
		m_armed = missileObj["armed"];
	} catch (Json::type_error &) {
		throw SavedGameCorruptException();
	}

	GetPropulsion()->Init(this, GetModel(), m_type->fuelTankMass, m_type->effectiveExhaustVelocity, m_type->linThrust, m_type->angThrust);
}

void Missile::SaveToJson(Json &jsonObj, Space *space)
{
	DynamicBody::SaveToJson(jsonObj, space);
	GetPropulsion()->SaveToJson(jsonObj, space);
	Json missileObj = Json::object(); // Create JSON object to contain missile data.

	if (m_curAICmd) m_curAICmd->SaveToJson(missileObj);

	missileObj["ai_message"] = int(m_aiMessage);
	missileObj["index_for_body"] = space->GetIndexForBody(m_owner);
	missileObj["power"] = m_power;
	missileObj["armed"] = m_armed;
	missileObj["ship_type_id"] = m_type->id;

	jsonObj["missile"] = missileObj; // Add missile object to supplied object.
}

void Missile::PostLoadFixup(Space *space)
{
	DynamicBody::PostLoadFixup(space);
	m_owner = space->GetBodyByIndex(m_ownerIndex);
	if (m_curAICmd) m_curAICmd->PostLoadFixup(space);
}

Missile::~Missile()
{
	if (m_curAICmd) delete m_curAICmd;
}

void Missile::ECMAttack(int power_val)
{
	if (power_val > m_power) {
		CollisionContact dummy;
		OnDamage(0, 1.0f, dummy);
	}
}

void Missile::StaticUpdate(const float timeStep)
{
	// Note: direct call to AI->TimeStepUpdate

	if (!m_curAICmd) {
		GetPropulsion()->ClearLinThrusterState();
		GetPropulsion()->ClearAngThrusterState();
	} else if (m_curAICmd->TimeStepUpdate()) {
		delete m_curAICmd;
		m_curAICmd = nullptr;
	}
	//Add smoke trails for missiles on thruster state
	static double s_timeAccum = 0.0;
	s_timeAccum += timeStep;
	if (!is_equal_exact(GetPropulsion()->GetLinThrusterState().LengthSqr(), 0.0) && (s_timeAccum > 4 || 0.1 * Pi::rng.Double() < timeStep)) {
		s_timeAccum = 0.0;
		const vector3d pos = GetOrient() * vector3d(0, 0, 5);
		const float speed = std::min(10.0 * GetVelocity().Length() * std::max(1.0, fabs(GetPropulsion()->GetLinThrusterState().z)), 100.0);
		SfxManager::AddThrustSmoke(this, speed, pos);
	}
}

void Missile::TimeStepUpdate(const float timeStep)
{

	const vector3d thrust = GetPropulsion()->GetActualLinThrust();
	AddRelForce(thrust);
	AddRelTorque(GetPropulsion()->GetActualAngThrust());

	DynamicBody::TimeStepUpdate(timeStep);
	GetPropulsion()->UpdateFuel(timeStep);

	const float MISSILE_DETECTION_RADIUS = 100.0f;
	if (!m_owner) {
		Explode();
	} else if (m_armed) {
		Space::BodyNearList nearby = Pi::game->GetSpace()->GetBodiesMaybeNear(this, MISSILE_DETECTION_RADIUS);
		for (Body *body : nearby) {
			if (body == this) continue;
			double dist = (body->GetPosition() - GetPosition()).Length();
			if (dist < MISSILE_DETECTION_RADIUS) {
				Explode();
				break;
			}
		}
	}
}

bool Missile::OnCollision(Object *o, Uint32 flags, double relVel)
{
	if (!IsDead()) {
		Explode();
	}
	return true;
}

bool Missile::OnDamage(Object *attacker, float kgDamage, const CollisionContact &contactData)
{
	if (!IsDead()) {
		Explode();
	}
	return true;
}

void Missile::Explode()
{
	Pi::game->GetSpace()->KillBody(this);

	const double damageRadius = 200.0;
	const double kgDamage = 10000.0;

	CollisionContact dummy;
	Space::BodyNearList nearby = Pi::game->GetSpace()->GetBodiesMaybeNear(this, damageRadius);
	for (Body *body : nearby) {
		if (body->GetFrame() != GetFrame()) continue;
		double dist = (body->GetPosition() - GetPosition()).Length();
		if (dist < damageRadius) {
			// linear damage decay with distance
			body->OnDamage(m_owner, kgDamage * (damageRadius - dist) / damageRadius, dummy);
			if (body->IsType(Object::SHIP))
				LuaEvent::Queue("onShipHit", dynamic_cast<Ship *>(body), m_owner);
		}
	}

	SfxManager::Add(this, TYPE_EXPLOSION);
}

void Missile::NotifyRemoved(const Body *const removedBody)
{
	if (m_curAICmd) m_curAICmd->OnDeleted(removedBody);
	if (m_owner == removedBody) {
		m_owner = 0;
	}
	DynamicBody::NotifyRemoved(removedBody);
}

void Missile::Arm()
{
	m_armed = true;
	Properties().Set("isArmed", true);
}

void Missile::Disarm()
{
	m_armed = false;
	Properties().Set("isArmed", false);
}

void Missile::Render(Graphics::Renderer *renderer, const Camera *camera, const vector3d &viewCoords, const matrix4x4d &viewTransform)
{
	if (IsDead()) return;

	GetPropulsion()->Render(renderer, camera, viewCoords, viewTransform);
	RenderModel(renderer, camera, viewCoords, viewTransform);
}

void Missile::AIKamikaze(Body *target)
{
	//AIClearInstructions();
	if (m_curAICmd != 0)
		delete m_curAICmd;
	m_curAICmd = new AICmdKamikaze(this, target);
}
