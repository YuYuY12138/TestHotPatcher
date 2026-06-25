#include "G01HotPatchCommandlet.h"
#include "G01HotPatcherAdapter.h"
#include "G01VersionManifest.h"
#include "G01BuildReport.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Serialization/JsonSerializer.h"
#include "AssetRegistry/AssetRegistryModule.h"

static bool IsRelPath(const FString& P)
{
    if (P.IsEmpty()) return true;
    if (P.Len() >= 2 && P[1] == TEXT(':')) return false;
    if (P[0] == TEXT('/') || P[0] == TEXT('\\')) return false;
    return true;
}

static FString ToAbs(const FString& P, const FString& Dir)
{
    return IsRelPath(P) ? FPaths::Combine(Dir, P) : P;
}

UG01HotPatchCommandlet::UG01HotPatchCommandlet()
{
    LogToConsole = true;
    IsClient = false;
    IsServer = false;
    IsEditor = true;
}

int32 UG01HotPatchCommandlet::Main(const FString& Params)
{
    UE_LOG(LogTemp, Display, TEXT("============================================"));
    UE_LOG(LogTemp, Display, TEXT(" G01 HotPatch Commandlet"));
    UE_LOG(LogTemp, Display, TEXT("============================================"));

    // Commandlet 进程的 AssetRegistry 默认不完整，必须先全量扫描
    // 否则依赖追踪只能发现直接引用，无法递归追踪完整依赖闭包
    if (IsRunningCommandlet())
    {
        UE_LOG(LogTemp, Display, TEXT("Loading AssetRegistry (SearchAllAssets)..."));
        FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
        AssetRegistryModule.Get().SearchAllAssets(true);
        UE_LOG(LogTemp, Display, TEXT("AssetRegistry ready."));
    }

    double StartTime = FPlatformTime::Seconds();
    FString BuildTimeStr = GetCurrentTimeISO8601();
    FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());

    FString ConfigPath;
    if (!FParse::Value(*Params, TEXT("-config="), ConfigPath))
    {
        UE_LOG(LogTemp, Error, TEXT("Usage: -run=G01HotPatch -config=<BuildTask.json>"));
        return 1;
    }
    ConfigPath = ToAbs(ConfigPath, ProjectDir);

    FG01BuildTask Task;
    if (!Task.LoadFromFile(ConfigPath))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to load: %s"), *ConfigPath);
        return 1;
    }

    TArray<FString> Errors = Task.Validate();
    if (Errors.Num() > 0)
    {
        for (const FString& E : Errors) UE_LOG(LogTemp, Error, TEXT("  %s"), *E);
        return 1;
    }

    UE_LOG(LogTemp, Display, TEXT("  TaskType:           %s"), *Task.TaskType);
    UE_LOG(LogTemp, Display, TEXT("  BasePackageVersion: %s"), *Task.BasePackageVersion);

    FString OutputRoot = ToAbs(Task.OutputDir, ProjectDir);
    FString HistPath = FPaths::Combine(OutputRoot, TEXT("BuildHistory.json"));

    if (Task.TaskType == TEXT("ExportRelease"))
    {
        FString RelDir = FPaths::Combine(OutputRoot, TEXT("Releases"), Task.BaseVersion);
        FString RelJson = FPaths::Combine(RelDir, Task.BaseVersion, Task.BaseVersion + TEXT("_Release.json"));
        FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*RelDir);
        return ExecuteExportRelease(Task, ProjectDir, RelDir, RelJson, BuildTimeStr, OutputRoot, HistPath, StartTime);
    }
    else if (Task.TaskType == TEXT("BuildPatch"))
    {
        FString RelDir = FPaths::Combine(OutputRoot, TEXT("Releases"), Task.BaseVersion);
        FString RelJson = FPaths::Combine(RelDir, Task.BaseVersion, Task.BaseVersion + TEXT("_Release.json"));
        return ExecuteBuildPatch(Task, ProjectDir, RelDir, RelJson, BuildTimeStr, OutputRoot, HistPath, StartTime);
    }
    else if (Task.TaskType == TEXT("PromoteToRelease"))
    {
        return ExecutePromoteToRelease(Task, ProjectDir, BuildTimeStr, OutputRoot, HistPath, StartTime);
    }

    return 1;
}

