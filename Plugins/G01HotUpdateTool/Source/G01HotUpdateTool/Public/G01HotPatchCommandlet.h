#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "G01BuildTask.h"
#include "G01HotPatchCommandlet.generated.h"

/**
 * UG01HotPatchCommandlet - G01 热更新统一构建入口
 *
 * 三种动作：
 *   ExportRelease      - 归档当前工作区为指定版本 Release
 *   BuildPatch         - 基于已有 Release 生成差异补丁（不自动生成新 Release）
 *   PromoteToRelease   - 将已成功 Patch 的 targetVersion 归档为 Release（需用户确认工作区状态）
 *
 * 调用：UE-Cmd.exe <Project> -run=G01HotPatch -config=<BuildTask.json>
 */
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
        const FString& BuildTimeStr, const FString& OutputRoot, double StartTime);

    int32 ExecuteBuildPatch(const FG01BuildTask& Task, const FString& ProjectDir,
        const FString& ReleasesDir, const FString& ReleaseJsonPath,
        const FString& BuildTimeStr, const FString& OutputRoot, double StartTime);

    int32 ExecutePromoteToRelease(const FG01BuildTask& Task, const FString& ProjectDir,
        const FString& BuildTimeStr, const FString& OutputRoot, double StartTime);

    static bool ComputeFileMD5(const FString& FilePath, FString& OutMD5);
    static FString GetCurrentTimeISO8601();
};
