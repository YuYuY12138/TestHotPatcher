#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Widgets/SWidget.h"
#include "LaunchPlayerController.generated.h"

/**
 * ALaunchPlayerController - 启动关卡专用 PlayerController
 *
 * 职责：
 *   1. 在 BeginPlay 中创建一个最简的 Slate UI（一个"Enter Game"按钮）
 *   2. 用户点击按钮后：
 *      a. 防止重复点击（bIsTransitioning 标记）
 *      b. 调用 UHotUpdateSubsystem 扫描并安装 Pending 目录中的补丁
 *      c. 移除 Slate UI，避免切关卡后 UI 残留
 *      d. OpenLevel 切换到正式关卡（TestMap）
 *
 * 为什么用 Slate 而不是 UMG Widget：
 *   热更检查发生在 UnLua 初始化之前，Lua VM 尚未创建，
 *   无法使用依赖 Lua 的 UIManager 框架。
 *   Slate 是纯 C++ 的 UI 框架，不依赖 UnLua，在任何阶段都可以安全使用。
 *
 * 重要约束：
 *   ★ 此类严禁实现 IUnLuaInterface，否则会提前触发 Lua VM 创建。
 */
UCLASS()
class TESTHOTPATCH_API ALaunchPlayerController : public APlayerController
{
	GENERATED_BODY()

public:

	/**
	 * BeginPlay - 关卡开始时调用
	 * 创建 Slate UI 并显示到屏幕，等待用户操作。
	 */
	virtual void BeginPlay() override;

private:

	/**
	 * 按钮点击回调 - 执行热更新并切换关卡
	 *
	 * 执行顺序：
	 *   1. 检查 bIsTransitioning，防止重复触发
	 *   2. 调用 UHotUpdateSubsystem::ScanAndInstallPatches()
	 *   3. 从 Viewport 移除 Slate UI
	 *   4. OpenLevel("TestMap")
	 */
	void OnEnterGameClicked();

	/**
	 * 保存 Slate UI 根节点的引用
	 * 用于在切关卡前调用 RemoveViewportWidgetContent 移除 UI，
	 * 防止 UI 在新关卡中残留。
	 */
	TSharedPtr<SWidget> LaunchWidget;

	/**
	 * 过渡标记 - 防止用户多次点击按钮
	 * 第一次点击后设为 true，后续点击直接忽略。
	 */
	bool bIsTransitioning = false;
};
