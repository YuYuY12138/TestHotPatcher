#pragma once

#include "CoreMinimal.h"
#include "G01BuildReport.generated.h"

USTRUCT()
struct FG01BuildReportOutput
{
    GENERATED_BODY()

    UPROPERTY()
    FString Type;

    UPROPERTY()
    FString Path;

    UPROPERTY()
    int64 Size = 0;

    UPROPERTY()
    FString MD5;
};

USTRUCT()
struct FG01BuildReport
{
    GENERATED_BODY()

    UPROPERTY()
    FString TaskType;

    UPROPERTY()
    FString Platform;

    UPROPERTY()
    FString PatchType;

    /** 该次构建所属的安装包版本 */
    UPROPERTY()
    FString BasePackageVersion;

    UPROPERTY()
    FString BaseVersion;

    UPROPERTY()
    FString TargetVersion;

    UPROPERTY()
    FString BuildTime;

    UPROPERTY()
    float Duration = 0.f;

    UPROPERTY()
    bool bSuccess = false;

    UPROPERTY()
    TArray<FG01BuildReportOutput> Outputs;

    UPROPERTY()
    TArray<FString> Errors;

    bool SaveToFile(const FString& FilePath) const;
    bool LoadFromFile(const FString& FilePath);
};

// ---- BasePackage 记录（BuildHistory 根级） ----

/**
 * FG01BasePackageInfo - 基础安装包记录
 *
 * 记录一条热更链的安装包基准。
 * 每次重新打整包时新增一条记录，并将 isActiveBase 切换。
 *
 * 关系：
 *   BasePackage_1.0.0 → linkedReleaseVersion = "1.0.0"
 *   → Patch_1.0.1, Release_1.0.1, Patch_1.0.2 ... 都属于这条热更链
 *
 *   BasePackage_1.1.0 → linkedReleaseVersion = "1.1.0"
 *   → 开启新热更链，isActiveBase = true，旧链 isActiveBase = false
 */
USTRUCT()
struct FG01BasePackageInfo
{
    GENERATED_BODY()

    /** 安装包版本号，如 "1.0.0" */
    UPROPERTY()
    FString PackageVersion;

    UPROPERTY()
    FString Platform;

    /**
     * 与该安装包对应的初始 Release 版本
     * 热更链从这个 Release 开始延伸
     */
    UPROPERTY()
    FString LinkedReleaseVersion;

    /** APK 路径（当前阶段存 Pak 文件绝对路径） */
    UPROPERTY()
    FString PackagePath;

    /** Pak 文件 MD5，用于校验基础包一致性 */
    UPROPERTY()
    FString PakMD5;

    UPROPERTY()
    FString BuildTime;

    /** Git Commit / P4 CL（预留，当前可为空） */
    UPROPERTY()
    FString GitCommit;

    /** 是否为当前活跃的基础包，新整包时将旧条目置 false */
    UPROPERTY()
    bool bIsActiveBase = false;
};

// ---- History Entry ----

USTRUCT()
struct FG01BuildHistoryEntry
{
    GENERATED_BODY()

    UPROPERTY()
    FString TargetVersion;

    UPROPERTY()
    FString BaseVersion;

    /** Release / Snapshot / Incremental / Merged */
    UPROPERTY()
    FString PatchType;

    UPROPERTY()
    FString Platform;

    /** 所属安装包版本 */
    UPROPERTY()
    FString BasePackageVersion;

    UPROPERTY()
    FString BuildTime;

    UPROPERTY()
    bool bSuccess = false;

    UPROPERTY()
    FString ReportPath;

    UPROPERTY()
    TArray<FString> ContainsVersions;

    UPROPERTY()
    int64 TotalPakSize = 0;

    UPROPERTY()
    FString PakMD5;

    UPROPERTY()
    bool bObsolete = false;

    UPROPERTY()
    FString PromotedFromPatch;

    UPROPERTY()
    FString CandidateReleasePath;
};

// ---- BuildHistory 根 ----

USTRUCT()
struct FG01BuildHistory
{
    GENERATED_BODY()

    /**
     * 基础安装包记录列表
     * 每次重新出整包时追加，isActiveBase 标记当前活跃包
     */
    UPROPERTY()
    TArray<FG01BasePackageInfo> BasePackages;

    UPROPERTY()
    TArray<FG01BuildHistoryEntry> Entries;

    UPROPERTY()
    FString LatestReleaseVersion;

    UPROPERTY()
    FString LatestPatchVersion;

    /** 当前活跃的基础包版本（缓存，从 BasePackages 推断） */
    UPROPERTY()
    FString ActiveBasePackageVersion;

    bool LoadFromFile(const FString& FilePath);
    bool SaveToFile(const FString& FilePath) const;
    void AddEntry(const FG01BuildHistoryEntry& Entry);

    /**
     * 注册或更新基础包信息
     * 调用时将其他包的 isActiveBase 置 false，新包置 true
     */
    void RegisterBasePackage(const FG01BasePackageInfo& Info);

    TArray<const FG01BuildHistoryEntry*> GetReleases() const;
    TArray<const FG01BuildHistoryEntry*> GetPatches() const;

    /** 获取当前活跃基础包信息 */
    const FG01BasePackageInfo* GetActiveBasePackage() const;
};
