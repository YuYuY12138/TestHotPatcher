#include "SG01HotUpdatePanel.h"
#include "G01BuildTask.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SComboBox.h"
//#include "Widgets/Input/SMultiLineEditableText.h"
#include "Widgets/Text/STextBlock.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Framework/Application/SlateApplication.h"

#define LOCTEXT_NAMESPACE "G01HotUpdatePanel"

static FString FormatBuildTime(const FString& IsoUtc)
{
    if (IsoUtc.IsEmpty()) return TEXT("-");
    FDateTime Dt;
    if (!FDateTime::ParseIso8601(*IsoUtc, Dt)) return IsoUtc;
    FDateTime Local = Dt + (FDateTime::Now() - FDateTime::UtcNow());
    return FString::Printf(TEXT("%04d-%02d-%02d %02d:%02d"),
        Local.GetYear(), Local.GetMonth(), Local.GetDay(),
        Local.GetHour(), Local.GetMinute());
}

static TArray<TSharedPtr<FString>> GActions = {
    MakeShareable(new FString(TEXT("Export Release"))),
    MakeShareable(new FString(TEXT("Build Patch"))),
    MakeShareable(new FString(TEXT("Promote to Release"))),
};

static TArray<TSharedPtr<FString>> GPatchTypes = {
    MakeShareable(new FString(TEXT("Incremental"))),
    MakeShareable(new FString(TEXT("Consolidated"))),
};

