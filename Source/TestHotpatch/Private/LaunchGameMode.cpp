#include "LaunchGameMode.h"
#include "LaunchPlayerController.h"

ALaunchGameMode::ALaunchGameMode()
{
	PlayerControllerClass = ALaunchPlayerController::StaticClass();
}
