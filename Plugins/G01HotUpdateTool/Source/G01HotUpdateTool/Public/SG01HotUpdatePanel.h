#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "G01BuildReport.h"

/**
 * SG01HotUpdatePanel - G01 热更新出包面板
 *
 * 四个区块：版本状态、操作、确认摘要、构建结果
 * 三种动作：Export Release / Build Snapshot Patch / Promote to Release
 * 不暴露 HotPatcher 原始概念
 */
class SG01HotUpdatePanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SG01HotUpdatePanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    // ---- 数据 ----
    FG01BuildHistory History;

    // ---- 操作区 ----
    /** 0=ExportRelease, 1=BuildPatch, 2=PromoteToRelease */
    int32 ActionIndex = 0;
    FString InputReleaseVersion;         // ExportRelease 时手动输入
    FString SelectedBaseVersion;         // BuildPatch 时从下拉选
    FString InputTargetVersion;          // BuildPatch 时手动输入
    FString SelectedPromotePatchVersion; // Promote 时从下拉选

    // ---- 下拉选项 ----
    TArray<TSharedPtr<FString>> BaseVersionOptions;
    TArray<TSharedPtr<FString>> PromotablePatchOptions;

    // ---- 确认摘要 ----
    FString SummaryText;
    FString ValidationText;
    bool bValidationPassed = false;
    bool bShowSummary = false;
    bool bIsBuilding = false;

    // ---- 结果 ----
    FString ResultText;

    // ---- 表格数据 ----
    TArray<TSharedPtr<FG01BuildHistoryEntry>> ReleaseRows;
    TArray<TSharedPtr<FG01BuildHistoryEntry>> PatchRows;

    // ---- 方法 ----
    void RefreshHistory();
    FString GetOutputRoot() const;
    void GenerateSummary();
    void ExecuteBuild();
    void OnBuildComplete(int32 ReturnCode);

    TSharedRef<ITableRow> MakeReleaseRow(TSharedPtr<FG01BuildHistoryEntry> Item, const TSharedRef<STableViewBase>& Owner);
    TSharedRef<ITableRow> MakePatchRow(TSharedPtr<FG01BuildHistoryEntry> Item, const TSharedRef<STableViewBase>& Owner);

    // ---- 回调 ----
    FReply OnRefreshClicked();
    FReply OnGenerateClicked();
    FReply OnConfirmClicked();
    FReply OnCancelClicked();
    FReply OnOpenDirClicked();
};