void SG01HotUpdatePanel::Construct(const FArguments& InArgs)
{
    RefreshHistory();

    if (InputPakPath.IsEmpty())
    {
        InputPakPath = FPaths::Combine(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()),
            TEXT("Saved/StagedBuilds/Android_ASTC/TestHotpatch/Content/Paks/TestHotpatch-Android_ASTC.pak"));
    }

    ChildSlot
    [
        SNew(SScrollBox)

        // ===== 标题栏 =====
        + SScrollBox::Slot().Padding(8)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.f)
            [ SNew(STextBlock).Text(LOCTEXT("Title", "G01 HotUpdate Tool")).Font(FCoreStyle::GetDefaultFontStyle("Bold", 16)) ]
            + SHorizontalBox::Slot().AutoWidth()
            [ SNew(SButton).Text(LOCTEXT("Ref", "Refresh")).OnClicked(this, &SG01HotUpdatePanel::OnRefreshClicked) ]
        ]

        + SScrollBox::Slot().Padding(8, 2) [ SNew(SSeparator) ]

        // ===== 版本总览区 =====
        + SScrollBox::Slot().Padding(8)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(STextBlock).Text(LOCTEXT("OverviewH", "Version Overview")).Font(FCoreStyle::GetDefaultFontStyle("Bold", 12)) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
            [
                SNew(STextBlock)
                .Text_Lambda([this]() -> FText
                {
                    const FG01BasePackageInfo* BP = History.GetActiveBasePackage();
                    FString ActivePkg = TEXT("(none)");
                    FString LinkedRel = TEXT("-");
                    FString PakFile = TEXT("-");
                    FString Md5 = TEXT("-");
                    FString BpTime = TEXT("-");
                    if (BP)
                    {
                        ActivePkg = BP->PackageVersion;
                        LinkedRel = BP->LinkedReleaseVersion;
                        PakFile = BP->PackagePath.IsEmpty() ? TEXT("-") : FPaths::GetCleanFilename(BP->PackagePath);
                        Md5 = BP->PakMD5.IsEmpty() ? TEXT("-") : BP->PakMD5.Left(16) + TEXT("...");
                        BpTime = FormatBuildTime(BP->BuildTime);
                    }
                    else
                    {
                        for (const FG01BuildHistoryEntry& E : History.Entries)
                        {
                            if (E.PatchType == TEXT("Release") && !E.BasePackageVersion.IsEmpty())
                            {
                                ActivePkg = E.BasePackageVersion;
                                LinkedRel = E.TargetVersion;
                                break;
                            }
                        }
                    }
                    FString LatestRes = History.LatestPatchVersion.IsEmpty() ? History.LatestReleaseVersion : History.LatestPatchVersion;
                    return FText::FromString(FString::Printf(TEXT(
                        "Active Base Package:  %s\nPlatform:             Android\nPak File:             %s\nPak MD5:              %s\nBuild Time:           %s\nLinked Release:       %s\nLatest Resource Ver:  %s"),
                        *ActivePkg, *PakFile, *Md5, *BpTime, *LinkedRel, LatestRes.IsEmpty() ? TEXT("-") : *LatestRes));
                })
            ]
        ]

        + SScrollBox::Slot().Padding(8, 2) [ SNew(SSeparator) ]

        // ===== Release 表 =====
        + SScrollBox::Slot().Padding(8, 4, 8, 16)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(STextBlock).Text(LOCTEXT("RelH", "Releases")).Font(FCoreStyle::GetDefaultFontStyle("Bold", 12)) ]
            + SVerticalBox::Slot().AutoHeight().MaxHeight(120)
            [
                SAssignNew(ReleaseListView, SListView<TSharedPtr<FG01BuildHistoryEntry>>)
                .ListItemsSource(&ReleaseRows)
                .OnGenerateRow(this, &SG01HotUpdatePanel::MakeReleaseRow)
                .HeaderRow(
                    SNew(SHeaderRow)
                    + SHeaderRow::Column("BPV").DefaultLabel(LOCTEXT("BPV","BasePackage")).FillWidth(0.15f)
                    + SHeaderRow::Column("V").DefaultLabel(LOCTEXT("V","Version")).FillWidth(0.15f)
                    + SHeaderRow::Column("P").DefaultLabel(LOCTEXT("P","Platform")).FillWidth(0.1f)
                    + SHeaderRow::Column("T").DefaultLabel(LOCTEXT("T","Build Time")).FillWidth(0.35f)
                    + SHeaderRow::Column("R").DefaultLabel(LOCTEXT("R","Result")).FillWidth(0.1f)
                )
            ]
        ]

        // ===== Patch 表 =====
        + SScrollBox::Slot().Padding(8, 4, 8, 16)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(STextBlock).Text(LOCTEXT("PaH", "Patches")).Font(FCoreStyle::GetDefaultFontStyle("Bold", 12)) ]
            + SVerticalBox::Slot().AutoHeight().MaxHeight(200)
            [
                SAssignNew(PatchListView, SListView<TSharedPtr<FG01BuildHistoryEntry>>)
                .ListItemsSource(&PatchRows)
                .OnGenerateRow(this, &SG01HotUpdatePanel::MakePatchRow)
                .HeaderRow(
                    SNew(SHeaderRow)
                    + SHeaderRow::Column("BPV").DefaultLabel(LOCTEXT("BPVP","BasePkg")).FillWidth(0.1f)
                    + SHeaderRow::Column("B").DefaultLabel(LOCTEXT("B","Base")).FillWidth(0.08f)
                    + SHeaderRow::Column("TG").DefaultLabel(LOCTEXT("TG","Target")).FillWidth(0.08f)
                    + SHeaderRow::Column("TY").DefaultLabel(LOCTEXT("TY","Type")).FillWidth(0.09f)
                    + SHeaderRow::Column("SZ").DefaultLabel(LOCTEXT("SZ","Size")).FillWidth(0.1f)
                    + SHeaderRow::Column("M5").DefaultLabel(LOCTEXT("M5","MD5")).FillWidth(0.17f)
                    + SHeaderRow::Column("BT").DefaultLabel(LOCTEXT("BT","Time")).FillWidth(0.2f)
                    + SHeaderRow::Column("RS").DefaultLabel(LOCTEXT("RS","OK")).FillWidth(0.07f)
                )
            ]
        ]

        + SScrollBox::Slot().Padding(8, 2) [ SNew(SSeparator) ]

        // ===== 操作区 =====
        + SScrollBox::Slot().Padding(8)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(STextBlock).Text(LOCTEXT("AcH", "Build Action")).Font(FCoreStyle::GetDefaultFontStyle("Bold", 12)) ]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(0.3f) [ SNew(STextBlock).Text(LOCTEXT("BpvL", "Base Package Ver:")) ]
                + SHorizontalBox::Slot().FillWidth(0.7f)
                [
                    SNew(SEditableTextBox)
                    .Text_Lambda([this]() { return FText::FromString(InputBasePackageVersion); })
                    .HintText(LOCTEXT("BpvH", "e.g. 1.0.0"))
                    .OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type) { InputBasePackageVersion = T.ToString(); bShowSummary = false; })
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0)
            [
                SNew(STextBlock)
                .Visibility_Lambda([this]() {
                    const FG01BasePackageInfo* BP = History.GetActiveBasePackage();
                    if (BP && !BP->PackageVersion.IsEmpty() && !InputBasePackageVersion.IsEmpty() && BP->PackageVersion != InputBasePackageVersion)
                    {
                        return EVisibility::Visible;
                    }
                    return EVisibility::Collapsed;
                })
                .Text_Lambda([this]() {
                    const FG01BasePackageInfo* BP = History.GetActiveBasePackage();
                    if (BP)
                    {
                        return FText::FromString(FString::Printf(TEXT("Warning: Active BasePackage is %s, you entered %s"), *BP->PackageVersion, *InputBasePackageVersion));
                    }
                    return FText::GetEmpty();
                })
                .ColorAndOpacity(FLinearColor::Yellow)
                .AutoWrapText(true)
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(0.3f) [ SNew(STextBlock).Text(LOCTEXT("AcL", "Action:")) ]
                + SHorizontalBox::Slot().FillWidth(0.7f)
                [
                    SNew(SComboBox<TSharedPtr<FString>>)
                    .OptionsSource(&GActions)
                    .OnSelectionChanged_Lambda([this](TSharedPtr<FString> V, ESelectInfo::Type) {
                        ActionIndex = GActions.IndexOfByPredicate([&](const TSharedPtr<FString>& S) { return *S == *V; });
                        bShowSummary = false;
                    })
                    .OnGenerateWidget_Lambda([](TSharedPtr<FString> I) { return SNew(STextBlock).Text(FText::FromString(*I)); })
                    [ SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(*GActions[ActionIndex]); }) ]
                ]
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
            [
                SNew(SHorizontalBox)
                .Visibility_Lambda([this]() { return ActionIndex == 0 ? EVisibility::Visible : EVisibility::Collapsed; })
                + SHorizontalBox::Slot().FillWidth(0.3f) [ SNew(STextBlock).Text(LOCTEXT("RvL", "Release Version:")) ]
                + SHorizontalBox::Slot().FillWidth(0.7f)
                [
                    SNew(SEditableTextBox)
                    .Text_Lambda([this]() { return FText::FromString(InputReleaseVersion); })
                    .HintText(LOCTEXT("RvH", "e.g. 1.0.0"))
                    .OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type) { InputReleaseVersion = T.ToString(); bShowSummary = false; })
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
            [
                SNew(SHorizontalBox)
                .Visibility_Lambda([this]() { return ActionIndex == 0 ? EVisibility::Visible : EVisibility::Collapsed; })
                + SHorizontalBox::Slot().FillWidth(0.3f) [ SNew(STextBlock).Text(LOCTEXT("PkL", "Pak Path:")) ]
                + SHorizontalBox::Slot().FillWidth(0.7f)
                [
                    SNew(SEditableTextBox)
                    .Text_Lambda([this]() { return FText::FromString(InputPakPath); })
                    .HintText(LOCTEXT("PkH", "Path to base package .pak file"))
                    .OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type) { InputPakPath = T.ToString(); bShowSummary = false; })
                ]
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
            [
                SNew(SHorizontalBox)
                .Visibility_Lambda([this]() { return ActionIndex == 1 ? EVisibility::Visible : EVisibility::Collapsed; })
                + SHorizontalBox::Slot().FillWidth(0.3f) [ SNew(STextBlock).Text(LOCTEXT("PtL", "Patch Type:")) ]
                + SHorizontalBox::Slot().FillWidth(0.7f)
                [
                    SNew(SComboBox<TSharedPtr<FString>>)
                    .OptionsSource(&GPatchTypes)
                    .OnSelectionChanged_Lambda([this](TSharedPtr<FString> V, ESelectInfo::Type) {
                        if (V.IsValid())
                        {
                            PatchTypeIndex = GPatchTypes.IndexOfByPredicate([&](const TSharedPtr<FString>& S) { return *S == *V; });
                            bShowSummary = false;
                        }
                    })
                    .OnGenerateWidget_Lambda([](TSharedPtr<FString> I) { return SNew(STextBlock).Text(FText::FromString(*I)); })
                    [ SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(*GPatchTypes[PatchTypeIndex]); }) ]
                ]
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
            [
                SNew(SHorizontalBox)
                .Visibility_Lambda([this]() { return ActionIndex == 1 ? EVisibility::Visible : EVisibility::Collapsed; })
                + SHorizontalBox::Slot().FillWidth(0.3f) [ SNew(STextBlock).Text(LOCTEXT("BvL", "Base Version:")) ]
                + SHorizontalBox::Slot().FillWidth(0.7f)
                [
                    SNew(SComboBox<TSharedPtr<FString>>)
                    .OptionsSource(&BaseVersionOptions)
                    .OnSelectionChanged_Lambda([this](TSharedPtr<FString> V, ESelectInfo::Type) {
                        if (V.IsValid()) { SelectedBaseVersion = *V; bShowSummary = false; }
                    })
                    .OnGenerateWidget_Lambda([](TSharedPtr<FString> I) { return SNew(STextBlock).Text(FText::FromString(*I)); })
                    [ SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(SelectedBaseVersion.IsEmpty() ? TEXT("(select release)") : SelectedBaseVersion); }) ]
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
            [
                SNew(SHorizontalBox)
                .Visibility_Lambda([this]() { return ActionIndex == 1 ? EVisibility::Visible : EVisibility::Collapsed; })
                + SHorizontalBox::Slot().FillWidth(0.3f) [ SNew(STextBlock).Text(LOCTEXT("TvL", "Target Version:")) ]
                + SHorizontalBox::Slot().FillWidth(0.7f)
                [
                    SNew(SEditableTextBox)
                    .Text_Lambda([this]() { return FText::FromString(InputTargetVersion); })
                    .HintText(LOCTEXT("TvH", "e.g. 1.0.1"))
                    .OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type) { InputTargetVersion = T.ToString(); bShowSummary = false; })
                ]
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
            [
                SNew(SHorizontalBox)
                .Visibility_Lambda([this]() { return ActionIndex == 2 ? EVisibility::Visible : EVisibility::Collapsed; })
                + SHorizontalBox::Slot().FillWidth(0.3f) [ SNew(STextBlock).Text(LOCTEXT("PpL", "Promote Patch:")) ]
                + SHorizontalBox::Slot().FillWidth(0.7f)
                [
                    SNew(SComboBox<TSharedPtr<FString>>)
                    .OptionsSource(&PromotablePatchOptions)
                    .OnSelectionChanged_Lambda([this](TSharedPtr<FString> V, ESelectInfo::Type) {
                        if (!V.IsValid()) return;
                        SelectedPromotePatchVersion = *V;
                        for (const FG01BuildHistoryEntry& E : History.Entries)
                            if (E.TargetVersion == *V && E.PatchType != TEXT("Release") && !E.BasePackageVersion.IsEmpty())
                            { InputBasePackageVersion = E.BasePackageVersion; break; }
                        bShowSummary = false;
                    })
                    .OnGenerateWidget_Lambda([](TSharedPtr<FString> I) { return SNew(STextBlock).Text(FText::FromString(*I)); })
                    [ SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(SelectedPromotePatchVersion.IsEmpty() ? TEXT("(select patch)") : SelectedPromotePatchVersion); }) ]
                ]
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 8)
            [
                SNew(SButton).Text(LOCTEXT("Gen", "Generate"))
                .HAlign(HAlign_Center)
                .IsEnabled_Lambda([this]() { return !bIsBuilding; })
                .OnClicked(this, &SG01HotUpdatePanel::OnGenerateClicked)
            ]
        ]

        // ===== 确认摘要区 =====
        + SScrollBox::Slot().Padding(8)
        [
            SNew(SVerticalBox)
            .Visibility_Lambda([this]() { return bShowSummary ? EVisibility::Visible : EVisibility::Collapsed; })
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(STextBlock).Text(LOCTEXT("CfH", "Confirm Build")).Font(FCoreStyle::GetDefaultFontStyle("Bold", 12)) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
            [ SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(SummaryText); }).AutoWrapText(true) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
            [
                SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(ValidationText); })
                .ColorAndOpacity_Lambda([this]() { return bValidationPassed ? FSlateColor(FLinearColor::Green) : FSlateColor(FLinearColor::Red); })
                .AutoWrapText(true)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
                [ SNew(SButton).Text(LOCTEXT("Cfm", "Confirm & Build")).IsEnabled_Lambda([this]() { return bValidationPassed && !bIsBuilding; }).OnClicked(this, &SG01HotUpdatePanel::OnConfirmClicked) ]
                + SHorizontalBox::Slot().AutoWidth()
                [ SNew(SButton).Text(LOCTEXT("Ccl", "Cancel")).OnClicked(this, &SG01HotUpdatePanel::OnCancelClicked) ]
            ]
        ]

        + SScrollBox::Slot().Padding(8, 2) [ SNew(SSeparator) ]

        // ===== 构建结果 + 实时日志区 =====
        + SScrollBox::Slot().Padding(8)
        [
            SNew(SVerticalBox)

            // 结果标题 + 按钮行
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f)
                [ SNew(STextBlock).Text(LOCTEXT("RsH", "Build Log")).Font(FCoreStyle::GetDefaultFontStyle("Bold", 12)) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
                [ SNew(SButton).Text(LOCTEXT("ClrLog", "Clear")).OnClicked(this, &SG01HotUpdatePanel::OnClearLogClicked) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
                [ SNew(SButton).Text(LOCTEXT("OpenLog", "Open Log File")).OnClicked(this, &SG01HotUpdatePanel::OnOpenLogFileClicked) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
                [ SNew(SButton).Text(LOCTEXT("CpyErr", "Copy Errors")).OnClicked(this, &SG01HotUpdatePanel::OnCopyErrorClicked) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
                [ SNew(SButton).Text(LOCTEXT("OpD", "Open Output Dir")).OnClicked(this, &SG01HotUpdatePanel::OnOpenDirClicked) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
                [
                    SNew(SButton).Text(LOCTEXT("Abort", "Abort Build"))
                    .IsEnabled_Lambda([this]() { return bIsBuilding; })
                    .OnClicked(this, &SG01HotUpdatePanel::OnAbortClicked)
                ]
            ]

            // 结果摘要（成功/失败/错误提示）
            + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
            [
                SNew(STextBlock)
                .Text_Lambda([this]() { return FText::FromString(ResultSummary); })
                .Font_Lambda([this]() {
                    if (ResultSummary.Contains(TEXT("COMPLETE")))
                    {
                        return FCoreStyle::GetDefaultFontStyle("Bold", 28);
                    }
                    return FCoreStyle::GetDefaultFontStyle("Regular", 10);
                })
                .ColorAndOpacity_Lambda([this]() {
                    if (ResultSummary.Contains(TEXT("COMPLETE"))) return FSlateColor(FLinearColor::Green);
                    if (ResultSummary.Contains(TEXT("FAILED")) || ResultSummary.Contains(TEXT("ERROR"))) return FSlateColor(FLinearColor::Red);
                    return FSlateColor(FLinearColor::White);
                })
                .AutoWrapText(true)
            ]

            // 实时日志文本框（SBox 固定容器高度，STextBlock 内部自由增长，ScrollBox 才能真正滚动）
            + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
            [
                SNew(SBox).HeightOverride(350)
                [
                    SAssignNew(LogScrollBox, SScrollBox)
                    + SScrollBox::Slot()
                    [
                        SNew(SBorder)
                        .BorderBackgroundColor(FLinearColor(0.05f, 0.05f, 0.05f, 1.f))
                        .Padding(4)
                        [
                            SNew(STextBlock)
                            .Text_Lambda([this]() {
                                return FText::FromString(BuildLog.IsEmpty() ? TEXT("No build output yet.") : BuildLog);
                            })
                            .Font(FCoreStyle::GetDefaultFontStyle("Mono", 9))
                            .AutoWrapText(true)
                        ]
                    ]
                ]
            ]
        ]
    ];
}

// ===== Tick: 轮询进程 + 读取 Pipe =====

void SG01HotUpdatePanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
    SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

    if (!bIsBuilding || !BuildProcHandle.IsValid()) return;

    // 读取 stdout pipe 新增内容
    if (ReadPipe)
    {
        FString NewOutput = FPlatformProcess::ReadPipe(ReadPipe);
        if (!NewOutput.IsEmpty())
        {
            AppendLog(NewOutput);
        }
    }

    // 新内容追加后滚到底部（在 Layout 之后执行才准确）
    if (LogScrollBox.IsValid())
        LogScrollBox->ScrollToEnd();

    // 检查进程是否结束
    if (!FPlatformProcess::IsProcRunning(BuildProcHandle))
    {
        // 最后再读一次确保不遗漏
        if (ReadPipe)
        {
            FString Tail = FPlatformProcess::ReadPipe(ReadPipe);
            if (!Tail.IsEmpty()) AppendLog(Tail);

            FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
            ReadPipe = nullptr;
            WritePipe = nullptr;
        }

        int32 ReturnCode = 0;
        FPlatformProcess::GetProcReturnCode(BuildProcHandle, &ReturnCode);
        FPlatformProcess::CloseProc(BuildProcHandle);
        BuildProcHandle.Reset();

        OnBuildComplete(ReturnCode);
    }
}

