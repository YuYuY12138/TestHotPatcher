#include "HotUpdateSubsystem.h"
#include "FlibPakHelper.h"                          // HotPatcher 提供的 Pak 挂载工具
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

void UHotUpdateSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 在 GameInstance 初始化阶段就创建好工作目录
	// 避免后续扫描时因目录不存在而失败
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*GetPendingDir());
	PlatformFile.CreateDirectoryTree(*GetPaksDir());

	UE_LOG(LogTemp, Log, TEXT("[HotUpdate] Subsystem initialized"));
	UE_LOG(LogTemp, Log, TEXT("[HotUpdate] Pending dir: %s"), *GetPendingDir());
	UE_LOG(LogTemp, Log, TEXT("[HotUpdate] Paks dir:    %s"), *GetPaksDir());
}

int32 UHotUpdateSubsystem::ScanAndInstallPatches()
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	IFileManager& FileManager = IFileManager::Get();

	// 枚举 Pending 目录下所有 .pak 文件（不递归子目录）
	TArray<FString> PakFiles;
	FileManager.FindFiles(PakFiles, *GetPendingDir(), TEXT(".pak"));

	if (PakFiles.Num() == 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[HotUpdate] No pending patches found in: %s"), *GetPendingDir());
		return 0;
	}

	UE_LOG(LogTemp, Log, TEXT("[HotUpdate] Found %d pending patch(es)"), PakFiles.Num());

	int32 InstalledCount = 0;

	for (const FString& PakFileName : PakFiles)
	{
		FString SrcPath = FPaths::Combine(GetPendingDir(), PakFileName);
		FString DstPath = FPaths::Combine(GetPaksDir(), PakFileName);

		UE_LOG(LogTemp, Log, TEXT("[HotUpdate] Installing: %s"), *PakFileName);

		// 将文件从 Pending 移动到 Paks 目录
		// 使用 Move 而不是 Copy+Delete，在同分区下是原子操作，更安全
		if (!PlatformFile.MoveFile(*DstPath, *SrcPath))
		{
			UE_LOG(LogTemp, Error, TEXT("[HotUpdate] Failed to move: %s -> %s"), *SrcPath, *DstPath);
			continue;
		}

		// PakOrder 决定覆盖优先级：数字越大优先级越高
		// 从 100 开始，每个补丁递增，确保新补丁覆盖旧补丁中的同路径资源
		int32 PakOrder = 100 + InstalledCount;
		bool bMounted = UFlibPakHelper::MountPak(DstPath, PakOrder, FString());

		if (bMounted)
		{
			UE_LOG(LogTemp, Log, TEXT("[HotUpdate] Mounted: %s (order=%d) OK"), *PakFileName, PakOrder);
			InstalledCount++;
		}
		else
		{
			// 挂载失败：将文件移回 Pending，保持旧版本可运行
			UE_LOG(LogTemp, Error, TEXT("[HotUpdate] Mount failed: %s, rolling back"), *PakFileName);
			PlatformFile.MoveFile(*SrcPath, *DstPath);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[HotUpdate] Done: %d/%d patch(es) installed"), InstalledCount, PakFiles.Num());
	return InstalledCount;
}

FString UHotUpdateSubsystem::GetPendingDir() const
{
	// Pending 目录：存放已下载但尚未安装的补丁文件
	// 下载过程中使用 .download 后缀，校验通过后改为 .pak
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("HotUpdate"), TEXT("Pending"));
}

FString UHotUpdateSubsystem::GetPaksDir() const
{
	// UE 引擎启动时自动扫描 Saved/Paks/ 目录
	// 文件名中含 _P 后缀的 pak 会被识别为补丁并以高优先级加载
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Paks"));
}
