#include "simulation/ElementCommon.h"

int Element_STKM_graphics(GRAPHICS_FUNC_ARGS);

static int update(UPDATE_FUNC_ARGS);
static bool createAllowed(ELEMENT_CREATE_ALLOWED_FUNC_ARGS);
static void changeType(ELEMENT_CHANGETYPE_FUNC_ARGS);

void Element::Element_HMAN()
{
	Identifier = "DEFAULT_PT_HMAN";
	Name = "HMAN";
	Colour = 0xFFE0A0_rgb;
	MenuVisible = 1;
	MenuSection = SC_SPECIAL;
	Enabled = 1;

	Advection = 0.0f;
	AirDrag = 0.00f * CFDS;
	AirLoss = 0.2f;
	Loss = 1.0f;
	Collision = 0.0f;
	Gravity = 0.0f;
	NewtonianGravity = 0.0f;
	Diffusion = 0.0f;
	HotAir = 0.00f * CFDS;
	Falldown = 0;

	Flammable = 0;
	Explosive = 0;
	Meltable = 0;
	Hardness = 0;

	Weight = 50;

	DefaultProperties.temp = R_TEMP + 14.6f + 273.15f;
	HeatConduct = 0;
	Description = "Human. Runs on its own body movement with balancing arms and IK legs.";

	Properties = PROP_NOCTYPEDRAW;
	CarriesTypeIn = 0;

	LowPressure = IPL;
	LowPressureTransition = NT;
	HighPressure = IPH;
	HighPressureTransition = NT;
	LowTemperature = ITL;
	LowTemperatureTransition = NT;
	HighTemperature = 620.0f;
	HighTemperatureTransition = PT_FIRE;

	DefaultProperties.life = 100;

	Update = &update;
	Graphics = &Element_STKM_graphics;
	CreateAllowed = &createAllowed;
	ChangeType = &changeType;
}

static Vec2<float> lerp(Vec2<float> a, Vec2<float> b, float t)
{
	return a + (b - a) * t;
}

static Vec2<float> solveIk(Vec2<float> root, Vec2<float> target, float upper, float lower, float bendSide)
{
	auto delta = target - root;
	float dist = std::sqrt(delta.X * delta.X + delta.Y * delta.Y);
	if (dist < 0.001f)
	{
		delta = Vec2<float>(0.0f, 1.0f);
		dist = 0.001f;
	}
	auto dir = delta / dist;
	float clampedDist = std::clamp(dist, 0.001f, upper + lower - 0.001f);
	float base = std::clamp((clampedDist * clampedDist + upper * upper - lower * lower) / (2.0f * clampedDist), 0.0f, upper);
	float height = std::sqrt(std::max(0.0f, upper * upper - base * base));
	auto bend = Vec2<float>(-dir.Y, dir.X) * (height * bendSide);
	return root + dir * base + bend;
}

static Vec2<float> limitReach(Vec2<float> root, Vec2<float> target, float maxLength)
{
	auto delta = target - root;
	float dist = std::sqrt(delta.X * delta.X + delta.Y * delta.Y);
	if (dist <= maxLength || dist < 0.001f)
		return target;
	return root + delta / dist * maxLength;
}

static Vec2<float> solveLegIk(Vec2<float> hip, Vec2<float> foot, float upper, float lower, float outward)
{
	auto knee = solveIk(hip, foot, upper, lower, -outward);
	knee.X += outward * 0.8f;
	if (outward < 0.0f)
		knee.X = std::min(knee.X, hip.X - 0.4f);
	else
		knee.X = std::max(knee.X, hip.X + 0.4f);
	return knee;
}

static bool isSolidSupport(Simulation *sim, int self, int x, int y)
{
	if (x < CELL || y < CELL || x >= XRES-CELL || y >= YRES-CELL)
		return y >= YRES-CELL;
	if (sim->IsWallBlocking(x, y, PT_DUST))
		return true;
	auto r = sim->pmap[y][x];
	if (!r || ID(r) == self)
		return false;
	auto rt = TYP(r);
	auto const &elements = SimulationData::CRef().elements;
	return rt > 0 && rt < PT_NUM &&
		rt != PT_HMAN && rt != PT_STKM && rt != PT_STKM2 && rt != PT_FIGH &&
		!(elements[rt].Properties & (TYPE_GAS | TYPE_LIQUID | TYPE_ENERGY));
}

