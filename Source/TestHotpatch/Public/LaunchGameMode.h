#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "LaunchGameMode.generated.h"

/**
 * ALaunchGameMode - 启动关卡专用 GameMode
 *
 * 职责：
 *   作为 LaunchMap 关卡的 GameMode，仅负责指定 PlayerController 类型。
 *   不绑定任何 UnLua 脚本，确保在热更流程完成前不会触发 Lua VM 初始化。
 *
 * 重要约束：
 *   ★ 此类及其衍生类严禁实现 IUnLuaInterface / GetModuleName，
 *     否则加载 LaunchMap 时会提前创建 Lua VM，导致补丁 Lua 脚本无法生效。
 *
 * 设计思路：
 *   整个热更新流程必须在 UnLua 初始化之前完成。
 *   LaunchMap 使用纯 C++ 的 GameMode + PlayerController，
 *   不加载任何业务 Lua，待补丁挂载完成后再切换到正式关卡（TestMap），
 *   届时 TestMap 的 GameMode 才会触发 Lua VM 创建，require 到的全是最新脚本。
 */
UCLASS()
class TESTHOTPATCH_API ALaunchGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:

	/**
	 * 构造函数
	 * 设置 PlayerControllerClass 为 ALaunchPlayerController，
	 * 使 LaunchMap 启动时自动创建热更新 UI 和流程逻辑。
	 */
	ALaunchGameMode();
};
