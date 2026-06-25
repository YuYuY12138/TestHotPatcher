#pragma once

#include "CoreMinimal.h"
#include "G01VersionManifest.generated.h"

USTRUCT()
struct FG01ManifestFileInfo
{
    GENERATED_BODY()

    UPROPERTY()
    FString Name;

    UPROPERTY()
    FString Url;

    UPROPERTY()
    int64 Size = 0;

    UPROPERTY()
    FString MD5;
};

USTRUCT()
struct FG01VersionManifest
{
    GENERATED_BODY()

    UPROPERTY()
    FString Version;

    /** 该补丁所属的安装包版本，客户端据此判断补丁适用性 */
    UPROPERTY()
    FString BasePackageVersion;

    UPROPERTY()
    FString BaseVersion;

    UPROPERTY()
    FString PatchType;

    /** 唯一标识：{BasePackageVersion}_{BaseVersion}_to_{TargetVersion}_{PatchType} */
    UPROPERTY()
    FString PatchId;

    UPROPERTY()
    FString Platform;

    UPROPERTY()
    FString BuildTime;

    UPROPERTY()
    TArray<FG01ManifestFileInfo> Files;

    UPROPERTY()
    FString ReleaseNote;

    UPROPERTY()
    TArray<FString> ContainsVersions;

    UPROPERTY()
    TArray<FString> ReplacesPatches;

    UPROPERTY()
    int32 MountOrder = 100;

    bool SaveToFile(const FString& FilePath) const;
};