void SG01HotUpdatePanel::AppendLog(const FString& NewText)
{
    BuildLog += NewText;

    // 检测真实构建错误（排除 DLL/PIX/profiler 等系统警告）
    // 只有明确的引擎/HotPatcher/Commandlet 错误才算失败
    if (!bBuildHasErrors)
    {
        // 排除已知无害警告
        bool bIsHarmlessWarning =
            NewText.Contains(TEXT("aqProf.dll")) ||
            NewText.Contains(TEXT("VtuneApi")) ||
            NewText.Contains(TEXT("WinPixGpu")) ||
            NewText.Contains(TEXT("PIX capture plugin")) ||
            NewText.Contains(TEXT("AssetDetail failed")) ||   // HotPatcher 扫描时资产找不到，是 warning 不是 error
            NewText.Contains(TEXT("Failed to load '"));        // DLL 加载失败

        if (!bIsHarmlessWarning)
        {
            if (NewText.Contains(TEXT("LogTemp: Error:")) ||
                NewText.Contains(TEXT("Fatal error:")) ||
                NewText.Contains(TEXT("BUILD FAILED")) ||
                NewText.Contains(TEXT("Commandlet->Main return this error")) ||
                NewText.Contains(TEXT("BASE RELEASE NOT FOUND")) ||
                NewText.Contains(TEXT("MISMATCH")))
            {
                bBuildHasErrors = true;
            }
        }
    }

}  // ScrollToEnd 放到 Tick 里处理