// ========================================================================
// ExportRelease
// ========================================================================
int32 UG01HotPatchCommandlet::ExecuteExportRelease(
    const FG01BuildTask& Task, const FString& ProjectDir,
    const FString& ReleasesDir, const FString& ReleaseJsonPath,
    const FString& BuildTimeStr, const FString& OutputRoot,
    const FString& HistPath, double StartTime)
{
    auto& PF = FPlatformFileManager::Get().GetPlatformFile();

    if (PF.FileExists(*ReleaseJsonPath))
    {
        UE_LOG(LogTemp, Error, TEXT("Release %s already exists: %s"), *Task.BaseVersion, *ReleaseJsonPath);
        UE_LOG(LogTemp, Error, TEXT("Delete manually to re-export."));
        return 5;
    }

    UE_LOG(LogTemp, Display, TEXT("Exporting Release %s (BasePackage=%s) ..."), *Task.BaseVersion, *Task.BasePackageVersion);
    UE_LOG(LogTemp, Warning, TEXT(">>> Confirm: workspace must be at %s state <<<"), *Task.BaseVersion);

    FString Err;
    bool bOk = FG01HotPatcherAdapter::ExportRelease(
        Task.BaseVersion, Task.Platform,
        ToAbs(Task.ReleaseConfigTemplate, ProjectDir),
        ReleasesDir, Err);

    if (!bOk) { UE_LOG(LogTemp, Error, TEXT("ExportRelease FAILED: %s"), *Err); return 2; }

    if (Task.Options.bGenerateBuildReport)
    {
        FG01BuildReport Report;
        Report.TaskType = TEXT("ExportRelease");
        Report.Platform = Task.Platform;
        Report.BasePackageVersion = Task.BasePackageVersion;
        Report.BaseVersion = Task.BaseVersion;
        Report.TargetVersion = Task.BaseVersion;
        Report.BuildTime = BuildTimeStr;
        Report.Duration = static_cast<float>(FPlatformTime::Seconds() - StartTime);
        Report.bSuccess = true;
        Report.SaveToFile(FPaths::Combine(ReleasesDir, FString::Printf(TEXT("BuildReport_%s.json"), *Task.BaseVersion)));

        FG01BuildHistory Hist; Hist.LoadFromFile(HistPath);
        FG01BuildHistoryEntry Entry;
        Entry.TargetVersion = Task.BaseVersion;
        Entry.BaseVersion = Task.BaseVersion;
        Entry.PatchType = TEXT("Release");
        Entry.Platform = Task.Platform;
        Entry.BasePackageVersion = Task.BasePackageVersion;
        Entry.BuildTime = BuildTimeStr;
        Entry.bSuccess = true;
        Entry.ReportPath = FString::Printf(TEXT("Releases/%s/BuildReport_%s.json"), *Task.BaseVersion, *Task.BaseVersion);
        Hist.AddEntry(Entry);

        // 注册 BasePackage（首次 ExportRelease 时自动注册为 active base package）
        FG01BasePackageInfo BP;
        BP.PackageVersion = Task.BasePackageVersion;
        BP.Platform = Task.Platform;
        BP.LinkedReleaseVersion = Task.BaseVersion;
        BP.BuildTime = BuildTimeStr;
        BP.bIsActiveBase = true;
        Hist.RegisterBasePackage(BP);

        Hist.SaveToFile(HistPath);
    }

    UE_LOG(LogTemp, Display, TEXT("EXPORT RELEASE COMPLETE: %s (%.1fs)"), *Task.BaseVersion, FPlatformTime::Seconds() - StartTime);
    return 0;
}

