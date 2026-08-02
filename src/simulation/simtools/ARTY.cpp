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

void performDraw(SimTool *tool, Simulation *sim, Brush const &brush, ui::Point position)
{
	sim->BeginArtilleryAim(position);
}

void performDrawLine(SimTool *tool, Simulation *sim, Brush const &brush, ui::Point position1, ui::Point position2, bool dragging)
{
	if (dragging)
	{
		sim->UpdateArtilleryAim(position2);
	}
	else
	{
		sim->FireArtilleryShell(position1, position2, blastFromBrush(tool, brush));
	}
}

void performClick(SimTool *tool, Simulation *sim, Brush const &brush, ui::Point position)
{
	if (sim->artilleryAiming)
	{
		sim->FireArtilleryShell(sim->artilleryAimStart, position, blastFromBrush(tool, brush));
	}
}
}

void SimTool::Tool_ARTY()
{
	Identifier = "DEFAULT_TOOL_ARTY";
	Name = "ARTY";
	Colour = 0xD49A3A_rgb;
	Description = "Artillery tool. Drag to aim and release to fire a visible shell.";
	PerformDraw = &performDraw;
	PerformDrawLine = &performDrawLine;
	PerformClick = &performClick;
}