// ===== 数据加载 =====

void SG01HotUpdatePanel::RefreshHistory()
{
    History = FG01BuildHistory();
    History.LoadFromFile(FPaths::Combine(GetOutputRoot(), TEXT("BuildHistory.json")));

    ReleaseRows.Empty();
    PatchRows.Empty();
    BaseVersionOptions.Empty();
    PromotablePatchOptions.Empty();

    const FG01BasePackageInfo* ActiveBP = History.GetActiveBasePackage();
    if (ActiveBP && InputBasePackageVersion.IsEmpty())
        InputBasePackageVersion = ActiveBP->PackageVersion;

    for (const FG01BuildHistoryEntry& E : History.Entries)
    {
        if (E.PatchType == TEXT("Release"))
        {
            ReleaseRows.Add(MakeShareable(new FG01BuildHistoryEntry(E)));
            BaseVersionOptions.Add(MakeShareable(new FString(E.TargetVersion)));
        }
        else
        {
            PatchRows.Add(MakeShareable(new FG01BuildHistoryEntry(E)));
            if (E.bSuccess)
            {
                bool bPromoted = false;
                for (const FG01BuildHistoryEntry& R : History.Entries)
                    if (R.PatchType == TEXT("Release") && R.TargetVersion == E.TargetVersion) { bPromoted = true; break; }
                if (!bPromoted)
                    PromotablePatchOptions.Add(MakeShareable(new FString(E.TargetVersion)));
            }
        }
    }

    if (BaseVersionOptions.Num() > 0 && SelectedBaseVersion.IsEmpty())
        SelectedBaseVersion = *BaseVersionOptions.Last();

    FString Latest = History.LatestPatchVersion.IsEmpty() ? History.LatestReleaseVersion : History.LatestPatchVersion;
    if (!Latest.IsEmpty() && InputTargetVersion.IsEmpty())
    {
        int32 Dot;
        if (Latest.FindLastChar(TEXT('.'), Dot))
        {
            int32 Num = FCString::Atoi(*Latest.Mid(Dot + 1)) + 1;
            InputTargetVersion = Latest.Left(Dot + 1) + FString::FromInt(Num);
        }
    }

    if (ReleaseListView.IsValid())
    {
        ReleaseListView->RequestListRefresh();
    }
    if (PatchListView.IsValid())
    {
        PatchListView->RequestListRefresh();
    }
}

