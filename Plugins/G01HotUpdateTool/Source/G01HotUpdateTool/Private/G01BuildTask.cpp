#include "G01BuildTask.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"

bool FG01BuildTask::LoadFromFile(const FString& FilePath)
{
    FString Json;
    if (!FFileHelper::LoadFileToString(Json, *FilePath)) return false;

    TSharedPtr<FJsonObject> O;
    auto R = TJsonReaderFactory<>::Create(Json);
    if (!FJsonSerializer::Deserialize(R, O) || !O.IsValid()) return false;

    TaskType = O->GetStringField(TEXT("taskType"));
    Platform = O->GetStringField(TEXT("platform"));

    if (O->HasField(TEXT("basePackageVersion"))) BasePackageVersion = O->GetStringField(TEXT("basePackageVersion"));
    if (O->HasField(TEXT("baseVersion"))) BaseVersion = O->GetStringField(TEXT("baseVersion"));
    if (O->HasField(TEXT("targetVersion"))) TargetVersion = O->GetStringField(TEXT("targetVersion"));
    if (O->HasField(TEXT("patchType"))) PatchType = O->GetStringField(TEXT("patchType"));
    if (O->HasField(TEXT("promoteFromPatchVersion"))) PromoteFromPatchVersion = O->GetStringField(TEXT("promoteFromPatchVersion"));
    if (O->HasField(TEXT("releaseConfigTemplate"))) ReleaseConfigTemplate = O->GetStringField(TEXT("releaseConfigTemplate"));
    if (O->HasField(TEXT("patchConfigTemplate"))) PatchConfigTemplate = O->GetStringField(TEXT("patchConfigTemplate"));
    if (O->HasField(TEXT("outputDir"))) OutputDir = O->GetStringField(TEXT("outputDir"));

    if (O->HasField(TEXT("options")))
    {
        auto Opt = O->GetObjectField(TEXT("options"));
        if (Opt.IsValid())
        {
            if (Opt->HasField(TEXT("bCookPatchAssets"))) Options.bCookPatchAssets = Opt->GetBoolField(TEXT("bCookPatchAssets"));
            if (Opt->HasField(TEXT("bCompressPak"))) Options.bCompressPak = Opt->GetBoolField(TEXT("bCompressPak"));
            if (Opt->HasField(TEXT("bStandaloneMode"))) Options.bStandaloneMode = Opt->GetBoolField(TEXT("bStandaloneMode"));
            if (Opt->HasField(TEXT("bCalculateMD5"))) Options.bCalculateMD5 = Opt->GetBoolField(TEXT("bCalculateMD5"));
            if (Opt->HasField(TEXT("bGenerateManifest"))) Options.bGenerateManifest = Opt->GetBoolField(TEXT("bGenerateManifest"));
            if (Opt->HasField(TEXT("bGenerateBuildReport"))) Options.bGenerateBuildReport = Opt->GetBoolField(TEXT("bGenerateBuildReport"));
        }
    }
    return true;
}

bool FG01BuildTask::SaveToFile(const FString& FilePath) const
{
    TSharedRef<FJsonObject> O = MakeShareable(new FJsonObject());
    O->SetStringField(TEXT("taskType"), TaskType);
    O->SetStringField(TEXT("platform"), Platform);
    O->SetStringField(TEXT("basePackageVersion"), BasePackageVersion);
    O->SetStringField(TEXT("baseVersion"), BaseVersion);
    O->SetStringField(TEXT("targetVersion"), TargetVersion);
    O->SetStringField(TEXT("patchType"), PatchType);
    O->SetStringField(TEXT("promoteFromPatchVersion"), PromoteFromPatchVersion);
    O->SetStringField(TEXT("releaseConfigTemplate"), ReleaseConfigTemplate);
    O->SetStringField(TEXT("patchConfigTemplate"), PatchConfigTemplate);
    O->SetStringField(TEXT("outputDir"), OutputDir);

    TSharedRef<FJsonObject> Opt = MakeShareable(new FJsonObject());
    Opt->SetBoolField(TEXT("bCookPatchAssets"), Options.bCookPatchAssets);
    Opt->SetBoolField(TEXT("bCompressPak"), Options.bCompressPak);
    Opt->SetBoolField(TEXT("bStandaloneMode"), Options.bStandaloneMode);
    Opt->SetBoolField(TEXT("bCalculateMD5"), Options.bCalculateMD5);
    Opt->SetBoolField(TEXT("bGenerateManifest"), Options.bGenerateManifest);
    Opt->SetBoolField(TEXT("bGenerateBuildReport"), Options.bGenerateBuildReport);
    O->SetObjectField(TEXT("options"), Opt);

    FString Out;
    auto W = TJsonWriterFactory<>::Create(&Out);
    FJsonSerializer::Serialize(O, W);
    return FFileHelper::SaveStringToFile(Out, *FilePath);
}

TArray<FString> FG01BuildTask::Validate() const
{
    TArray<FString> Errors;

    if (TaskType != TEXT("ExportRelease") && TaskType != TEXT("BuildPatch") && TaskType != TEXT("PromoteToRelease"))
        Errors.Add(FString::Printf(TEXT("Invalid taskType: '%s'"), *TaskType));

    if (Platform != TEXT("Android"))
        Errors.Add(FString::Printf(TEXT("Unsupported platform: '%s'"), *Platform));

    // basePackageVersion 三种动作都需要
    if (BasePackageVersion.IsEmpty())
        Errors.Add(TEXT("basePackageVersion is required (the Android package version this hotfix chain belongs to)"));

    if (TaskType == TEXT("ExportRelease"))
    {
        if (BaseVersion.IsEmpty()) Errors.Add(TEXT("baseVersion required for ExportRelease"));
        if (ReleaseConfigTemplate.IsEmpty()) Errors.Add(TEXT("releaseConfigTemplate required"));
    }

    if (TaskType == TEXT("BuildPatch"))
    {
        if (BaseVersion.IsEmpty()) Errors.Add(TEXT("baseVersion required"));
        if (TargetVersion.IsEmpty()) Errors.Add(TEXT("targetVersion required"));
        if (PatchConfigTemplate.IsEmpty()) Errors.Add(TEXT("patchConfigTemplate required"));
        if (ReleaseConfigTemplate.IsEmpty()) Errors.Add(TEXT("releaseConfigTemplate required"));
        if (PatchType != TEXT("Snapshot") && PatchType != TEXT("Incremental") && PatchType != TEXT("Merged"))
            Errors.Add(FString::Printf(TEXT("Invalid patchType: '%s'"), *PatchType));
        if (PatchType == TEXT("Incremental")) Errors.Add(TEXT("Incremental not supported in MVP"));
        if (BaseVersion == TargetVersion) Errors.Add(TEXT("baseVersion and targetVersion must differ"));
    }

    if (TaskType == TEXT("PromoteToRelease"))
    {
        if (TargetVersion.IsEmpty()) Errors.Add(TEXT("targetVersion required for Promote"));
        if (PromoteFromPatchVersion.IsEmpty()) Errors.Add(TEXT("promoteFromPatchVersion required"));
        if (ReleaseConfigTemplate.IsEmpty()) Errors.Add(TEXT("releaseConfigTemplate required"));
    }

    return Errors;
}
