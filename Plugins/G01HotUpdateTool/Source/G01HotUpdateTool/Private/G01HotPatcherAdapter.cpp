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

bool FG01HotPatcherAdapter::ExportRelease(
    const FString& Version,
    const FString& Platform,
    const FString& TemplatePath,
    const FString& OutputDir,
    FString& OutError)
{
    // 1. 加载模板 JSON
    FString FullTemplatePath = ResolvePath(TemplatePath);
    FString JsonContent;
    if (!FFileHelper::LoadFileToString(JsonContent, *FullTemplatePath))
    {
        OutError = FString::Printf(TEXT("Failed to load release template: %s"), *FullTemplatePath);
        return false;
    }

    // 2. 反序列化为 Settings 结构体
    FExportReleaseSettings Settings;
    if (!THotPatcherTemplateHelper::TDeserializeJsonStringAsStruct(JsonContent, Settings))
    {
        OutError = FString::Printf(TEXT("Failed to parse release template JSON: %s"), *FullTemplatePath);
        return false;
    }

    // 3. 覆盖关键字段
    Settings.VersionId = Version;
    Settings.SavePath.Path = OutputDir;
    Settings.bStandaloneMode = false;  // Commandlet 已是独立进程，不再套子进程

    UE_LOG(LogTemp, Log, TEXT("[Adapter] ExportRelease: Version=%s, Platform=%s, Output=%s"),
        *Version, *Platform, *OutputDir);

    // 4. 创建并执行 Proxy
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
    // 1. 加载模板 JSON
    FString FullTemplatePath = ResolvePath(TemplatePath);
    FString JsonContent;
    if (!FFileHelper::LoadFileToString(JsonContent, *FullTemplatePath))
    {
        OutError = FString::Printf(TEXT("Failed to load patch template: %s"), *FullTemplatePath);
        return false;
    }

    // 2. 反序列化为 Settings 结构体
    FExportPatchSettings Settings;
    if (!THotPatcherTemplateHelper::TDeserializeJsonStringAsStruct(JsonContent, Settings))
    {
        OutError = FString::Printf(TEXT("Failed to parse patch template JSON: %s"), *FullTemplatePath);
        return false;
    }

    // 3. 覆盖关键字段
    Settings.VersionId = TargetVersion;
    Settings.bByBaseVersion = true;
    Settings.BaseVersion.FilePath = ResolvePath(BaseReleaseJson);
    Settings.SavePath.Path = OutputDir;
    Settings.bStandaloneMode = false;
    Settings.bCookPatchAssets = bCookAssets;
    Settings.bStorageNewRelease = true;  // 同步生成 CandidateRelease，供 PromoteToRelease 使用

    // 4. 设置目标平台
    int32 PlatformEnumValue;
    if (!GetTargetPlatformEnum(Platform, PlatformEnumValue))
    {
        OutError = FString::Printf(TEXT("Unsupported platform: %s"), *Platform);
        return false;
    }
    Settings.PakTargetPlatforms.Empty();
    Settings.PakTargetPlatforms.Add(static_cast<ETargetPlatform>(PlatformEnumValue));

    UE_LOG(LogTemp, Log, TEXT("[Adapter] BuildPatch: Base=%s, Target=%s, Platform=%s, Output=%s"),
        *BaseVersion, *TargetVersion, *Platform, *OutputDir);

    // 5. 创建并执行 Proxy
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

    // 6. 扫描产物目录中的 .pak 文件
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

    UE_LOG(LogTemp, Log, TEXT("[Adapter] BuildPatch completed: %d pak file(s) generated"), OutPakPaths.Num());
    return true;
}

bool FG01HotPatcherAdapter::GetTargetPlatformEnum(const FString& PlatformName, int32& OutEnumValue)
{
    // HotPatcher 使用动态扩展枚举，平台名在运行时通过 AppendEnumeraters 添加
    // 通过 StaticEnum 按字符串查找对应的枚举值
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

    // 替换 [PROJECTDIR] 为实际项目路径
    FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
    Result.ReplaceInline(TEXT("[PROJECTDIR]"), *ProjectDir);

    // 判断是否为相对路径（不以盘符或/开头）
    bool bIsRelative = true;
    if (!Result.IsEmpty())
    {
        if (Result.Len() >= 2 && Result[1] == TEXT(':')) bIsRelative = false;
        else if (Result[0] == TEXT('/') || Result[0] == TEXT('\\')) bIsRelative = false;
    }

    if (bIsRelative)
    {
        Result = FPaths::Combine(ProjectDir, Result);
    }

    FPaths::NormalizeDirectoryName(Result);
    return Result;
}