FString SG01HotUpdatePanel::GetOutputRoot() const
{
    return FPaths::Combine(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()), TEXT("Saved/HotPatcher"));
}

// ===== 确认摘要 =====

void SG01HotUpdatePanel::GenerateSummary()
{
    TArray<FString> Checks;
    bValidationPassed = true;

    if (InputBasePackageVersion.IsEmpty()) { Checks.Add(TEXT("[FAIL] Base Package Version empty")); bValidationPassed = false; }
    else
    {
        Checks.Add(FString::Printf(TEXT("[OK] BasePackage: %s"), *InputBasePackageVersion));
        const FG01BasePackageInfo* ActiveBP = History.GetActiveBasePackage();
        if (ActiveBP && !ActiveBP->PackageVersion.IsEmpty() && ActiveBP->PackageVersion != InputBasePackageVersion)
        {
            Checks.Add(FString::Printf(TEXT("[WARN] Active BasePackage is %s, you specified %s."), *ActiveBP->PackageVersion, *InputBasePackageVersion));
            Checks.Add(TEXT("       Cross-chain patch will be blocked if Release chain mismatch."));
        }
    }

    if (ActionIndex == 0)
    {
        SummaryText = FString::Printf(TEXT(
            "Action:          Export Release\nPlatform:        Android\nBase Package:    %s\nRelease Version: %s\nPak Path:        %s\n\n"
            "WARNING: Confirm workspace content corresponds to %s."),
            *InputBasePackageVersion, *InputReleaseVersion, *InputPakPath, *InputReleaseVersion);
        if (InputReleaseVersion.IsEmpty()) { Checks.Add(TEXT("[FAIL] Release Version empty")); bValidationPassed = false; }
        else
        {
            Checks.Add(FString::Printf(TEXT("[OK] Release: %s"), *InputReleaseVersion));
            FString RelPath = FPaths::Combine(GetOutputRoot(), TEXT("Releases"), InputReleaseVersion, InputReleaseVersion, InputReleaseVersion + TEXT("_Release.json"));
            if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*RelPath))
            { Checks.Add(TEXT("[FAIL] Release already exists")); bValidationPassed = false; }
        }
        if (InputPakPath.IsEmpty())
        {
            Checks.Add(TEXT("[FAIL] Pak Path empty"));
            bValidationPassed = false;
        }
        else if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*InputPakPath))
        {
            Checks.Add(FString::Printf(TEXT("[FAIL] Pak file not found: %s"), *FPaths::GetCleanFilename(InputPakPath)));
            bValidationPassed = false;
        }
        else
        {
            Checks.Add(FString::Printf(TEXT("[OK] Pak exists: %s"), *FPaths::GetCleanFilename(InputPakPath)));
        }
    }
    else if (ActionIndex == 1)
    {
        FString PtName = *GPatchTypes[PatchTypeIndex];
        SummaryText = FString::Printf(TEXT(
            "Action:        Build %s Patch\nPlatform:      Android\nBase Package:  %s\nBase Release:  %s\nTarget:        %s"),
            *PtName, *InputBasePackageVersion, *SelectedBaseVersion, *InputTargetVersion);

        if (PatchTypeIndex == 1)
        {
            TArray<FString> Contained;
            for (const FG01BuildHistoryEntry& E : History.Entries)
            {
                if (E.PatchType != TEXT("Release") && E.bSuccess && E.BasePackageVersion == InputBasePackageVersion)
                {
                    Contained.Add(E.TargetVersion);
                }
            }
            Contained.Add(InputTargetVersion);
            SummaryText += TEXT("\nContains:      ") + FString::Join(Contained, TEXT(", "));
        }

        SummaryText += FString::Printf(TEXT("\n\nWARNING: Confirm workspace includes all %s changes."), *InputTargetVersion);

        if (SelectedBaseVersion.IsEmpty()) { Checks.Add(TEXT("[FAIL] Base Version not selected")); bValidationPassed = false; }
        if (InputTargetVersion.IsEmpty()) { Checks.Add(TEXT("[FAIL] Target Version empty")); bValidationPassed = false; }
        else if (InputTargetVersion == SelectedBaseVersion) { Checks.Add(TEXT("[FAIL] Target must differ from Base")); bValidationPassed = false; }
        else
        {
            Checks.Add(FString::Printf(TEXT("[OK] %s -> %s (%s)"), *SelectedBaseVersion, *InputTargetVersion, *PtName));
            FString RelPath = FPaths::Combine(GetOutputRoot(), TEXT("Releases"), SelectedBaseVersion, SelectedBaseVersion, SelectedBaseVersion + TEXT("_Release.json"));
            if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*RelPath))
            {
                Checks.Add(TEXT("[OK] Base Release exists"));
            }
            else
            {
                Checks.Add(TEXT("[FAIL] Base Release not found"));
                bValidationPassed = false;
            }
        }
    }
    else if (ActionIndex == 2)
    {
        SummaryText = FString::Printf(TEXT(
            "Action:        Promote to Release\nPlatform:      Android\nBase Package:  %s\nPatch:         %s\n\n"
            "WARNING: Confirm workspace content corresponds to %s."),
            *InputBasePackageVersion, *SelectedPromotePatchVersion, *SelectedPromotePatchVersion);
        if (SelectedPromotePatchVersion.IsEmpty()) { Checks.Add(TEXT("[FAIL] No patch selected")); bValidationPassed = false; }
        else Checks.Add(FString::Printf(TEXT("[OK] Patch: %s"), *SelectedPromotePatchVersion));
    }

    Checks.Add(TEXT("[OK] IoStore disabled (Pak-only)"));
    ValidationText = FString::Join(Checks, TEXT("\n"));
}

