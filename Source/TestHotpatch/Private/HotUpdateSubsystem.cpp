#include "HotUpdateSubsystem.h"
#include "FlibPakHelper.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

void UHotUpdateSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*GetPendingDir());
	PlatformFile.CreateDirectoryTree(*GetPaksDir());

	UE_LOG(LogTemp, Log, TEXT("[HotUpdate] Subsystem initialized"));
	UE_LOG(LogTemp, Log, TEXT("[HotUpdate] Pending dir: %s"), *GetPendingDir());
	UE_LOG(LogTemp, Log, TEXT("[HotUpdate] Paks dir: %s"), *GetPaksDir());
}

int32 UHotUpdateSubsystem::ScanAndInstallPatches()
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	IFileManager& FileManager = IFileManager::Get();

	TArray<FString> PakFiles;
	FileManager.FindFiles(PakFiles, *GetPendingDir(), TEXT(".pak"));

	if (PakFiles.Num() == 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[HotUpdate] No pending patches found"));
		return 0;
	}

	int32 InstalledCount = 0;

	for (const FString& PakFile : PakFiles)
	{
		FString SrcPath = FPaths::Combine(GetPendingDir(), PakFile);
		FString DstPath = FPaths::Combine(GetPaksDir(), PakFile);

		UE_LOG(LogTemp, Log, TEXT("[HotUpdate] Installing patch: %s"), *PakFile);

		if (PlatformFile.MoveFile(*DstPath, *SrcPath))
		{
			int32 PakOrder = 100 + InstalledCount;
			bool bMounted = UFlibPakHelper::MountPak(DstPath, PakOrder, FString());

			if (bMounted)
			{
				UE_LOG(LogTemp, Log, TEXT("[HotUpdate] Mounted: %s (order=%d)"), *PakFile, PakOrder);
				InstalledCount++;
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("[HotUpdate] Mount failed: %s"), *PakFile);
				PlatformFile.MoveFile(*SrcPath, *DstPath);
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[HotUpdate] Move failed: %s → %s"), *SrcPath, *DstPath);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[HotUpdate] Installed %d patches"), InstalledCount);
	return InstalledCount;
}

FString UHotUpdateSubsystem::GetPendingDir() const
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("HotUpdate"), TEXT("Pending"));
}

FString UHotUpdateSubsystem::GetPaksDir() const
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Paks"));
}
