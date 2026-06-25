#include "G01HotPatcherAdapter.h"
#include "CreatePatch/ReleaseProxy.h"
#include "CreatePatch/PatcherProxy.h"
#include "CreatePatch/FExportReleaseSettings.h"
#include "CreatePatch/FExportPatchSettings.h"
#include "Templates/HotPatcherTemplateHelper.hpp"
#include "ETargetPlatform.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

static void DumpSettingsToFile(const FString& DumpPath, const FString& JsonContent)
{
    if (FFileHelper::SaveStringToFile(JsonContent, *DumpPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        UE_LOG(LogTemp, Log, TEXT("[Adapter][DUMP] Settings dumped to: %s"), *DumpPath);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[Adapter][DUMP] Failed to write dump: %s"), *DumpPath);
    }
}

bool FG01HotPatcherAdapter::ExportRelease(
    const FString& Version,
    const FString& Platform,
    const FString& TemplatePath,
    const FString& OutputDir,
    FString& OutError)
{
    FString FullTemplatePath = ResolvePath(TemplatePath);

    UE_LOG(LogTemp, Log, TEXT("[Adapter][DIAG] ============ ExportRelease ============"));
    UE_LOG(LogTemp, Log, TEXT("[Adapter][DIAG] Version:      %s"), *Version);
    UE_LOG(LogTemp, Log, TEXT("[Adapter][DIAG] Platform:     %s"), *Platform);
    UE_LOG(LogTemp, Log, TEXT("[Adapter][DIAG] Template:     %s"), *FullTemplatePath);
    UE_LOG(LogTemp, Log, TEXT("[Adapter][DIAG] OutputDir:    %s"), *OutputDir);

    FString JsonContent;
    if (!FFileHelper::LoadFileToString(JsonContent, *FullTemplatePath))
    {
        OutError = FString::Printf(TEXT("Failed to load release template: %s"), *FullTemplatePath);
        return false;
    }

    FExportReleaseSettings Settings;
    if (!THotPatcherTemplateHelper::TDeserializeJsonStringAsStruct(JsonContent, Settings))
    {
        OutError = FString::Printf(TEXT("Failed to parse release template JSON: %s"), *FullTemplatePath);
        return false;
    }

    Settings.VersionId = Version;
    Settings.SavePath.Path = OutputDir;
    Settings.bStandaloneMode = false;

    // byPakList 模式：替换 PakFiles/PakResponseFiles 路径中的 [PROJECTDIR]
    if (Settings.IsByPakList())
    {
        for (FPlatformPakListFiles& PlatPak : Settings.PlatformsPakListFiles)
        {
            for (FFilePath& PakFile : PlatPak.PakFiles)
            {
                PakFile.FilePath = ResolvePath(PakFile.FilePath);
                UE_LOG(LogTemp, Log, TEXT("[Adapter][DIAG] PakFile: %s"), *PakFile.FilePath);
            }
            for (FFilePath& RespFile : PlatPak.PakResponseFiles)
            {
                RespFile.FilePath = ResolvePath(RespFile.FilePath);
                UE_LOG(LogTemp, Log, TEXT("[Adapter][DIAG] PakResponseFile: %s"), *RespFile.FilePath);
            }
        }
        UE_LOG(LogTemp, Log, TEXT("[Adapter][DIAG] byPakList=true, %d platform(s)"), Settings.PlatformsPakListFiles.Num());
    }

    FString FinalJson;
    THotPatcherTemplateHelper::TSerializeStructAsJsonString(Settings, FinalJson);
    FString DumpPath = FPaths::Combine(OutputDir, FString::Printf(TEXT("Dump_FinalReleaseConfig_%s.json"), *Version));
    DumpSettingsToFile(DumpPath, FinalJson);
    UE_LOG(LogTemp, Log, TEXT("[Adapter][DIAG] FinalConfigDump: %s"), *DumpPath);
    UE_LOG(LogTemp, Log, TEXT("[Adapter][DIAG] ============================================="));

    UReleaseProxy* Proxy = NewObject<UReleaseProxy>();
    Proxy->AddToRoot();
    Proxy->Init(&Settings);
    Proxy->OnShowMsg.AddLambda([](const FString& Msg)
    {
        UE_LOG(LogTemp, Log, TEXT("[HotPatcher Release] %s"), *Msg);
    });

    bool bSuccess = Proxy->DoExport();
    Proxy->RemoveFromRoot();

    if (!bSuccess)
    {
        OutError = TEXT("UReleaseProxy::DoExport() returned false");
    }

    return bSuccess;
}

bool FG01HotPatcherAdapter::BuildPatch(
    const FString& BaseVersion,
    const FString& TargetVersion,
    const FString& Platform,
    const FString& TemplatePath,
    const FString& BaseReleaseJson,
    const FString& OutputDir,
    bool bCookAssets,
    bool bCompress,
    TArray<FString>& OutPakPaths,
    FString& OutError)
{
    FString FullTemplatePath = ResolvePath(TemplatePath);
    FString ResolvedBaseRelease = ResolvePath(BaseReleaseJson);

    UE_LOG(LogTemp, Log, TEXT("[Adapter][DIAG] ============ BuildPatch ============"));
    UE_LOG(LogTemp, Log, TEXT("[Adapter][DIAG] BaseVersion:     %s"), *BaseVersion);
    UE_LOG(LogTemp, Log, TEXT("[Adapter][DIAG] TargetVersion:   %s"), *TargetVersion);
    UE_LOG(LogTemp, Log, TEXT("[Adapter][DIAG] Platform:        %s"), *Platform);
    UE_LOG(LogTemp, Log, TEXT("[Adapter][DIAG] PatchTemplate:   %s"), *FullTemplatePath);
    UE_LOG(LogTemp, Log, TEXT("[Adapter][DIAG] BaseRelease:     %s"), *ResolvedBaseRelease);
    UE_LOG(LogTemp, Log, TEXT("[Adapter][DIAG] OutputDir:       %s"), *OutputDir);
    UE_LOG(LogTemp, Log, TEXT("[Adapter][DIAG] bCookAssets:     %s"), bCookAssets ? TEXT("true") : TEXT("false"));

    if (!IFileManager::Get().FileExists(*ResolvedBaseRelease))
    {
        UE_LOG(LogTemp, Error, TEXT("[Adapter][DIAG] BaseRelease NOT FOUND: %s"), *ResolvedBaseRelease);
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("[Adapter][DIAG] BaseRelease exists: OK"));
    }

    FString JsonContent;
    if (!FFileHelper::LoadFileToString(JsonContent, *FullTemplatePath))
    {
        OutError = FString::Printf(TEXT("Failed to load patch template: %s"), *FullTemplatePath);
        return false;
    }

    FExportPatchSettings Settings;
    if (!THotPatcherTemplateHelper::TDeserializeJsonStringAsStruct(JsonContent, Settings))
    {
        OutError = FString::Printf(TEXT("Failed to parse patch template JSON: %s"), *FullTemplatePath);
        return false;
    }

    Settings.VersionId = TargetVersion;
    Settings.bByBaseVersion = true;
    Settings.BaseVersion.FilePath = ResolvedBaseRelease;
    Settings.SavePath.Path = OutputDir;
    Settings.bStandaloneMode = false;
    Settings.bCookPatchAssets = bCookAssets;
    Settings.bStorageNewRelease = true;

    int32 PlatformEnumValue;
    if (!GetTargetPlatformEnum(Platform, PlatformEnumValue))
    {
        OutError = FString::Printf(TEXT("Unsupported platform: %s"), *Platform);
        return false;
    }
    Settings.PakTargetPlatforms.Empty();
    Settings.PakTargetPlatforms.Add(static_cast<ETargetPlatform>(PlatformEnumValue));

    FString FinalJson;
    THotPatcherTemplateHelper::TSerializeStructAsJsonString(Settings, FinalJson);
    FString DumpPath = FPaths::Combine(OutputDir, FString::Printf(TEXT("Dump_FinalPatchConfig_%s.json"), *TargetVersion));
    DumpSettingsToFile(DumpPath, FinalJson);
    UE_LOG(LogTemp, Log, TEXT("[Adapter][DIAG] FinalConfigDump: %s"), *DumpPath);

    UE_LOG(LogTemp, Log, TEXT("[Adapter][DIAG] --- Key Settings ---"));
    UE_LOG(LogTemp, Log, TEXT("[Adapter][DIAG] VersionId:           %s"), *Settings.VersionId);
    UE_LOG(LogTemp, Log, TEXT("[Adapter][DIAG] bByBaseVersion:      %s"), Settings.bByBaseVersion ? TEXT("true") : TEXT("false"));
    UE_LOG(LogTemp, Log, TEXT("[Adapter][DIAG] BaseReleasePath:     %s"), *Settings.BaseVersion.FilePath);
    UE_LOG(LogTemp, Log, TEXT("[Adapter][DIAG] SavePath:            %s"), *Settings.SavePath.Path);
    UE_LOG(LogTemp, Log, TEXT("[Adapter][DIAG] bCookPatchAssets:    %s"), Settings.bCookPatchAssets ? TEXT("true") : TEXT("false"));
    UE_LOG(LogTemp, Log, TEXT("[Adapter][DIAG] bStandaloneMode:     %s"), Settings.bStandaloneMode ? TEXT("true") : TEXT("false"));
    UE_LOG(LogTemp, Log, TEXT("[Adapter][DIAG] bStorageNewRelease:  %s"), Settings.bStorageNewRelease ? TEXT("true") : TEXT("false"));
    UE_LOG(LogTemp, Log, TEXT("[Adapter][DIAG] bAnalysisFilterDeps: %s"), Settings.AssetScanConfig.bAnalysisFilterDependencies ? TEXT("true") : TEXT("false"));
    UE_LOG(LogTemp, Log, TEXT("[Adapter][DIAG] bForceSkipContent:   %s"), Settings.AssetScanConfig.bForceSkipContent ? TEXT("true") : TEXT("false"));
    UE_LOG(LogTemp, Log, TEXT("[Adapter][DIAG] IncludeFilters: %d"), Settings.AssetScanConfig.AssetIncludeFilters.Num());
    for (const auto& F : Settings.AssetScanConfig.AssetIncludeFilters)
    {
        UE_LOG(LogTemp, Log, TEXT("[Adapter][DIAG]   + %s"), *F.Path);
    }
    UE_LOG(LogTemp, Log, TEXT("[Adapter][DIAG] IgnoreFilters: %d"), Settings.AssetScanConfig.AssetIgnoreFilters.Num());
    for (const auto& F : Settings.AssetScanConfig.AssetIgnoreFilters)
    {
        UE_LOG(LogTemp, Log, TEXT("[Adapter][DIAG]   - %s"), *F.Path);
    }
    UE_LOG(LogTemp, Log, TEXT("[Adapter][DIAG] ForceSkipRules: %d"), Settings.AssetScanConfig.ForceSkipContentRules.Num());
    for (const auto& F : Settings.AssetScanConfig.ForceSkipContentRules)
    {
        UE_LOG(LogTemp, Log, TEXT("[Adapter][DIAG]   skip: %s"), *F.Path);
    }
    UE_LOG(LogTemp, Log, TEXT("[Adapter][DIAG] ============================================="));

    UPatcherProxy* Proxy = NewObject<UPatcherProxy>();
    Proxy->AddToRoot();
    Proxy->Init(&Settings);
    Proxy->OnShowMsg.AddLambda([](const FString& Msg)
    {
        UE_LOG(LogTemp, Log, TEXT("[HotPatcher Patch] %s"), *Msg);
    });

    bool bSuccess = Proxy->DoExport();
    Proxy->RemoveFromRoot();

    if (!bSuccess)
    {
        OutError = TEXT("UPatcherProxy::DoExport() returned false");
        return false;
    }

    IFileManager& FM = IFileManager::Get();
    TArray<FString> FoundFiles;
    FM.FindFilesRecursive(FoundFiles, *OutputDir, TEXT("*.pak"), true, false);
    for (const FString& F : FoundFiles)
    {
        OutPakPaths.Add(F);
    }

    if (OutPakPaths.Num() == 0)
    {
        OutError = FString::Printf(TEXT("Patch succeeded but no .pak files found in: %s"), *OutputDir);
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("[Adapter] BuildPatch completed: %d pak file(s)"), OutPakPaths.Num());
    return true;
}