// ===== 构建执行 =====

void SG01HotUpdatePanel::ExecuteBuild()
{
    // 清空日志
    BuildLog.Empty();
    bBuildHasErrors = false;
    ResultSummary = TEXT("Building...");
    bIsBuilding = true;

    FG01BuildTask Task;
    Task.Platform = TEXT("Android");
    Task.BasePackageVersion = InputBasePackageVersion;
    Task.ReleaseConfigTemplate = TEXT("ReleaseTest.json");
    Task.PatchConfigTemplate = TEXT("PatchTest.json");
    Task.OutputDir = TEXT("Saved/HotPatcher");

    if (ActionIndex == 0)
    {
        Task.TaskType = TEXT("ExportRelease");
        Task.BaseVersion = InputReleaseVersion;
        Task.PakPath = InputPakPath;
    }
    else if (ActionIndex == 1)
    {
        Task.TaskType = TEXT("BuildPatch");
        Task.BaseVersion = SelectedBaseVersion;
        Task.TargetVersion = InputTargetVersion;
        Task.PatchType = PatchTypeIndex == 0 ? TEXT("Incremental") : TEXT("Consolidated");
    }
    else if (ActionIndex == 2)
    {
        Task.TaskType = TEXT("PromoteToRelease");
        Task.TargetVersion = SelectedPromotePatchVersion;
        Task.PromoteFromPatchVersion = SelectedPromotePatchVersion;
    }

    FString TaskPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("HotPatcher/_CurrentBuildTask.json"));
    Task.SaveToFile(TaskPath);

    // 日志路径
    FString TimeStr = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
    CurrentLogPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("HotPatcher/Logs"),
        FString::Printf(TEXT("HotPatch_%s.log"), *TimeStr));

    FString UECmd = FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries/Win64/UnrealEditor-Cmd.exe"));
    FString Project = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());

    FString Params = FString::Printf(
        TEXT("\"%s\" -run=G01HotPatch -config=\"%s\" -NoLiveCoding -NoHotReload -unattended -nop4 -stdout -FullStdOutLogOutput -FORCELOGFLUSH -abslog=\"%s\" -UTF8Output"),
        *Project, *TaskPath, *CurrentLogPath);

    AppendLog(FString::Printf(TEXT("=== G01 HotPatch Build Started ===\n")));
    AppendLog(FString::Printf(TEXT("Task: %s\nLog: %s\n\n"), *Task.TaskType, *CurrentLogPath));

    // 创建 stdout pipe
    FPlatformProcess::CreatePipe(ReadPipe, WritePipe);

    BuildProcHandle = FPlatformProcess::CreateProc(
        *UECmd, *Params,
        false,  // bLaunchDetached - false 以便 pipe 正常工作
        true,   // bLaunchHidden
        true,   // bLaunchReallyHidden
        nullptr, 0, nullptr,
        WritePipe,   // 子进程 stdout 写入端
        nullptr
    );

    if (!BuildProcHandle.IsValid())
    {
        FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
        ReadPipe = nullptr;
        WritePipe = nullptr;
        ResultSummary = TEXT("ERROR: Failed to launch Commandlet process");
        bIsBuilding = false;
    }
}

