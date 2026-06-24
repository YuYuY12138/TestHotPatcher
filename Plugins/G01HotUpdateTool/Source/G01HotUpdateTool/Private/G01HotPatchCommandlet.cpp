#include "G01HotPatchCommandlet.h"
#include "G01HotPatcherAdapter.h"
#include "G01VersionManifest.h"
#include "G01BuildReport.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"

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

    UE_LOG(LogTemp, Display, TEXT("  TaskType: %s"), *Task.TaskType);
    FString OutputRoot = ToAbs(Task.OutputDir, ProjectDir);

    if (Task.TaskType == TEXT("ExportRelease"))
    {
        FString RelDir = FPaths::Combine(OutputRoot, TEXT("Releases"), Task.BaseVersion);
        FString RelJson = FPaths::Combine(RelDir, Task.BaseVersion, Task.BaseVersion + TEXT("_Release.json"));
        FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*RelDir);
        return ExecuteExportRelease(Task, ProjectDir, RelDir, RelJson, BuildTimeStr, OutputRoot, StartTime);
    }
    else if (Task.TaskType == TEXT("BuildPatch"))
    {
        FString RelDir = FPaths::Combine(OutputRoot, TEXT("Releases"), Task.BaseVersion);
        FString RelJson = FPaths::Combine(RelDir, Task.BaseVersion, Task.BaseVersion + TEXT("_Release.json"));
        return ExecuteBuildPatch(Task, ProjectDir, RelDir, RelJson, BuildTimeStr, OutputRoot, StartTime);
    }
    else if (Task.TaskType == TEXT("PromoteToRelease"))
    {
        return ExecutePromoteToRelease(Task, ProjectDir, BuildTimeStr, OutputRoot, StartTime);
    }

    return 1;
}

// ========================================================================
// ExportRelease - 归档当前工作区为指定版本 Release
// ========================================================================
int32 UG01HotPatchCommandlet::ExecuteExportRelease(
    const FG01BuildTask& Task, const FString& ProjectDir,
    const FString& ReleasesDir, const FString& ReleaseJsonPath,
    const FString& BuildTimeStr, const FString& OutputRoot, double StartTime)
{
    auto& PF = FPlatformFileManager::Get().GetPlatformFile();

    if (PF.FileExists(*ReleaseJsonPath))
    {
        UE_LOG(LogTemp, Error, TEXT("Release %s already exists: %s"), *Task.BaseVersion, *ReleaseJsonPath);
        UE_LOG(LogTemp, Error, TEXT("Delete manually to re-export."));
        return 5;
    }

    UE_LOG(LogTemp, Display, TEXT("Exporting Release %s ..."), *Task.BaseVersion);
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
        Report.BaseVersion = Task.BaseVersion;
        Report.TargetVersion = Task.BaseVersion;
        Report.BuildTime = BuildTimeStr;
        Report.Duration = static_cast<float>(FPlatformTime::Seconds() - StartTime);
        Report.bSuccess = true;
        Report.SaveToFile(FPaths::Combine(ReleasesDir, FString::Printf(TEXT("BuildReport_%s.json"), *Task.BaseVersion)));

        FString HistPath = FPaths::Combine(OutputRoot, TEXT("BuildHistory.json"));
        FG01BuildHistory Hist; Hist.LoadFromFile(HistPath);

        FG01BuildHistoryEntry Entry;
        Entry.TargetVersion = Task.BaseVersion;
        Entry.BaseVersion = Task.BaseVersion;
        Entry.PatchType = TEXT("Release");
        Entry.Platform = Task.Platform;
        Entry.BuildTime = BuildTimeStr;
        Entry.bSuccess = true;
        Entry.ReportPath = FString::Printf(TEXT("Releases/%s/BuildReport_%s.json"), *Task.BaseVersion, *Task.BaseVersion);
        Hist.AddEntry(Entry);
        Hist.SaveToFile(HistPath);
    }

    UE_LOG(LogTemp, Display, TEXT("EXPORT RELEASE COMPLETE: %s (%.1fs)"), *Task.BaseVersion, FPlatformTime::Seconds() - StartTime);
    return 0;
}