bool FG01HotPatcherAdapter::GetTargetPlatformEnum(const FString& PlatformName, int32& OutEnumValue)
{
    const UEnum* Enum = StaticEnum<ETargetPlatform>();
    if (Enum)
    {
        int64 Value = Enum->GetValueByNameString(PlatformName);
        if (Value != INDEX_NONE)
        {
            OutEnumValue = static_cast<int32>(Value);
            return true;
        }
    }

    UE_LOG(LogTemp, Error, TEXT("[Adapter] Platform '%s' not found in ETargetPlatform enum"), *PlatformName);
    return false;
}

FString FG01HotPatcherAdapter::ResolvePath(const FString& InPath)
{
    FString Result = InPath;
    FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
    Result.ReplaceInline(TEXT("[PROJECTDIR]"), *ProjectDir);

    bool bIsRelative = true;
    if (!Result.IsEmpty())
    {
        if (Result.Len() >= 2 && Result[1] == TEXT(':'))
        {
            bIsRelative = false;
        }
        else if (Result[0] == TEXT('/') || Result[0] == TEXT('\\'))
        {
            bIsRelative = false;
        }
    }

    if (bIsRelative)
    {
        Result = FPaths::Combine(ProjectDir, Result);
    }

    FPaths::NormalizeDirectoryName(Result);
    return Result;
}
