#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "G01BuildTask.h"
#include "G01HotPatchCommandlet.generated.h"

UCLASS()
class UG01HotPatchCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    UG01HotPatchCommandlet();
    virtual int32 Main(const FString& Params) override;

private:
    int32 ExecuteExportRelease(const FG01BuildTask& Task, const FString& ProjectDir,
        const FString& ReleasesDir, const FString& ReleaseJsonPath,
        const FString& BuildTimeStr, const FString& OutputRoot,
        const FString& HistPath, double StartTime);

    int32 ExecuteBuildPatch(const FG01BuildTask& Task, const FString& ProjectDir,
        const FString& ReleasesDir, const FString& ReleaseJsonPath,
        const FString& BuildTimeStr, const FString& OutputRoot,
        const FString& HistPath, double StartTime);

    int32 ExecutePromoteToRelease(const FG01BuildTask& Task, const FString& ProjectDir,
        const FString& BuildTimeStr, const FString& OutputRoot,
        const FString& HistPath, double StartTime);

    static bool ComputeFileMD5(const FString& FilePath, FString& OutMD5);
    static FString GetCurrentTimeISO8601();
};
