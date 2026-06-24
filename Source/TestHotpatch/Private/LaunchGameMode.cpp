#include "LaunchGameMode.h"
#include "LaunchPlayerController.h"

ALaunchGameMode::ALaunchGameMode()
{
	// 指定 PlayerController 类型为热更专用控制器
	// LaunchMap 启动时引擎会自动实例化并调用其 BeginPlay
	PlayerControllerClass = ALaunchPlayerController::StaticClass();
}
