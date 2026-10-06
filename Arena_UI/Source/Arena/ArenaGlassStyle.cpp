#include "ArenaGlassStyle.h"
#include "HAL/IConsoleManager.h"

// Corner radius handed to UBackgroundBlur. A rounded box brush of radius 22 and a blur of radius 11 draw the same corner (the
// blur scales it with the UI by itself). The blur and the glass border must round
// their corners the same way; this is exposed as a cvar so the match can be tuned while looking at it.
static TAutoConsoleVariable<float> CVarArenaBlurRadius(
	TEXT("arena.BlurRadius"), 11.0f,
	TEXT("Corner radius of the blur behind the in-game glass panels (Slate units, multiplied by the UI scale)."));

float ArenaGlass::BlurRadius()
{
	return CVarArenaBlurRadius.GetValueOnGameThread();
}