void SG01HotUpdatePanel::OnBuildComplete(int32 ReturnCode)
{
    bIsBuilding = false;
    bShowSummary = false;
    RefreshHistory();

    // exit code 0 = 成功（以 Commandlet 返回码为准）
    // bBuildHasErrors 仅在有明确错误关键词时为 true，作为补充参考
    bool bSuccess = (ReturnCode == 0);

    if (bSuccess)
    {
        ResultSummary = TEXT("BUILD COMPLETE");
        if (History.Entries.Num() > 0)
        {
            const auto& L = History.Entries.Last();
            ResultSummary += FString::Printf(TEXT("\nVersion: %s  BasePackage: %s  Type: %s"),
                *L.TargetVersion, *L.BasePackageVersion, *L.PatchType);
            if (L.TotalPakSize > 0)
            {
                ResultSummary += FString::Printf(TEXT("  Size: %.1f MB"), L.TotalPakSize / (1024.0 * 1024.0));
            }
        }
    }
    else if (bBuildHasErrors)
    {
        ResultSummary = FString::Printf(TEXT("BUILD FAILED (exit code: %d)\n存在错误，请查看日志"), ReturnCode);
    }
    else
    {
        ResultSummary = FString::Printf(TEXT("BUILD FAILED (exit code: %d)"), ReturnCode);
    }

    AppendLog(FString::Printf(TEXT("\n=== Build %s (exit: %d) ===\n"),
        bSuccess ? TEXT("COMPLETE") : TEXT("FAILED"), ReturnCode));
}

// ===== 表格行 =====

