#include "G01VersionManifest.h"
#include "G01BuildReport.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"

static TArray<TSharedPtr<FJsonValue>> ToJsonStrArr(const TArray<FString>& A)
{
    TArray<TSharedPtr<FJsonValue>> R;
    for (const FString& S : A) R.Add(MakeShareable(new FJsonValueString(S)));
    return R;
}

static TArray<FString> FromJsonStrArr(const TSharedPtr<FJsonObject>& O, const FString& K)
{
    TArray<FString> R;
    const TArray<TSharedPtr<FJsonValue>>* A;
    if (O->TryGetArrayField(K, A))
        for (const auto& V : *A) R.Add(V->AsString());
    return R;
}

// ========== VersionManifest ==========

bool FG01VersionManifest::SaveToFile(const FString& FilePath) const
{
    TSharedRef<FJsonObject> Root = MakeShareable(new FJsonObject());
    Root->SetStringField(TEXT("version"), Version);
    Root->SetStringField(TEXT("basePackageVersion"), BasePackageVersion);
    Root->SetStringField(TEXT("baseVersion"), BaseVersion);
    Root->SetStringField(TEXT("patchType"), PatchType);
    Root->SetStringField(TEXT("platform"), Platform);
    Root->SetStringField(TEXT("buildTime"), BuildTime);
    Root->SetStringField(TEXT("releaseNote"), ReleaseNote);
    Root->SetNumberField(TEXT("mountOrder"), MountOrder);
    Root->SetArrayField(TEXT("containsVersions"), ToJsonStrArr(ContainsVersions));
    Root->SetArrayField(TEXT("replacesPatches"), ToJsonStrArr(ReplacesPatches));

    TArray<TSharedPtr<FJsonValue>> Arr;
    for (const FG01ManifestFileInfo& F : Files)
    {
        TSharedRef<FJsonObject> FO = MakeShareable(new FJsonObject());
        FO->SetStringField(TEXT("name"), F.Name);
        FO->SetStringField(TEXT("url"), F.Url);
        FO->SetNumberField(TEXT("size"), F.Size);
        FO->SetStringField(TEXT("md5"), F.MD5);
        Arr.Add(MakeShareable(new FJsonValueObject(FO)));
    }
    Root->SetArrayField(TEXT("files"), Arr);

    FString Out;
    auto W = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Out);
    FJsonSerializer::Serialize(Root, W);
    return FFileHelper::SaveStringToFile(Out, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

// ========== BuildReport ==========

bool FG01BuildReport::SaveToFile(const FString& FilePath) const
{
    TSharedRef<FJsonObject> Root = MakeShareable(new FJsonObject());
    Root->SetStringField(TEXT("taskType"), TaskType);
    Root->SetStringField(TEXT("platform"), Platform);
    Root->SetStringField(TEXT("patchType"), PatchType);
    Root->SetStringField(TEXT("basePackageVersion"), BasePackageVersion);
    Root->SetStringField(TEXT("baseVersion"), BaseVersion);
    Root->SetStringField(TEXT("targetVersion"), TargetVersion);
    Root->SetStringField(TEXT("buildTime"), BuildTime);
    Root->SetNumberField(TEXT("duration"), Duration);
    Root->SetBoolField(TEXT("success"), bSuccess);

    TArray<TSharedPtr<FJsonValue>> OutArr;
    for (const FG01BuildReportOutput& O : Outputs)
    {
        TSharedRef<FJsonObject> OO = MakeShareable(new FJsonObject());
        OO->SetStringField(TEXT("type"), O.Type);
        OO->SetStringField(TEXT("path"), O.Path);
        OO->SetNumberField(TEXT("size"), O.Size);
        OO->SetStringField(TEXT("md5"), O.MD5);
        OutArr.Add(MakeShareable(new FJsonValueObject(OO)));
    }
    Root->SetArrayField(TEXT("outputs"), OutArr);
    Root->SetArrayField(TEXT("errors"), ToJsonStrArr(Errors));

    FString Out;
    auto W = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Out);
    FJsonSerializer::Serialize(Root, W);
    return FFileHelper::SaveStringToFile(Out, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool FG01BuildReport::LoadFromFile(const FString& FilePath)
{
    FString Json;
    if (!FFileHelper::LoadFileToString(Json, *FilePath)) return false;
    TSharedPtr<FJsonObject> Root;
    auto Reader = TJsonReaderFactory<>::Create(Json);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return false;

    TaskType = Root->GetStringField(TEXT("taskType"));
    Platform = Root->GetStringField(TEXT("platform"));
    if (Root->HasField(TEXT("patchType"))) PatchType = Root->GetStringField(TEXT("patchType"));
    if (Root->HasField(TEXT("basePackageVersion"))) BasePackageVersion = Root->GetStringField(TEXT("basePackageVersion"));
    if (Root->HasField(TEXT("baseVersion"))) BaseVersion = Root->GetStringField(TEXT("baseVersion"));
    TargetVersion = Root->GetStringField(TEXT("targetVersion"));
    BuildTime = Root->GetStringField(TEXT("buildTime"));
    Duration = Root->GetNumberField(TEXT("duration"));
    bSuccess = Root->GetBoolField(TEXT("success"));

    Outputs.Empty();
    const TArray<TSharedPtr<FJsonValue>>* OArr;
    if (Root->TryGetArrayField(TEXT("outputs"), OArr))
        for (const auto& V : *OArr)
        {
            auto OO = V->AsObject();
            FG01BuildReportOutput O;
            O.Type = OO->GetStringField(TEXT("type"));
            O.Path = OO->GetStringField(TEXT("path"));
            O.Size = static_cast<int64>(OO->GetNumberField(TEXT("size")));
            O.MD5 = OO->GetStringField(TEXT("md5"));
            Outputs.Add(O);
        }
    Errors = FromJsonStrArr(Root, TEXT("errors"));
    return true;
}

// ========== BuildHistory ==========

bool FG01BuildHistory::LoadFromFile(const FString& FilePath)
{
    FString Json;
    if (!FFileHelper::LoadFileToString(Json, *FilePath)) return false;
    TSharedPtr<FJsonObject> Root;
    auto Reader = TJsonReaderFactory<>::Create(Json);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return false;

    if (Root->HasField(TEXT("latestReleaseVersion"))) LatestReleaseVersion = Root->GetStringField(TEXT("latestReleaseVersion"));
    if (Root->HasField(TEXT("latestPatchVersion"))) LatestPatchVersion = Root->GetStringField(TEXT("latestPatchVersion"));
    if (Root->HasField(TEXT("activeBasePackageVersion"))) ActiveBasePackageVersion = Root->GetStringField(TEXT("activeBasePackageVersion"));

    // BasePackages
    BasePackages.Empty();
    const TArray<TSharedPtr<FJsonValue>>* BPArr;
    if (Root->TryGetArrayField(TEXT("basePackages"), BPArr))
        for (const auto& V : *BPArr)
        {
            auto O = V->AsObject();
            if (!O.IsValid()) continue;
            FG01BasePackageInfo BP;
            BP.PackageVersion = O->GetStringField(TEXT("packageVersion"));
            BP.Platform = O->GetStringField(TEXT("platform"));
            if (O->HasField(TEXT("linkedReleaseVersion"))) BP.LinkedReleaseVersion = O->GetStringField(TEXT("linkedReleaseVersion"));
            if (O->HasField(TEXT("packagePath"))) BP.PackagePath = O->GetStringField(TEXT("packagePath"));
            if (O->HasField(TEXT("buildTime"))) BP.BuildTime = O->GetStringField(TEXT("buildTime"));
            if (O->HasField(TEXT("gitCommit"))) BP.GitCommit = O->GetStringField(TEXT("gitCommit"));
            if (O->HasField(TEXT("isActiveBase"))) BP.bIsActiveBase = O->GetBoolField(TEXT("isActiveBase"));
            BasePackages.Add(BP);
        }

    // Entries
    Entries.Empty();
    const TArray<TSharedPtr<FJsonValue>>* EArr;
    if (Root->TryGetArrayField(TEXT("entries"), EArr))
        for (const auto& V : *EArr)
        {
            auto O = V->AsObject();
            if (!O.IsValid()) continue;
            FG01BuildHistoryEntry E;
            E.TargetVersion = O->GetStringField(TEXT("targetVersion"));
            E.BaseVersion = O->GetStringField(TEXT("baseVersion"));
            E.PatchType = O->GetStringField(TEXT("patchType"));
            E.Platform = O->GetStringField(TEXT("platform"));
            if (O->HasField(TEXT("basePackageVersion"))) E.BasePackageVersion = O->GetStringField(TEXT("basePackageVersion"));
            E.BuildTime = O->GetStringField(TEXT("buildTime"));
            E.bSuccess = O->GetBoolField(TEXT("success"));
            E.ReportPath = O->GetStringField(TEXT("reportPath"));
            E.ContainsVersions = FromJsonStrArr(O, TEXT("containsVersions"));
            if (O->HasField(TEXT("totalPakSize"))) E.TotalPakSize = static_cast<int64>(O->GetNumberField(TEXT("totalPakSize")));
            if (O->HasField(TEXT("pakMD5"))) E.PakMD5 = O->GetStringField(TEXT("pakMD5"));
            if (O->HasField(TEXT("bObsolete"))) E.bObsolete = O->GetBoolField(TEXT("bObsolete"));
            if (O->HasField(TEXT("promotedFromPatch"))) E.PromotedFromPatch = O->GetStringField(TEXT("promotedFromPatch"));
            if (O->HasField(TEXT("candidateReleasePath"))) E.CandidateReleasePath = O->GetStringField(TEXT("candidateReleasePath"));
            Entries.Add(E);
        }
    return true;
}

bool FG01BuildHistory::SaveToFile(const FString& FilePath) const
{
    TSharedRef<FJsonObject> Root = MakeShareable(new FJsonObject());
    Root->SetStringField(TEXT("latestReleaseVersion"), LatestReleaseVersion);
    Root->SetStringField(TEXT("latestPatchVersion"), LatestPatchVersion);
    Root->SetStringField(TEXT("activeBasePackageVersion"), ActiveBasePackageVersion);

    TArray<TSharedPtr<FJsonValue>> BPArr;
    for (const FG01BasePackageInfo& BP : BasePackages)
    {
        TSharedRef<FJsonObject> O = MakeShareable(new FJsonObject());
        O->SetStringField(TEXT("packageVersion"), BP.PackageVersion);
        O->SetStringField(TEXT("platform"), BP.Platform);
        O->SetStringField(TEXT("linkedReleaseVersion"), BP.LinkedReleaseVersion);
        O->SetStringField(TEXT("packagePath"), BP.PackagePath);
        O->SetStringField(TEXT("buildTime"), BP.BuildTime);
        O->SetStringField(TEXT("gitCommit"), BP.GitCommit);
        O->SetBoolField(TEXT("isActiveBase"), BP.bIsActiveBase);
        BPArr.Add(MakeShareable(new FJsonValueObject(O)));
    }
    Root->SetArrayField(TEXT("basePackages"), BPArr);

    TArray<TSharedPtr<FJsonValue>> EArr;
    for (const FG01BuildHistoryEntry& E : Entries)
    {
        TSharedRef<FJsonObject> O = MakeShareable(new FJsonObject());
        O->SetStringField(TEXT("targetVersion"), E.TargetVersion);
        O->SetStringField(TEXT("baseVersion"), E.BaseVersion);
        O->SetStringField(TEXT("patchType"), E.PatchType);
        O->SetStringField(TEXT("platform"), E.Platform);
        O->SetStringField(TEXT("basePackageVersion"), E.BasePackageVersion);
        O->SetStringField(TEXT("buildTime"), E.BuildTime);
        O->SetBoolField(TEXT("success"), E.bSuccess);
        O->SetStringField(TEXT("reportPath"), E.ReportPath);
        O->SetArrayField(TEXT("containsVersions"), ToJsonStrArr(E.ContainsVersions));
        O->SetNumberField(TEXT("totalPakSize"), E.TotalPakSize);
        O->SetStringField(TEXT("pakMD5"), E.PakMD5);
        O->SetBoolField(TEXT("bObsolete"), E.bObsolete);
        O->SetStringField(TEXT("promotedFromPatch"), E.PromotedFromPatch);
        O->SetStringField(TEXT("candidateReleasePath"), E.CandidateReleasePath);
        EArr.Add(MakeShareable(new FJsonValueObject(O)));
    }
    Root->SetArrayField(TEXT("entries"), EArr);

    FString Out;
    auto W = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Out);
    FJsonSerializer::Serialize(Root, W);
    return FFileHelper::SaveStringToFile(Out, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void FG01BuildHistory::AddEntry(const FG01BuildHistoryEntry& Entry)
{
    Entries.Add(Entry);
    if (Entry.PatchType == TEXT("Release"))
        LatestReleaseVersion = Entry.TargetVersion;
    else
        LatestPatchVersion = Entry.TargetVersion;
}

void FG01BuildHistory::RegisterBasePackage(const FG01BasePackageInfo& Info)
{
    // 将现有包的 isActiveBase 全部置 false
    for (FG01BasePackageInfo& BP : BasePackages)
        BP.bIsActiveBase = false;

    // 如果已有同版本记录则更新，否则追加
    bool bFound = false;
    for (FG01BasePackageInfo& BP : BasePackages)
    {
        if (BP.PackageVersion == Info.PackageVersion && BP.Platform == Info.Platform)
        {
            BP = Info;
            BP.bIsActiveBase = true;
            bFound = true;
            break;
        }
    }
    if (!bFound)
    {
        FG01BasePackageInfo NewBP = Info;
        NewBP.bIsActiveBase = true;
        BasePackages.Add(NewBP);
    }

    ActiveBasePackageVersion = Info.PackageVersion;
}

TArray<const FG01BuildHistoryEntry*> FG01BuildHistory::GetReleases() const
{
    TArray<const FG01BuildHistoryEntry*> R;
    for (const FG01BuildHistoryEntry& E : Entries)
        if (E.PatchType == TEXT("Release")) R.Add(&E);
    return R;
}

TArray<const FG01BuildHistoryEntry*> FG01BuildHistory::GetPatches() const
{
    TArray<const FG01BuildHistoryEntry*> R;
    for (const FG01BuildHistoryEntry& E : Entries)
        if (E.PatchType != TEXT("Release")) R.Add(&E);
    return R;
}

const FG01BasePackageInfo* FG01BuildHistory::GetActiveBasePackage() const
{
    for (const FG01BasePackageInfo& BP : BasePackages)
        if (BP.bIsActiveBase) return &BP;
    return nullptr;
}