// ========================================================================
// BuildPatch - 基于已有 Release 生成差异补丁 + 同步生成 CandidateRelease
//
// bStorageNewRelease=true 让 HotPatcher 在 Patch 产物目录下同步生成
// 一份 Release JSON。这份 Release 和 Patch 在同一时刻从同一工作区生成，
// 内容一致性有保证。我们把它重命名为 CandidateRelease，
// 供后续 PromoteToRelease 使用。
// ========================================================================
int32 UG01HotPatchCommandlet::ExecuteBuildPatch(
    const FG01BuildTask& Task, const FString& ProjectDir,
    const FString& ReleasesDir, const FString& ReleaseJsonPath,
    const FString& BuildTimeStr, const FString& OutputRoot, double StartTime)
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

    UE_LOG(LogTemp, Display, TEXT("Building Patch %s (base=%s) ..."), *Task.TargetVersion, *Task.BaseVersion);
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

    // ---- 处理 CandidateRelease ----
    // HotPatcher 的 bStorageNewRelease=true 会在 PatchDir/{targetVersion}/ 下生成
    // {targetVersion}_Release.json，我们把它重命名为 CandidateRelease
    FString CandidateReleasePath;
    {
        // 搜索 HotPatcher 生成的 Release JSON（可能在子目录下）
        TArray<FString> ReleaseFiles;
        IFileManager::Get().FindFilesRecursive(ReleaseFiles, *PatchDir,
            *FString::Printf(TEXT("%s_Release.json"), *Task.TargetVersion), true, false);

        if (ReleaseFiles.Num() > 0)
        {
            FString SrcRelease = ReleaseFiles[0];
            CandidateReleasePath = FPaths::Combine(PatchDir,
                FString::Printf(TEXT("CandidateRelease_%s.json"), *Task.TargetVersion));
            PF.MoveFile(*CandidateReleasePath, *SrcRelease);
            UE_LOG(LogTemp, Display, TEXT("CandidateRelease saved: %s"), *CandidateReleasePath);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("No CandidateRelease generated by HotPatcher."));
            UE_LOG(LogTemp, Warning, TEXT("PromoteToRelease will NOT be available for this version."));
            UE_LOG(LogTemp, Warning, TEXT("To promote, rebuild the patch or use ExportRelease explicitly."));
        }
    }

    // ---- MD5 + Manifest ----
    FG01VersionManifest Manifest;
    Manifest.Version = Task.TargetVersion;
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
    {
        FString ManPath = FPaths::Combine(PatchDir, FString::Printf(TEXT("VersionManifest_%s.json"), *Task.TargetVersion));
        Manifest.SaveToFile(ManPath);
    }

    // ---- Report + History ----
    if (Task.Options.bGenerateBuildReport)
    {
        FG01BuildReport Report;
        Report.TaskType = TEXT("BuildPatch");
        Report.Platform = Task.Platform;
        Report.PatchType = Task.PatchType;
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

        FString HistPath = FPaths::Combine(OutputRoot, TEXT("BuildHistory.json"));
        FG01BuildHistory Hist; Hist.LoadFromFile(HistPath);

        FG01BuildHistoryEntry Entry;
        Entry.TargetVersion = Task.TargetVersion;
        Entry.BaseVersion = Task.BaseVersion;
        Entry.PatchType = Task.PatchType;
        Entry.Platform = Task.Platform;
        Entry.BuildTime = BuildTimeStr;
        Entry.bSuccess = true;
        Entry.TotalPakSize = TotalSize;
        Entry.PakMD5 = FirstMD5;
        Entry.CandidateReleasePath = CandidateReleasePath;
        Entry.ReportPath = FString::Printf(TEXT("Patches/%s/BuildReport_%s.json"), *Task.TargetVersion, *Task.TargetVersion);
        Hist.AddEntry(Entry);
        Hist.SaveToFile(HistPath);
    }

    UE_LOG(LogTemp, Display, TEXT("BUILD PATCH COMPLETE: %s -> %s (%d paks, %.1fs)"),
        *Task.BaseVersion, *Task.TargetVersion, PakPaths.Num(), FPlatformTime::Seconds() - StartTime);
    return 0;
}

