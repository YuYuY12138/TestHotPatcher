#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "G01BuildReport.h"
#include "HAL/PlatformProcess.h"

class SScrollBox;

class SG01HotUpdatePanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SG01HotUpdatePanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
    FG01BuildHistory History;

    // ---- 操作区 ----
    int32 ActionIndex = 0;
    FString InputBasePackageVersion;
    FString InputReleaseVersion;
    FString InputPakPath;
    FString SelectedBaseVersion;
    FString InputTargetVersion;
    FString SelectedPromotePatchVersion;

    // ---- 下拉选项 ----
    TArray<TSharedPtr<FString>> BaseVersionOptions;
    TArray<TSharedPtr<FString>> PromotablePatchOptions;

    // ---- 确认摘要 ----
    FString SummaryText;
    FString ValidationText;
    bool bValidationPassed = false;
    bool bShowSummary = false;
    bool bIsBuilding = false;

    // ---- 异步进程 + Pipe ----
    FProcHandle BuildProcHandle;
    void* ReadPipe = nullptr;
    void* WritePipe = nullptr;

    // ---- 实时日志 ----
    FString BuildLog;
    FString CurrentLogPath;
    bool bBuildHasErrors = false;
    TSharedPtr<SScrollBox> LogScrollBox;

    // ---- 结果摘要 ----
    FString ResultSummary;

    // ---- 表格数据 ----
    TArray<TSharedPtr<FG01BuildHistoryEntry>> ReleaseRows;
    TArray<TSharedPtr<FG01BuildHistoryEntry>> PatchRows;
    TSharedPtr<SListView<TSharedPtr<FG01BuildHistoryEntry>>> ReleaseListView;
    TSharedPtr<SListView<TSharedPtr<FG01BuildHistoryEntry>>> PatchListView;

    void RefreshHistory();
    FString GetOutputRoot() const;
    void GenerateSummary();
    void ExecuteBuild();
    void OnBuildComplete(int32 ReturnCode);
    void AppendLog(const FString& NewText);

    TSharedRef<ITableRow> MakeReleaseRow(TSharedPtr<FG01BuildHistoryEntry> Item, const TSharedRef<STableViewBase>& Owner);
    TSharedRef<ITableRow> MakePatchRow(TSharedPtr<FG01BuildHistoryEntry> Item, const TSharedRef<STableViewBase>& Owner);

    FReply OnRefreshClicked();
    FReply OnGenerateClicked();
    FReply OnConfirmClicked();
    FReply OnCancelClicked();
    FReply OnOpenDirClicked();
    FReply OnClearLogClicked();
    FReply OnOpenLogFileClicked();
    FReply OnCopyErrorClicked();
    FReply OnAbortClicked();
};