// ========================================================================
// BuildPatch
// ========================================================================
int32 UG01HotPatchCommandlet::ExecuteBuildPatch(
    const FG01BuildTask& Task, const FString& ProjectDir,
    const FString& ReleasesDir, const FString& ReleaseJsonPath,
    const FString& BuildTimeStr, const FString& OutputRoot,
    const FString& HistPath, double StartTime)
{
    auto& PF = FPlatformFileManager::Get().GetPlatformFile();

    if (!PF.FileExists(*ReleaseJsonPath))
    {
        UE_LOG(LogTemp, Error, TEXT("BASE RELEASE NOT FOUND: %s"), *ReleaseJsonPath);
        UE_LOG(LogTemp, Error, TEXT("Run ExportRelease under %s content state first."), *Task.BaseVersion);
        return 3;
    }

    FString PatchDir = FPaths::Combine(OutputRoot, TEXT("Patches"), Task.TargetVersion);
    PF.CreateDirectoryTree(*PatchDir);

    // 提前加载 History 用于跨链校验
    FG01BuildHistory Hist; Hist.LoadFromFile(HistPath);

    UE_LOG(LogTemp, Display, TEXT("Building Patch %s->%s (BasePackage=%s) ..."),
        *Task.BaseVersion, *Task.TargetVersion, *Task.BasePackageVersion);
    UE_LOG(LogTemp, Warning, TEXT(">>> Confirm: workspace must include %s changes <<<"), *Task.TargetVersion);

    FString Err;
    TArray<FString> PakPaths;
    bool bOk = FG01HotPatcherAdapter::BuildPatch(
        Task.BaseVersion, Task.TargetVersion, Task.Platform,
        ToAbs(Task.PatchConfigTemplate, ProjectDir),
        ReleaseJsonPath, PatchDir,
        Task.Options.bCookPatchAssets, Task.Options.bCompressPak,
        PakPaths, Err);

    if (!bOk) { UE_LOG(LogTemp, Error, TEXT("BuildPatch FAILED: %s"), *Err); return 4; }

    UE_LOG(LogTemp, Display, TEXT("Patch build OK: %d pak(s)"), PakPaths.Num());

    // 从 BuildHistory 校验 baseVersion Release 的 basePackageVersion 与本次 BuildTask 一致
    // 防止跨基础包链打 Patch（Release_1.0.3 是 BasePackage_1.0.0 的，不能用于 BasePackage_1.1.0 的 Patch）
    {
        bool bReleaseFound = false;
        for (const FG01BuildHistoryEntry& E : Hist.Entries)
        {
            if (E.PatchType == TEXT("Release") && E.TargetVersion == Task.BaseVersion)
            {
                bReleaseFound = true;
                if (!E.BasePackageVersion.IsEmpty() && E.BasePackageVersion != Task.BasePackageVersion)
                {
                    UE_LOG(LogTemp, Error, TEXT("============================================"));
                    UE_LOG(LogTemp, Error, TEXT(" BASE PACKAGE VERSION MISMATCH"));
                    UE_LOG(LogTemp, Error, TEXT("  Release_%s belongs to BasePackage: %s"), *Task.BaseVersion, *E.BasePackageVersion);
                    UE_LOG(LogTemp, Error, TEXT("  BuildTask.basePackageVersion:       %s"), *Task.BasePackageVersion);
                    UE_LOG(LogTemp, Error, TEXT("  Cannot build Patch across different base package chains."));
                    UE_LOG(LogTemp, Error, TEXT("  Fix: use basePackageVersion=%s in BuildTask."), *E.BasePackageVersion);
                    UE_LOG(LogTemp, Error, TEXT("============================================"));
                    return 9;
                }
                if (!E.BasePackageVersion.IsEmpty())
                {
                    UE_LOG(LogTemp, Display, TEXT("Chain verified: Release_%s belongs to BasePackage_%s"), *Task.BaseVersion, *E.BasePackageVersion);
                }
                else
                {
                    UE_LOG(LogTemp, Warning, TEXT("Release_%s has no BasePackageVersion in History. Chain validation skipped."), *Task.BaseVersion);
                }
                break;
            }
        }
        if (!bReleaseFound)
            UE_LOG(LogTemp, Warning, TEXT("Release_%s not found in BuildHistory. Chain validation skipped."), *Task.BaseVersion);
    }

    // CandidateRelease 处理
    FString CandidateReleasePath;
    {
        TArray<FString> ReleaseFiles;
        IFileManager::Get().FindFilesRecursive(ReleaseFiles, *PatchDir,
            *FString::Printf(TEXT("%s_Release.json"), *Task.TargetVersion), true, false);

        if (ReleaseFiles.Num() > 0)
        {
            CandidateReleasePath = FPaths::Combine(PatchDir,
                FString::Printf(TEXT("CandidateRelease_%s.json"), *Task.TargetVersion));
            PF.MoveFile(*CandidateReleasePath, *ReleaseFiles[0]);
            UE_LOG(LogTemp, Display, TEXT("CandidateRelease: %s"), *CandidateReleasePath);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("No CandidateRelease generated. PromoteToRelease will NOT be available."));
            UE_LOG(LogTemp, Warning, TEXT("Rebuild patch or use ExportRelease explicitly."));
        }
    }

    // MD5 + Manifest
    FG01VersionManifest Manifest;
    Manifest.Version = Task.TargetVersion;
    Manifest.BasePackageVersion = Task.BasePackageVersion;
    Manifest.BaseVersion = Task.BaseVersion;
    Manifest.PatchType = Task.PatchType;
    Manifest.Platform = Task.Platform;
    Manifest.BuildTime = BuildTimeStr;

    int64 TotalSize = 0;
    FString FirstMD5;

    for (const FString& PakPath : PakPaths)
    {
        FG01ManifestFileInfo Fi;
        Fi.Name = FPaths::GetCleanFilename(PakPath);
        Fi.Size = IFileManager::Get().FileSize(*PakPath);
        TotalSize += Fi.Size;
        if (Task.Options.bCalculateMD5)
        {
            FString MD5;
            if (ComputeFileMD5(PakPath, MD5))
            {
                Fi.MD5 = MD5;
                if (FirstMD5.IsEmpty()) FirstMD5 = MD5;
                UE_LOG(LogTemp, Display, TEXT("  %s: %lld bytes, MD5=%s"), *Fi.Name, Fi.Size, *MD5);
            }
        }
        Manifest.Files.Add(Fi);
    }

    if (Task.Options.bGenerateManifest && PakPaths.Num() > 0)
        Manifest.SaveToFile(FPaths::Combine(PatchDir, FString::Printf(TEXT("VersionManifest_%s.json"), *Task.TargetVersion)));

    if (Task.Options.bGenerateBuildReport)
    {
        FG01BuildReport Report;
        Report.TaskType = TEXT("BuildPatch");
        Report.Platform = Task.Platform;
        Report.PatchType = Task.PatchType;
        Report.BasePackageVersion = Task.BasePackageVersion;
        Report.BaseVersion = Task.BaseVersion;
        Report.TargetVersion = Task.TargetVersion;
        Report.BuildTime = BuildTimeStr;
        Report.Duration = static_cast<float>(FPlatformTime::Seconds() - StartTime);
        Report.bSuccess = true;
        for (const FG01ManifestFileInfo& F : Manifest.Files)
        {
            FG01BuildReportOutput O;
            O.Type = TEXT("Pak"); O.Path = F.Name; O.Size = F.Size; O.MD5 = F.MD5;
            Report.Outputs.Add(O);
        }
        Report.SaveToFile(FPaths::Combine(PatchDir, FString::Printf(TEXT("BuildReport_%s.json"), *Task.TargetVersion)));

        FG01BuildHistoryEntry Entry;
        Entry.TargetVersion = Task.TargetVersion;
        Entry.BaseVersion = Task.BaseVersion;
        Entry.PatchType = Task.PatchType;
        Entry.Platform = Task.Platform;
        Entry.BasePackageVersion = Task.BasePackageVersion;
        Entry.BuildTime = BuildTimeStr;
        Entry.bSuccess = true;
        Entry.TotalPakSize = TotalSize;
        Entry.PakMD5 = FirstMD5;
        Entry.CandidateReleasePath = CandidateReleasePath;
        Entry.ReportPath = FString::Printf(TEXT("Patches/%s/BuildReport_%s.json"), *Task.TargetVersion, *Task.TargetVersion);
        Hist.AddEntry(Entry);
        Hist.SaveToFile(HistPath);
    }

    UE_LOG(LogTemp, Display, TEXT("BUILD PATCH COMPLETE: %s->%s BasePackage=%s (%.1fs)"),
        *Task.BaseVersion, *Task.TargetVersion, *Task.BasePackageVersion, FPlatformTime::Seconds() - StartTime);
    return 0;
}

