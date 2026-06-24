#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "HotUpdateSubsystem.generated.h"

/**
 * UHotUpdateSubsystem - 热更新调度子系统
 *
 * 职责：
 *   管理补丁 Pak 文件的安装与挂载流程。
 *   在 GameInstance 初始化时自动创建所需目录，
 *   在玩家点击"进入游戏"后扫描 Pending 目录，
 *   将验证通过的补丁移动到 Saved/Paks/ 并调用引擎挂载接口。
 *
 * 生命周期：
 *   随 GameInstance 创建/销毁，跨关卡常驻。
 *
 * 使用方式（C++）：
 *   UHotUpdateSubsystem* Sub = GetGameInstance()->GetSubsystem<UHotUpdateSubsystem>();
 *   int32 Count = Sub->ScanAndInstallPatches();
 *
 * 目录约定：
 *   Pending : Saved/HotUpdate/Pending/  —— 下载完成待安装的补丁
 *   Paks    : Saved/Paks/               —— 引擎自动扫描的补丁目录
 */
UCLASS()
class TESTHOTPATCH_API UHotUpdateSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:

	/**
	 * 子系统初始化
	 * 在 GameInstance::Init() 之后由引擎自动调用。
	 * 负责创建 Pending 和 Paks 目录（如果不存在）。
	 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/**
	 * 扫描 Pending 目录并安装所有补丁
	 *
	 * 执行步骤：
	 *   1. 枚举 Pending/ 目录下所有 .pak 文件
	 *   2. 将每个 .pak 移动到 Saved/Paks/
	 *   3. 调用 UFlibPakHelper::MountPak 挂载
	 *   4. 挂载失败时将文件移回 Pending/（回滚）
	 *
	 * @return 成功安装并挂载的补丁数量
	 *
	 * 注意：挂载顺序由 PakOrder 决定（从 100 开始递增），
	 *       数字越大优先级越高，会覆盖低优先级 Pak 中的同路径资源。
	 */
	UFUNCTION(BlueprintCallable, Category = "HotUpdate")
	int32 ScanAndInstallPatches();

private:

	/**
	 * 获取补丁下载临时目录的绝对路径
	 * 路径：{ProjectSavedDir}/HotUpdate/Pending/
	 */
	FString GetPendingDir() const;

	/**
	 * 获取引擎自动扫描的补丁目录的绝对路径
	 * 路径：{ProjectSavedDir}/Paks/
	 * 说明：UE 引擎在启动时会自动扫描此目录并挂载 *_P.pak 文件
	 */
	FString GetPaksDir() const;
};
