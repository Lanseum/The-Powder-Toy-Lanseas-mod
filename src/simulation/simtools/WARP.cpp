#include "simulation/ToolCommon.h"
#include "simulation/SimulationData.h"
#include "gui/game/Brush.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
struct WarpTarget
{
	int id;
	float distanceSq;
};

float normalisedDistanceSq(Vec2<float> offset, Vec2<int> radius)
{
	auto normalX = radius.X ? offset.X / float(radius.X) : 0.0f;
	auto normalY = radius.Y ? offset.Y / float(radius.Y) : 0.0f;
	return normalX * normalX + normalY * normalY;
}

void warpParticles(SimTool *tool, Simulation *sim, Brush const &brush, Vec2<int> centre)
{
	std::vector<WarpTarget> targets;
	targets.reserve(size_t(sim->parts.active));

	for (auto i = 0; i < sim->parts.active; ++i)
	{
		auto const &part = sim->parts[i];
		if (!part.type || (sim->editLayersEnabled && part.editLayer != sim->editLayer))
		{
			continue;
		}

		auto offset = Vec2<float>{ part.x, part.y } - Vec2<float>(centre);
		auto normSq = normalisedDistanceSq(Vec2<float>(offset), brush.GetRadius());
		if (normSq <= 0.0f || normSq > 1.0f)
		{
			continue;
		}

		targets.push_back({ i, normSq });
	}

	std::sort(targets.begin(), targets.end(), [](WarpTarget const &lhs, WarpTarget const &rhs) {
		return lhs.distanceSq > rhs.distanceSq;
	});

	auto movedAny = false;
	auto pullScale = (0.18f + std::clamp(tool->Strength, 0.0f, 2.0f) * 0.08f) / 5.0f;
	for (auto const &target : targets)
	{
		auto &part = sim->parts[target.id];
		if (!part.type)
		{
			continue;
		}

		auto fromCentre = Vec2<float>{ part.x, part.y } - Vec2<float>(centre);
		auto normSq = normalisedDistanceSq(fromCentre, brush.GetRadius());
		if (normSq <= 0.0f || normSq > 1.0f)
		{
			continue;
		}

		auto pull = std::min(normSq, 1.0f) * pullScale;
		auto move = fromCentre * -pull;
		auto dest = Vec2<float>{ part.x, part.y } + move;
		auto destCell = Vec2<int>(dest + Vec2<float>{ 0.5f, 0.5f });
		if (destCell.X < 0 || destCell.Y < 0 || destCell.X >= XRES || destCell.Y >= YRES ||
			sim->IsWallBlocking(destCell.X, destCell.Y, part.type))
		{
			continue;
		}

		part.x = std::clamp(dest.X, 0.0f, float(XRES - 1));
		part.y = std::clamp(dest.Y, 0.0f, float(YRES - 1));
		part.vx += move.X * 0.08f;
		part.vy += move.Y * 0.08f;
		movedAny = true;
	}

	if (movedAny)
	{
		sim->force_stacking_check = true;
		if (sim->editLayersEnabled)
		{
			sim->RebuildMapsForEditLayer();
		}
		else
		{
			sim->RecalcFreeParticles(false);
		}
	}
}

void performDraw(SimTool *tool, Simulation *sim, Brush const &brush, ui::Point position)
{
	warpParticles(tool, sim, brush, position);
}

void performDrawLine(SimTool *tool, Simulation *sim, Brush const &brush, ui::Point position1, ui::Point position2, bool dragging)
{
	warpParticles(tool, sim, brush, position2);
}
}

void SimTool::Tool_WARP()
{
	Identifier = "DEFAULT_TOOL_WARP";
	Name = "WARP";
	Colour = 0x45D6B8_rgb;
	Description = "Warp tool. Pulls particles toward the brush centre, faster near the brush edge.";
	PerformDraw = &performDraw;
	PerformDrawLine = &performDrawLine;
}
