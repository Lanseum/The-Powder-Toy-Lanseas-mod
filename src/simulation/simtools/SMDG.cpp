#include "simulation/ToolCommon.h"
#include "simulation/SimulationData.h"
#include "gui/game/Brush.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
struct SmudgeTarget
{
	int id;
	float projection;
};

float normalisedDistanceSq(Vec2<float> offset, Vec2<int> radius)
{
	auto normalX = radius.X ? offset.X / float(radius.X) : 0.0f;
	auto normalY = radius.Y ? offset.Y / float(radius.Y) : 0.0f;
	return normalX * normalX + normalY * normalY;
}

void smudgeParticles(Simulation *sim, Brush const &brush, Vec2<int> position1, Vec2<int> position2)
{
	auto drag = position2 - position1;
	if (drag == Vec2<int>{ 0, 0 })
	{
		return;
	}

	Vec2<float> dragFloat(drag);
	auto dragLength = std::sqrt(dragFloat.X * dragFloat.X + dragFloat.Y * dragFloat.Y);
	if (dragLength <= 0.0f)
	{
		return;
	}

	auto maxDrag = 12.0f;
	auto dragScale = std::min(dragLength, maxDrag) / dragLength;
	dragFloat = dragFloat * dragScale;

	auto strength = 0.85f;
	std::vector<SmudgeTarget> targets;
	auto brushSize = brush.GetSize();
	targets.reserve(size_t(brushSize.X * brushSize.Y * 2));

	for (auto offset : brush)
	{
		auto pos = position1 + offset;
		if (pos.X < 0 || pos.Y < 0 || pos.X >= XRES || pos.Y >= YRES)
		{
			continue;
		}

		auto normSq = normalisedDistanceSq(Vec2<float>(offset), brush.GetRadius());
		if (normSq > 1.0f)
		{
			continue;
		}

		auto mapValue = sim->pmap[pos.Y][pos.X];
		if (mapValue)
		{
			targets.push_back({ ID(mapValue), offset.X * dragFloat.X + offset.Y * dragFloat.Y });
		}

		mapValue = sim->photons[pos.Y][pos.X];
		if (mapValue)
		{
			targets.push_back({ ID(mapValue), offset.X * dragFloat.X + offset.Y * dragFloat.Y });
		}
	}

	std::sort(targets.begin(), targets.end(), [](SmudgeTarget const &lhs, SmudgeTarget const &rhs) {
		return lhs.projection > rhs.projection;
	});

	for (auto const &target : targets)
	{
		auto &part = sim->parts[target.id];
		if (!part.type)
		{
			continue;
		}

		auto offset = Vec2<float>{ part.x, part.y } - Vec2<float>(position1);
		auto normSq = normalisedDistanceSq(offset, brush.GetRadius());
		if (normSq > 1.0f)
		{
			continue;
		}

		auto falloff = (1.0f - normSq) * strength;
		auto move = dragFloat * falloff;
		auto dest = Vec2<int>(Vec2<float>{ part.x, part.y } + move + Vec2<float>{ 0.5f, 0.5f });
		if (dest.X < 0 || dest.Y < 0 || dest.X >= XRES || dest.Y >= YRES)
		{
			continue;
		}

		sim->do_move(target.id, int(part.x + 0.5f), int(part.y + 0.5f), dest.X, dest.Y);
		part.vx += move.X * 0.12f;
		part.vy += move.Y * 0.12f;
	}
}

void performDraw(SimTool *tool, Simulation *sim, Brush const &brush, ui::Point position)
{
}

void performDrawLine(SimTool *tool, Simulation *sim, Brush const &brush, ui::Point position1, ui::Point position2, bool dragging)
{
	smudgeParticles(sim, brush, position1, position2);
}

void performDrag(SimTool *tool, Simulation *sim, Brush const &brush, ui::Point position1, ui::Point position2)
{
	smudgeParticles(sim, brush, position1, position2);
}
}

void SimTool::Tool_SMDG()
{
	Identifier = "DEFAULT_TOOL_SMDG";
	Name = "SMDG";
	Colour = 0xB464FF_rgb;
	Description = "Smudge tool. Drag to push particles around with a soft circular brush.";
	PerformDraw = &performDraw;
	PerformDrawLine = &performDrawLine;
	PerformDrag = &performDrag;
}