// ========================================================================
// PromoteToRelease - 从 CandidateRelease 复制为正式 Release
//
// 不重新扫描工作区。CandidateRelease 是 BuildPatch 时同步生成的，
// 和 Patch Pak 在同一时刻从同一工作区产生，内容一致性有保证。
// ========================================================================
int32 UG01HotPatchCommandlet::ExecutePromoteToRelease(
    const FG01BuildTask& Task, const FString& ProjectDir,
    const FString& BuildTimeStr, const FString& OutputRoot, double StartTime)
{
    auto& PF = FPlatformFileManager::Get().GetPlatformFile();

    // 1. 从 History 里找到源 Patch 记录
    FString HistPath = FPaths::Combine(OutputRoot, TEXT("BuildHistory.json"));
    FG01BuildHistory Hist;
    Hist.LoadFromFile(HistPath);

    FString CandidatePath;
    bool bFoundPatch = false;
    for (const FG01BuildHistoryEntry& E : Hist.Entries)
    {
        if (E.PatchType != TEXT("Release") && E.TargetVersion == Task.PromoteFromPatchVersion && E.bSuccess)
        {
            bFoundPatch = true;
            CandidatePath = E.CandidateReleasePath;
            break;
        }
    }

    if (!bFoundPatch)
    {
        UE_LOG(LogTemp, Error, TEXT("No successful Patch found for version: %s"), *Task.PromoteFromPatchVersion);
        return 6;
    }

    // 2. 检查 CandidateRelease 是否存在
    if (CandidatePath.IsEmpty() || !PF.FileExists(*CandidatePath))
    {
        // CandidateRelease 不存在，不静默重新扫描
        UE_LOG(LogTemp, Error, TEXT("============================================"));
        UE_LOG(LogTemp, Error, TEXT(" CANDIDATE RELEASE NOT FOUND"));
        UE_LOG(LogTemp, Error, TEXT("  Expected: %s"), *CandidatePath);
        UE_LOG(LogTemp, Error, TEXT(""));
        UE_LOG(LogTemp, Error, TEXT("  CandidateRelease is generated during BuildPatch."));
        UE_LOG(LogTemp, Error, TEXT("  If missing, please rebuild the patch or use"));
        UE_LOG(LogTemp, Error, TEXT("  ExportRelease explicitly under correct workspace state."));
        UE_LOG(LogTemp, Error, TEXT("============================================"));
        return 7;
    }

    // 3. 检查目标 Release 是否已存在
    FString RelDir = FPaths::Combine(OutputRoot, TEXT("Releases"), Task.TargetVersion);
    FString RelJson = FPaths::Combine(RelDir, Task.TargetVersion, Task.TargetVersion + TEXT("_Release.json"));

    if (PF.FileExists(*RelJson))
    {
        UE_LOG(LogTemp, Error, TEXT("Release %s already exists: %s"), *Task.TargetVersion, *RelJson);
        UE_LOG(LogTemp, Error, TEXT("Cannot overwrite. Delete manually if needed."));
        return 5;
    }

    // 3.5 校验 CandidateRelease 内容
    {
        FString CandidateJson;
        if (!FFileHelper::LoadFileToString(CandidateJson, *CandidatePath))
        {
            UE_LOG(LogTemp, Error, TEXT("CandidateRelease file unreadable: %s"), *CandidatePath);
            return 7;
        }

        TSharedPtr<FJsonObject> CandidateObj;
        auto Reader = TJsonReaderFactory<>::Create(CandidateJson);
        if (!FJsonSerializer::Deserialize(Reader, CandidateObj) || !CandidateObj.IsValid())
        {
            UE_LOG(LogTemp, Error, TEXT("CandidateRelease JSON parse failed: %s"), *CandidatePath);
            return 7;
        }

        FString CandidateVersion = CandidateObj->GetStringField(TEXT("VersionId"));
        if (CandidateVersion != Task.TargetVersion)
        {
            UE_LOG(LogTemp, Error, TEXT("CandidateRelease version mismatch: file=%s, expected=%s"),
                *CandidateVersion, *Task.TargetVersion);
            return 7;
        }

        UE_LOG(LogTemp, Display, TEXT("CandidateRelease validated: version=%s"), *CandidateVersion);
    }

    // 4. 复制 CandidateRelease → 正式 Release
    FString DestDir = FPaths::Combine(RelDir, Task.TargetVersion);
    PF.CreateDirectoryTree(*DestDir);

    UE_LOG(LogTemp, Display, TEXT("Promoting: %s"), *CandidatePath);
    UE_LOG(LogTemp, Display, TEXT("      To: %s"), *RelJson);

    if (!PF.CopyFile(*RelJson, *CandidatePath))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to copy CandidateRelease to Releases directory"));
        return 8;
    }

    // 5. 记录 History
    if (Task.Options.bGenerateBuildReport)
    {
        FG01BuildReport Report;
        Report.TaskType = TEXT("PromoteToRelease");
        Report.Platform = Task.Platform;
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
        Entry.BuildTime = BuildTimeStr;
        Entry.bSuccess = true;
        Entry.PromotedFromPatch = Task.PromoteFromPatchVersion;
        Entry.CandidateReleasePath = CandidatePath;
        Entry.ReportPath = FString::Printf(TEXT("Releases/%s/BuildReport_%s.json"), *Task.TargetVersion, *Task.TargetVersion);
        Hist.AddEntry(Entry);
        Hist.SaveToFile(HistPath);
    }

    UE_LOG(LogTemp, Display, TEXT("PROMOTE COMPLETE: %s is now an official Release (from Patch %s, %.1fs)"),
        *Task.TargetVersion, *Task.PromoteFromPatchVersion, FPlatformTime::Seconds() - StartTime);
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

    uint8 Digest[16];
    MD5.Final(Digest);
    OutMD5.Empty();
    for (int32 i = 0; i < 16; ++i)
        OutMD5 += FString::Printf(TEXT("%02x"), Digest[i]);
    return true;
}

FString UG01HotPatchCommandlet::GetCurrentTimeISO8601()
{
    return FDateTime::UtcNow().ToIso8601();
}
