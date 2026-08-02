#include "simulation/ToolCommon.h"
#include "gui/game/Brush.h"
#include <algorithm>

namespace
{
int blastFromBrush(SimTool *tool, Brush const &brush)
{
	auto radius = brush.GetRadius();
	auto brushBlast = 1 + (radius.X + radius.Y) / 7;
	auto strengthBlast = int(tool->Strength * 1.5f);
	return std::clamp(brushBlast + strengthBlast, 1, 12);
}

void performClick(SimTool *tool, Simulation *sim, Brush const &brush, ui::Point position)
{
	sim->SpawnFpvDrone(position, blastFromBrush(tool, brush));
}
}

void SimTool::Tool_DRON()
{
	Identifier = "DEFAULT_TOOL_DRON";
	Name = "DRON";
	Colour = 0x50A8FF_rgb;
	Description = "FPV drone. Click to place, fly with arrow keys. Brush size sets crash explosion.";
	PerformClick = &performClick;
}