TSharedRef<ITableRow> SG01HotUpdatePanel::MakeReleaseRow(TSharedPtr<FG01BuildHistoryEntry> Item, const TSharedRef<STableViewBase>& Owner)
{
    return SNew(STableRow<TSharedPtr<FG01BuildHistoryEntry>>, Owner)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(0.15f).Padding(4, 2) [ SNew(STextBlock).Text(FText::FromString(Item->BasePackageVersion)) ]
        + SHorizontalBox::Slot().FillWidth(0.15f).Padding(4, 2) [ SNew(STextBlock).Text(FText::FromString(Item->TargetVersion)) ]
        + SHorizontalBox::Slot().FillWidth(0.1f).Padding(4, 2) [ SNew(STextBlock).Text(FText::FromString(Item->Platform)) ]
        + SHorizontalBox::Slot().FillWidth(0.35f).Padding(4, 2) [ SNew(STextBlock).Text(FText::FromString(FormatBuildTime(Item->BuildTime))) ]
        + SHorizontalBox::Slot().FillWidth(0.1f).Padding(4, 2)
        [ SNew(STextBlock).Text(FText::FromString(Item->bSuccess ? TEXT("OK") : TEXT("FAIL")))
            .ColorAndOpacity(Item->bSuccess ? FLinearColor::Green : FLinearColor::Red) ]
    ];
}

TSharedRef<ITableRow> SG01HotUpdatePanel::MakePatchRow(TSharedPtr<FG01BuildHistoryEntry> Item, const TSharedRef<STableViewBase>& Owner)
{
    FString Sz = Item->TotalPakSize > 0 ? FString::Printf(TEXT("%.1fMB"), Item->TotalPakSize / (1024.0 * 1024.0)) : TEXT("-");
    FString M5 = Item->PakMD5.IsEmpty() ? TEXT("-") : Item->PakMD5.Left(12) + TEXT("..");

    return SNew(STableRow<TSharedPtr<FG01BuildHistoryEntry>>, Owner)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(0.1f).Padding(4, 2) [ SNew(STextBlock).Text(FText::FromString(Item->BasePackageVersion)) ]
        + SHorizontalBox::Slot().FillWidth(0.08f).Padding(4, 2) [ SNew(STextBlock).Text(FText::FromString(Item->BaseVersion)) ]
        + SHorizontalBox::Slot().FillWidth(0.08f).Padding(4, 2) [ SNew(STextBlock).Text(FText::FromString(Item->TargetVersion)) ]
        + SHorizontalBox::Slot().FillWidth(0.09f).Padding(4, 2) [ SNew(STextBlock).Text(FText::FromString(Item->PatchType)) ]
        + SHorizontalBox::Slot().FillWidth(0.1f).Padding(4, 2) [ SNew(STextBlock).Text(FText::FromString(Sz)) ]
        + SHorizontalBox::Slot().FillWidth(0.17f).Padding(4, 2) [ SNew(STextBlock).Text(FText::FromString(M5)) ]
        + SHorizontalBox::Slot().FillWidth(0.2f).Padding(4, 2) [ SNew(STextBlock).Text(FText::FromString(FormatBuildTime(Item->BuildTime))) ]
        + SHorizontalBox::Slot().FillWidth(0.07f).Padding(4, 2)
        [ SNew(STextBlock).Text(FText::FromString(Item->bSuccess ? TEXT("OK") : TEXT("FAIL")))
            .ColorAndOpacity(Item->bSuccess ? FLinearColor::Green : FLinearColor::Red) ]
    ];
}

// ===== 回调 =====

FReply SG01HotUpdatePanel::OnRefreshClicked() { RefreshHistory(); return FReply::Handled(); }
FReply SG01HotUpdatePanel::OnGenerateClicked() { GenerateSummary(); bShowSummary = true; return FReply::Handled(); }
FReply SG01HotUpdatePanel::OnConfirmClicked() { ExecuteBuild(); return FReply::Handled(); }
FReply SG01HotUpdatePanel::OnCancelClicked() { bShowSummary = false; return FReply::Handled(); }

FReply SG01HotUpdatePanel::OnClearLogClicked()
{
    BuildLog.Empty();
    ResultSummary.Empty();
    bBuildHasErrors = false;
    return FReply::Handled();
}

FReply SG01HotUpdatePanel::OnOpenLogFileClicked()
{
    if (!CurrentLogPath.IsEmpty() && FPlatformFileManager::Get().GetPlatformFile().FileExists(*CurrentLogPath))
        FPlatformProcess::LaunchFileInDefaultExternalApplication(*CurrentLogPath);
    else
        FPlatformProcess::ExploreFolder(*FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("HotPatcher/Logs")));
    return FReply::Handled();
}

FReply SG01HotUpdatePanel::OnCopyErrorClicked()
{
    // 提取包含 Error/Fatal 的行
    TArray<FString> Lines;
    BuildLog.ParseIntoArrayLines(Lines);
    FString Errors;
    for (const FString& L : Lines)
        if (L.Contains(TEXT("Error:")) || L.Contains(TEXT("Fatal:")) || L.Contains(TEXT("FAILED")))
            Errors += L + TEXT("\n");
    if (!Errors.IsEmpty())
        FPlatformApplicationMisc::ClipboardCopy(*Errors);
    return FReply::Handled();
}

FReply SG01HotUpdatePanel::OnOpenDirClicked()
{
    FPlatformProcess::ExploreFolder(*GetOutputRoot());
    return FReply::Handled();
}

FReply SG01HotUpdatePanel::OnAbortClicked()
{
    if (BuildProcHandle.IsValid())
    {
        FPlatformProcess::TerminateProc(BuildProcHandle, true);
        AppendLog(TEXT("\n=== Build ABORTED by user ===\n"));
    }
    return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