static bool isBodyBlocked(Simulation *sim, int self, int x, int y)
{
	if (x < CELL || y < CELL || x >= XRES-CELL || y >= YRES-CELL)
		return true;
	if (sim->IsWallBlocking(x, y, PT_HMAN))
		return true;
	auto r = sim->pmap[y][x];
	if (!r || ID(r) == self)
		return false;
	auto rt = TYP(r);
	auto const &elements = SimulationData::CRef().elements;
	return rt > 0 && rt < PT_NUM && !(elements[rt].Properties & (TYPE_GAS | TYPE_LIQUID | TYPE_ENERGY));
}

static bool findGroundY(Simulation *sim, int self, int x, int startY, int maxDepth, int &footY)
{
	x = std::clamp(x, CELL, XRES-CELL-1);
	startY = std::clamp(startY, CELL, YRES-CELL-1);
	auto endY = std::min(YRES-CELL-1, startY + maxDepth);
	for (auto y = startY; y <= endY; y++)
	{
		if (isSolidSupport(sim, self, x, y))
		{
			footY = y;
			return true;
		}
	}
	return false;
}

static bool chooseFootTarget(Simulation *sim, int self, float desiredX, float bodyY, Vec2<float> fallback, Vec2<float> &target)
{
	for (auto spread = 0; spread <= 7; spread++)
	{
		for (auto side = -1; side <= 1; side += 2)
		{
			if (spread == 0 && side > 0)
				continue;
			int footY = 0;
			int x = int(desiredX + float(spread * side) + 0.5f);
			if (findGroundY(sim, self, x, int(bodyY + 12.0f), 44, footY))
			{
				target = Vec2<float>(float(x), std::min(float(YRES - CELL - 1), float(footY) + 4.0f));
				return true;
			}
		}
	}
	target = fallback;
	return false;
}

static void smooth(float &value, float target, float amount)
{
	value += (target - value) * amount;
}

static void writeArmPose(HumanPose &pose, Vec2<float> leftElbow, Vec2<float> leftHand, Vec2<float> rightElbow, Vec2<float> rightHand)
{
	float targets[8] = {
		leftElbow.X, leftElbow.Y,
		leftHand.X, leftHand.Y,
		rightElbow.X, rightElbow.Y,
		rightHand.X, rightHand.Y,
	};
	if (!pose.ready)
	{
		for (int n = 0; n < 8; n++)
			pose.arms[n] = targets[n];
		return;
	}
	for (int n = 0; n < 8; n++)
		smooth(pose.arms[n], targets[n], 0.62f);
}

static void beginStep(HumanPose &pose, int leg, Vec2<float> from, Vec2<float> to)
{
	pose.stepping[leg] = true;
	pose.stepPhase[leg] = 0.0f;
	pose.stepFrom[leg * 2] = from.X;
	pose.stepFrom[leg * 2 + 1] = from.Y;
	pose.stepTo[leg * 2] = to.X;
	pose.stepTo[leg * 2 + 1] = to.Y;
	pose.nextStep = 1 - leg;
}

static Vec2<float> advanceFootStep(HumanPose &pose, int leg, Vec2<float> current, float speed)
{
	if (!pose.stepping[leg])
		return current;

	pose.stepPhase[leg] = std::min(1.0f, pose.stepPhase[leg] + speed);
	auto from = Vec2<float>(pose.stepFrom[leg * 2], pose.stepFrom[leg * 2 + 1]);
	auto to = Vec2<float>(pose.stepTo[leg * 2], pose.stepTo[leg * 2 + 1]);
	auto foot = lerp(from, to, pose.stepPhase[leg]);
	foot.Y -= std::sin(pose.stepPhase[leg] * std::numbers::pi_v<float>) * 5.0f;
	if (pose.stepPhase[leg] >= 1.0f)
	{
		pose.stepping[leg] = false;
		foot = to;
	}
	return foot;
}