// ========================================================================
// PromoteToRelease - 从 CandidateRelease 复制为正式 Release，继承 basePackageVersion
// ========================================================================
int32 UG01HotPatchCommandlet::ExecutePromoteToRelease(
    const FG01BuildTask& Task, const FString& ProjectDir,
    const FString& BuildTimeStr, const FString& OutputRoot,
    const FString& HistPath, double StartTime)
{
    auto& PF = FPlatformFileManager::Get().GetPlatformFile();

    FG01BuildHistory Hist; Hist.LoadFromFile(HistPath);

    // 找源 Patch，同时继承其 basePackageVersion
    FString CandidatePath;
    FString InheritedBasePackageVersion = Task.BasePackageVersion;
    bool bFoundPatch = false;

    for (const FG01BuildHistoryEntry& E : Hist.Entries)
    {
        if (E.PatchType != TEXT("Release") && E.TargetVersion == Task.PromoteFromPatchVersion && E.bSuccess)
        {
            bFoundPatch = true;
            CandidatePath = E.CandidateReleasePath;
            // 继承 Patch 记录的 basePackageVersion（比 Task 里的更可信）
            if (!E.BasePackageVersion.IsEmpty())
                InheritedBasePackageVersion = E.BasePackageVersion;
            break;
        }
    }

    if (!bFoundPatch)
    {
        UE_LOG(LogTemp, Error, TEXT("No successful Patch found for: %s"), *Task.PromoteFromPatchVersion);
        return 6;
    }

    // 校验 CandidateRelease
    if (CandidatePath.IsEmpty() || !PF.FileExists(*CandidatePath))
    {
        UE_LOG(LogTemp, Error, TEXT("CANDIDATE RELEASE NOT FOUND: %s"), *CandidatePath);
        UE_LOG(LogTemp, Error, TEXT("Rebuild patch or use ExportRelease explicitly."));
        return 7;
    }

    // 校验 JSON 可解析 + 版本号匹配
    {
        FString CJson;
        if (!FFileHelper::LoadFileToString(CJson, *CandidatePath))
        { UE_LOG(LogTemp, Error, TEXT("CandidateRelease unreadable")); return 7; }

        TSharedPtr<FJsonObject> CO;
        auto CR = TJsonReaderFactory<>::Create(CJson);
        if (!FJsonSerializer::Deserialize(CR, CO) || !CO.IsValid())
        { UE_LOG(LogTemp, Error, TEXT("CandidateRelease JSON parse failed")); return 7; }

        FString CV = CO->GetStringField(TEXT("VersionId"));
        if (CV != Task.TargetVersion)
        {
            UE_LOG(LogTemp, Error, TEXT("CandidateRelease version mismatch: file=%s, expected=%s"), *CV, *Task.TargetVersion);
            return 7;
        }
        UE_LOG(LogTemp, Display, TEXT("CandidateRelease validated: version=%s"), *CV);
    }

    // 检查目标 Release 是否已存在
    FString RelDir = FPaths::Combine(OutputRoot, TEXT("Releases"), Task.TargetVersion);
    FString RelJson = FPaths::Combine(RelDir, Task.TargetVersion, Task.TargetVersion + TEXT("_Release.json"));
    if (PF.FileExists(*RelJson))
    {
        UE_LOG(LogTemp, Error, TEXT("Release %s already exists"), *Task.TargetVersion);
        return 5;
    }

    PF.CreateDirectoryTree(*FPaths::Combine(RelDir, Task.TargetVersion));

    UE_LOG(LogTemp, Display, TEXT("Promoting Patch %s -> Release %s (BasePackage=%s) ..."),
        *Task.PromoteFromPatchVersion, *Task.TargetVersion, *InheritedBasePackageVersion);
    UE_LOG(LogTemp, Warning, TEXT(">>> Confirm: workspace must be at %s state <<<"), *Task.TargetVersion);

    if (!PF.CopyFile(*RelJson, *CandidatePath))
    { UE_LOG(LogTemp, Error, TEXT("Failed to copy CandidateRelease")); return 8; }

    if (Task.Options.bGenerateBuildReport)
    {
        FG01BuildReport Report;
        Report.TaskType = TEXT("PromoteToRelease");
        Report.Platform = Task.Platform;
        Report.BasePackageVersion = InheritedBasePackageVersion;
        Report.BaseVersion = Task.PromoteFromPatchVersion;
        Report.TargetVersion = Task.TargetVersion;
        Report.BuildTime = BuildTimeStr;
        Report.Duration = static_cast<float>(FPlatformTime::Seconds() - StartTime);
        Report.bSuccess = true;
        Report.SaveToFile(FPaths::Combine(RelDir, FString::Printf(TEXT("BuildReport_%s.json"), *Task.TargetVersion)));

        FG01BuildHistoryEntry Entry;
        Entry.TargetVersion = Task.TargetVersion;
        Entry.BaseVersion = Task.TargetVersion;
        Entry.PatchType = TEXT("Release");
        Entry.Platform = Task.Platform;
        Entry.BasePackageVersion = InheritedBasePackageVersion;  // 从源 Patch 继承
        Entry.BuildTime = BuildTimeStr;
        Entry.bSuccess = true;
        Entry.PromotedFromPatch = Task.PromoteFromPatchVersion;
        Entry.CandidateReleasePath = CandidatePath;
        Entry.ReportPath = FString::Printf(TEXT("Releases/%s/BuildReport_%s.json"), *Task.TargetVersion, *Task.TargetVersion);
        Hist.AddEntry(Entry);
        Hist.SaveToFile(HistPath);
    }

    UE_LOG(LogTemp, Display, TEXT("PROMOTE COMPLETE: %s -> Release (BasePackage=%s, %.1fs)"),
        *Task.TargetVersion, *InheritedBasePackageVersion, FPlatformTime::Seconds() - StartTime);
    return 0;
}

// ========================================================================

bool UG01HotPatchCommandlet::ComputeFileMD5(const FString& FilePath, FString& OutMD5)
{
    const int32 BufSize = 4 * 1024 * 1024;
    TUniquePtr<IFileHandle> Handle(FPlatformFileManager::Get().GetPlatformFile().OpenRead(*FilePath));
    if (!Handle) return false;

    FMD5 MD5;
    TArray<uint8> Buf;
    Buf.SetNumUninitialized(BufSize);
    int64 Rem = Handle->Size();
    while (Rem > 0)
    {
        int32 R = FMath::Min(static_cast<int64>(BufSize), Rem);
        if (!Handle->Read(Buf.GetData(), R)) return false;
        MD5.Update(Buf.GetData(), R);
        Rem -= R;
    }
    uint8 Digest[16]; MD5.Final(Digest);
    OutMD5.Empty();
    for (int32 i = 0; i < 16; ++i) OutMD5 += FString::Printf(TEXT("%02x"), Digest[i]);
    return true;
}

FString UG01HotPatchCommandlet::GetCurrentTimeISO8601()
{
    return FDateTime::UtcNow().ToIso8601();
}
