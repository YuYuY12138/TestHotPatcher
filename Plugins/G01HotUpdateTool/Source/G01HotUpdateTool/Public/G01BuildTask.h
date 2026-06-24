#pragma once

#include "CoreMinimal.h"
#include "G01BuildTask.generated.h"

USTRUCT()
struct FG01BuildTaskOptions
{
    GENERATED_BODY()

    UPROPERTY()
    bool bCookPatchAssets = true;

    UPROPERTY()
    bool bCompressPak = true;

    UPROPERTY()
    bool bStandaloneMode = false;

    UPROPERTY()
    bool bCalculateMD5 = true;

    UPROPERTY()
    bool bGenerateManifest = true;

    UPROPERTY()
    bool bGenerateBuildReport = true;
};

/**
 * FG01BuildTask - 构建任务描述
 *
 * 版本维度说明：
 *   basePackageVersion - Android 安装包版本（如 1.0.0），决定热更链归属
 *   baseVersion        - HotPatcher 对比用的 Release 版本（如 1.0.3）
 *   targetVersion      - 本次补丁的目标资源版本（如 1.0.4）
 *
 * 关系示例：
 *   BasePackage_1.0.0.apk → Release_1.0.0 → Patch_1.0.1 → ... → Patch_1.0.4
 *   所有这些版本的 basePackageVersion 都是 1.0.0
 *   当重新出包 BasePackage_1.1.0.apk 时，开启新的热更链
 */
USTRUCT()
struct FG01BuildTask
{
    GENERATED_BODY()

    UPROPERTY()
    FString TaskType = TEXT("BuildPatch");

    UPROPERTY()
    FString Platform = TEXT("Android");

    UPROPERTY()
    FString PatchType = TEXT("Snapshot");

    /**
     * 基础安装包版本
     * 标识当前热更链所属的 Android 安装包
     * 所有 Release/Patch 都必须记录这个字段
     * 客户端据此判断补丁是否适用于当前安装包
     */
    UPROPERTY()
    FString BasePackageVersion;

    /** HotPatcher 对比用的 Release 版本号 */
    UPROPERTY()
    FString BaseVersion;

    /** 本次补丁的目标资源版本号 */
    UPROPERTY()
    FString TargetVersion;

    UPROPERTY()
    FString PromoteFromPatchVersion;

    UPROPERTY()
    FString ReleaseConfigTemplate;

    UPROPERTY()
    FString PatchConfigTemplate;

    UPROPERTY()
    FString OutputDir = TEXT("Saved/HotPatcher");

    UPROPERTY()
    FG01BuildTaskOptions Options;

    bool LoadFromFile(const FString& FilePath);
    bool SaveToFile(const FString& FilePath) const;
    TArray<FString> Validate() const;
};