static void updateHumanPose(Simulation *sim, int self, playerst *human, HumanPose &pose, Particle &part, float bodyX, float bodyY, float drive)
{
	auto leftFoot = Vec2<float>(human->legs[4], human->legs[5]);
	auto rightFoot = Vec2<float>(human->legs[12], human->legs[13]);
	auto travelLead = std::clamp(drive * 3.0f + part.vx * 1.2f, -6.0f, 6.0f);
	auto leftHome = Vec2<float>(bodyX - 3.5f + travelLead, bodyY + 22.0f);
	auto rightHome = Vec2<float>(bodyX + 3.5f + travelLead, bodyY + 22.0f);
	bool leftGround = chooseFootTarget(sim, self, leftHome.X, bodyY, leftHome, leftHome);
	bool rightGround = chooseFootTarget(sim, self, rightHome.X, bodyY, rightHome, rightHome);

	bool firstPoseFrame = !pose.ready;
	if (!pose.ready)
	{
		leftFoot = leftHome;
		rightFoot = rightHome;
		pose.nextStep = 0;
	}

	auto leftDrift = std::hypot(leftHome.X - leftFoot.X, leftHome.Y - leftFoot.Y);
	auto rightDrift = std::hypot(rightHome.X - rightFoot.X, rightHome.Y - rightFoot.Y);
	auto walking = std::fabs(drive) > 0.05f;
	auto threshold = walking ? 3.8f : 7.0f;
	if (!pose.stepping[0] && !pose.stepping[1] && (leftGround || rightGround))
	{
		if ((pose.nextStep == 0 && leftDrift > threshold) || (leftDrift > rightDrift + 2.0f))
			beginStep(pose, 0, leftFoot, leftHome);
		else if ((pose.nextStep == 1 && rightDrift > threshold) || (rightDrift > leftDrift + 2.0f))
			beginStep(pose, 1, rightFoot, rightHome);
	}

	auto stepSpeed = std::clamp(0.17f + std::fabs(part.vx) * 0.04f, 0.17f, 0.34f);
	leftFoot = advanceFootStep(pose, 0, leftFoot, stepSpeed);
	rightFoot = advanceFootStep(pose, 1, rightFoot, stepSpeed);
	if (!pose.stepping[0] && !leftGround)
		leftFoot = lerp(leftFoot, leftHome, 0.14f);
	if (!pose.stepping[1] && !rightGround)
		rightFoot = lerp(rightFoot, rightHome, 0.14f);

	auto legRoot = Vec2<float>(bodyX, bodyY + 12.0f);
	leftFoot = limitReach(legRoot, leftFoot, 9.7f);
	rightFoot = limitReach(legRoot, rightFoot, 9.7f);
	auto leftKnee = solveLegIk(legRoot, leftFoot, 4.0f, 5.8f, -1.0f);
	auto rightKnee = solveLegIk(legRoot, rightFoot, 4.0f, 5.8f, 1.0f);

	human->legs[0] = human->legs[2] = leftKnee.X;
	human->legs[1] = human->legs[3] = leftKnee.Y;
	human->legs[4] = leftFoot.X;
	human->legs[5] = leftFoot.Y;
	human->legs[8] = human->legs[10] = rightKnee.X;
	human->legs[9] = human->legs[11] = rightKnee.Y;
	human->legs[12] = rightFoot.X;
	human->legs[13] = rightFoot.Y;

	float supportX = (leftFoot.X + rightFoot.X) * 0.5f;
	float lean = std::clamp((bodyX - supportX) * 0.15f + part.vx * 0.32f, -1.8f, 1.8f);
	float swing = walking ? std::sin(float(human->frames) * 0.45f) : 0.0f;
	float forward = std::fabs(drive) > 0.05f ? (drive > 0.0f ? 1.0f : -1.0f) : (part.vx >= 0.0f ? 1.0f : -1.0f);
	auto leftShoulder = Vec2<float>(bodyX - 1.7f, bodyY + 5.0f);
	auto rightShoulder = Vec2<float>(bodyX + 1.7f, bodyY + 5.0f);
	auto leftPump = -swing;
	auto rightPump = swing;
	auto leftElbow = leftShoulder + Vec2<float>(-1.5f, 4.2f);
	auto rightElbow = rightShoulder + Vec2<float>(1.5f, 4.2f);
	if (walking)
	{
		leftElbow.X += forward * leftPump * 2.8f;
		rightElbow.X += forward * rightPump * 2.8f;
		leftElbow.Y += std::fabs(leftPump) * 0.4f;
		rightElbow.Y += std::fabs(rightPump) * 0.4f;
	}
	leftElbow.X -= lean * 0.25f;
	rightElbow.X -= lean * 0.25f;
	auto forearmOffset = [](float side, float pump, float forward) {
		auto dir = Vec2<float>(side * 0.25f + forward * pump * 0.65f, 0.95f - pump * 0.18f);
		auto length = std::sqrt(std::max(dir.X * dir.X + dir.Y * dir.Y, 0.001f));
		return dir / length;
	};
	auto leftHand = leftElbow + forearmOffset(-1.0f, leftPump, forward) * 2.0f;
	auto rightHand = rightElbow + forearmOffset(1.0f, rightPump, forward) * 2.0f;
	leftHand.X -= lean * 0.1f;
	rightHand.X -= lean * 0.1f;
	if (pose.ready)
	{
		auto deltaX = bodyX - pose.bodyX;
		auto deltaY = bodyY - pose.bodyY;
		for (auto n = 0; n < 8; n += 2)
		{
			pose.arms[n] += deltaX;
			pose.arms[n + 1] += deltaY;
		}
	}
	writeArmPose(pose, leftElbow, leftHand, rightElbow, rightHand);
	pose.bodyX = bodyX;
	pose.bodyY = bodyY;
	if (firstPoseFrame)
		pose.ready = true;
}

static void initHuman(Simulation *sim, int i)
{
	auto &human = sim->human;
	auto x = sim->parts[i].x;
	auto y = sim->parts[i].y;
	human = {};
	human.legs[0] = x - 1.0f;
	human.legs[1] = y + 12.0f;
	human.legs[4] = x - 4.0f;
	human.legs[5] = y + 22.0f;
	human.legs[8] = x + 1.0f;
	human.legs[9] = y + 12.0f;
	human.legs[12] = x + 4.0f;
	human.legs[13] = y + 22.0f;
	human.spwn = 1;
	human.elem = PT_NONE;
	human.spawnID = -1;
	sim->humanPose = {};
}

static int update(UPDATE_FUNC_ARGS)
{
	auto *human = &sim->human;
	auto &part = parts[i];
	part.ctype = PT_NONE;
	human->frames++;

	if (part.life < 1)
	{
		sim->kill_part(i);
		return 1;
	}

	int groundY = 0;
	bool groundFound = findGroundY(sim, i, int(part.x + 0.5f), int(part.y + 12.0f), 44, groundY);
	float targetY = groundFound ? float(groundY) - 22.0f : part.y + 32.0f;
	bool grounded = groundFound && part.y >= targetY - 3.0f && part.vy >= -0.4f;

	float drive = 0.0f;
	if (human->comm & 0x01)
		drive -= 1.0f;
	if (human->comm & 0x02)
		drive += 1.0f;

	if (sim->fpvDrone.active)
	{
		float dx = part.x - sim->fpvDrone.x;
		float dy = part.y - sim->fpvDrone.y;
		float distSq = dx * dx + dy * dy;
		if (distSq < 70.0f * 70.0f)
		{
			float dist = std::sqrt(std::max(distSq, 1.0f));
			float panic = 1.0f - dist / 70.0f;
			drive += (dx < 0.0f ? -1.0f : 1.0f) * (0.8f + panic * 1.0f);
			if (grounded && dist < 28.0f && dy > -18.0f)
				part.vy = std::min(part.vy, -2.2f);
		}
	}

	drive = std::clamp(drive, -1.8f, 1.8f);
	auto running = std::fabs(drive) > 0.05f;
	auto runBob = running && grounded ? -std::abs(std::sin(float(human->frames) * 0.45f)) * 1.4f : 0.0f;
	if (std::fabs(drive) > 0.01f)
		part.vx += drive * (grounded ? 0.24f : 0.12f);
	else
		part.vx *= grounded ? 0.78f : 0.985f;
	part.vx = std::clamp(part.vx, -2.4f, 2.4f);

	bool jumpPressed = (human->comm & 0x04) && !(human->pcomm & 0x04);
	if (grounded)
	{
		if (jumpPressed)
			part.vy = -3.6f;
		else
		{
			part.vy += (targetY + runBob - part.y) * 0.16f;
			part.vy *= 0.82f;
			part.vy = std::clamp(part.vy, -2.4f, 2.4f);
		}
	}
	else
	{
		part.vy = std::min(part.vy + 0.22f, 4.2f);
	}

	int side = part.vx > 0.05f ? 1 : (part.vx < -0.05f ? -1 : 0);
	if (side)
	{
		int probeX = int(part.x + part.vx + float(side * 3) + 0.5f);
		for (auto yOff = -2; yOff <= 18; yOff += 3)
		{
			if (isBodyBlocked(sim, i, probeX, int(part.y + float(yOff) + 0.5f)))
			{
				part.vx = 0.0f;
				break;
			}
		}
	}

	updateHumanPose(sim, i, human, sim->humanPose, part, part.x + part.vx, part.y + part.vy, drive);
	human->pcomm = human->comm;
	return 0;
}

static bool createAllowed(ELEMENT_CREATE_ALLOWED_FUNC_ARGS)
{
	return sim->elementCount[PT_HMAN] <= 0 && !sim->human.spwn;
}

static void changeType(ELEMENT_CHANGETYPE_FUNC_ARGS)
{
	if (to == PT_HMAN)
	{
		initHuman(sim, i);
		sim->activeControllable = CONTROL_HMAN;
	}
	else
	{
		sim->human.spwn = 0;
		sim->humanPose = {};
	}
}
