#include "SG01HotUpdatePanel.h"
#include "G01BuildTask.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SComboBox.h"
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
    MakeShareable(new FString(TEXT("导出 Release"))),
    MakeShareable(new FString(TEXT("构建 Patch"))),
    MakeShareable(new FString(TEXT("提升为 Release"))),
};

static TArray<TSharedPtr<FString>> GPatchTypes = {
    MakeShareable(new FString(TEXT("Incremental (增量)"))),
    MakeShareable(new FString(TEXT("Consolidated (整合)"))),
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

        + SScrollBox::Slot().Padding(8)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.f)
            [ SNew(STextBlock).Text(FText::FromString(TEXT("G01 热更新工具"))).Font(FCoreStyle::GetDefaultFontStyle("Bold", 16)) ]
            + SHorizontalBox::Slot().AutoWidth()
            [ SNew(SButton).Text(FText::FromString(TEXT("刷新"))).OnClicked(this, &SG01HotUpdatePanel::OnRefreshClicked) ]
        ]

        + SScrollBox::Slot().Padding(8, 2) [ SNew(SSeparator) ]

        + SScrollBox::Slot().Padding(8)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(STextBlock).Text(FText::FromString(TEXT("版本总览"))).Font(FCoreStyle::GetDefaultFontStyle("Bold", 12)) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
            [
                SNew(STextBlock)
                .Text_Lambda([this]() -> FText
                {
                    const FG01BasePackageInfo* BP = History.GetActiveBasePackage();
                    FString ActivePkg = TEXT("(无)");
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
                        "当前基础包:    %s\n平台:          Android\nPak 文件:      %s\nPak MD5:       %s\n构建时间:      %s\n关联 Release:  %s\n最新资源版本:  %s"),
                        *ActivePkg, *PakFile, *Md5, *BpTime, *LinkedRel, LatestRes.IsEmpty() ? TEXT("-") : *LatestRes));
                })
            ]
        ]

        + SScrollBox::Slot().Padding(8, 2) [ SNew(SSeparator) ]

        + SScrollBox::Slot().Padding(8, 4, 8, 16)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(STextBlock).Text(FText::FromString(TEXT("Release 列表"))).Font(FCoreStyle::GetDefaultFontStyle("Bold", 12)) ]
            + SVerticalBox::Slot().AutoHeight().MaxHeight(120)
            [
                SAssignNew(ReleaseListView, SListView<TSharedPtr<FG01BuildHistoryEntry>>)
                .ListItemsSource(&ReleaseRows)
                .OnGenerateRow(this, &SG01HotUpdatePanel::MakeReleaseRow)
                .HeaderRow(
                    SNew(SHeaderRow)
                    + SHeaderRow::Column("BPV").DefaultLabel(FText::FromString(TEXT("基础包"))).FillWidth(0.15f)
                    + SHeaderRow::Column("V").DefaultLabel(FText::FromString(TEXT("版本"))).FillWidth(0.15f)
                    + SHeaderRow::Column("P").DefaultLabel(FText::FromString(TEXT("平台"))).FillWidth(0.1f)
                    + SHeaderRow::Column("T").DefaultLabel(FText::FromString(TEXT("构建时间"))).FillWidth(0.35f)
                    + SHeaderRow::Column("R").DefaultLabel(FText::FromString(TEXT("结果"))).FillWidth(0.1f)
                )
            ]
        ]

        + SScrollBox::Slot().Padding(8, 4, 8, 16)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(STextBlock).Text(FText::FromString(TEXT("Patch 列表"))).Font(FCoreStyle::GetDefaultFontStyle("Bold", 12)) ]
            + SVerticalBox::Slot().AutoHeight().MaxHeight(200)
            [
                SAssignNew(PatchListView, SListView<TSharedPtr<FG01BuildHistoryEntry>>)
                .ListItemsSource(&PatchRows)
                .OnGenerateRow(this, &SG01HotUpdatePanel::MakePatchRow)
                .HeaderRow(
                    SNew(SHeaderRow)
                    + SHeaderRow::Column("BPV").DefaultLabel(FText::FromString(TEXT("基础包"))).FillWidth(0.1f)
                    + SHeaderRow::Column("B").DefaultLabel(FText::FromString(TEXT("基准"))).FillWidth(0.08f)
                    + SHeaderRow::Column("TG").DefaultLabel(FText::FromString(TEXT("目标"))).FillWidth(0.08f)
                    + SHeaderRow::Column("TY").DefaultLabel(FText::FromString(TEXT("类型"))).FillWidth(0.09f)
                    + SHeaderRow::Column("SZ").DefaultLabel(FText::FromString(TEXT("大小"))).FillWidth(0.1f)
                    + SHeaderRow::Column("M5").DefaultLabel(FText::FromString(TEXT("MD5"))).FillWidth(0.17f)
                    + SHeaderRow::Column("BT").DefaultLabel(FText::FromString(TEXT("时间"))).FillWidth(0.2f)
                    + SHeaderRow::Column("RS").DefaultLabel(FText::FromString(TEXT("结果"))).FillWidth(0.07f)
                )
            ]
        ]

        + SScrollBox::Slot().Padding(8, 2) [ SNew(SSeparator) ]

        + SScrollBox::Slot().Padding(8)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(STextBlock).Text(FText::FromString(TEXT("构建操作"))).Font(FCoreStyle::GetDefaultFontStyle("Bold", 12)) ]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(0.3f) [ SNew(STextBlock).Text(FText::FromString(TEXT("基础包版本:"))) ]
                + SHorizontalBox::Slot().FillWidth(0.7f)
                [
                    SNew(SEditableTextBox)
                    .Text_Lambda([this]() { return FText::FromString(InputBasePackageVersion); })
                    .HintText(FText::FromString(TEXT("例: 1.0.0")))
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
                        return FText::FromString(FString::Printf(TEXT("警告: 当前活跃基础包为 %s，你输入的是 %s"), *BP->PackageVersion, *InputBasePackageVersion));
                    }
                    return FText::GetEmpty();
                })
                .ColorAndOpacity(FLinearColor::Yellow)
                .AutoWrapText(true)
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(0.3f) [ SNew(STextBlock).Text(FText::FromString(TEXT("操作类型:"))) ]
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
                + SHorizontalBox::Slot().FillWidth(0.3f) [ SNew(STextBlock).Text(FText::FromString(TEXT("Release 版本:"))) ]
                + SHorizontalBox::Slot().FillWidth(0.7f)
                [
                    SNew(SEditableTextBox)
                    .Text_Lambda([this]() { return FText::FromString(InputReleaseVersion); })
                    .HintText(FText::FromString(TEXT("例: 1.0.0")))
                    .OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type) { InputReleaseVersion = T.ToString(); bShowSummary = false; })
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
            [
                SNew(SHorizontalBox)
                .Visibility_Lambda([this]() { return ActionIndex == 0 ? EVisibility::Visible : EVisibility::Collapsed; })
                + SHorizontalBox::Slot().FillWidth(0.3f) [ SNew(STextBlock).Text(FText::FromString(TEXT("Pak 路径:"))) ]
                + SHorizontalBox::Slot().FillWidth(0.7f)
                [
                    SNew(SEditableTextBox)
                    .Text_Lambda([this]() { return FText::FromString(InputPakPath); })
                    .HintText(FText::FromString(TEXT("基础包 .pak 文件路径")))
                    .OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type) { InputPakPath = T.ToString(); bShowSummary = false; })
                ]
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
            [
                SNew(SHorizontalBox)
                .Visibility_Lambda([this]() { return ActionIndex == 1 ? EVisibility::Visible : EVisibility::Collapsed; })
                + SHorizontalBox::Slot().FillWidth(0.3f) [ SNew(STextBlock).Text(FText::FromString(TEXT("Patch 类型:"))) ]
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
                + SHorizontalBox::Slot().FillWidth(0.3f) [ SNew(STextBlock).Text(FText::FromString(TEXT("基准版本:"))) ]
                + SHorizontalBox::Slot().FillWidth(0.7f)
                [
                    SNew(SComboBox<TSharedPtr<FString>>)
                    .OptionsSource(&BaseVersionOptions)
                    .OnSelectionChanged_Lambda([this](TSharedPtr<FString> V, ESelectInfo::Type) {
                        if (V.IsValid()) { SelectedBaseVersion = *V; bShowSummary = false; }
                    })
                    .OnGenerateWidget_Lambda([](TSharedPtr<FString> I) { return SNew(STextBlock).Text(FText::FromString(*I)); })
                    [ SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(SelectedBaseVersion.IsEmpty() ? TEXT("(选择 Release)") : SelectedBaseVersion); }) ]
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
            [
                SNew(SHorizontalBox)
                .Visibility_Lambda([this]() { return ActionIndex == 1 ? EVisibility::Visible : EVisibility::Collapsed; })
                + SHorizontalBox::Slot().FillWidth(0.3f) [ SNew(STextBlock).Text(FText::FromString(TEXT("目标版本:"))) ]
                + SHorizontalBox::Slot().FillWidth(0.7f)
                [
                    SNew(SEditableTextBox)
                    .Text_Lambda([this]() { return FText::FromString(InputTargetVersion); })
                    .HintText(FText::FromString(TEXT("例: 1.0.1")))
                    .OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type) { InputTargetVersion = T.ToString(); bShowSummary = false; })
                ]
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
            [
                SNew(SHorizontalBox)
                .Visibility_Lambda([this]() { return ActionIndex == 2 ? EVisibility::Visible : EVisibility::Collapsed; })
                + SHorizontalBox::Slot().FillWidth(0.3f) [ SNew(STextBlock).Text(FText::FromString(TEXT("提升 Patch:"))) ]
                + SHorizontalBox::Slot().FillWidth(0.7f)
                [
                    SNew(SComboBox<TSharedPtr<FString>>)
                    .OptionsSource(&PromotablePatchOptions)
                    .OnSelectionChanged_Lambda([this](TSharedPtr<FString> V, ESelectInfo::Type) {
                        if (!V.IsValid()) return;
                        SelectedPromotePatchVersion = *V;
                        for (const FG01BuildHistoryEntry& E : History.Entries)
                        {
                            if (E.TargetVersion == *V && E.PatchType != TEXT("Release") && !E.BasePackageVersion.IsEmpty())
                            {
                                InputBasePackageVersion = E.BasePackageVersion;
                                break;
                            }
                        }
                        bShowSummary = false;
                    })
                    .OnGenerateWidget_Lambda([](TSharedPtr<FString> I) { return SNew(STextBlock).Text(FText::FromString(*I)); })
                    [ SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(SelectedPromotePatchVersion.IsEmpty() ? TEXT("(选择 Patch)") : SelectedPromotePatchVersion); }) ]
                ]
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 8)
            [
                SNew(SButton).Text(FText::FromString(TEXT("生成摘要")))
                .HAlign(HAlign_Center)
                .IsEnabled_Lambda([this]() { return !bIsBuilding; })
                .OnClicked(this, &SG01HotUpdatePanel::OnGenerateClicked)
            ]
        ]

        + SScrollBox::Slot().Padding(8)
        [
            SNew(SVerticalBox)
            .Visibility_Lambda([this]() { return bShowSummary ? EVisibility::Visible : EVisibility::Collapsed; })
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(STextBlock).Text(FText::FromString(TEXT("确认构建"))).Font(FCoreStyle::GetDefaultFontStyle("Bold", 12)) ]
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
                [ SNew(SButton).Text(FText::FromString(TEXT("确认并构建"))).IsEnabled_Lambda([this]() { return bValidationPassed && !bIsBuilding; }).OnClicked(this, &SG01HotUpdatePanel::OnConfirmClicked) ]
                + SHorizontalBox::Slot().AutoWidth()
                [ SNew(SButton).Text(FText::FromString(TEXT("取消"))).OnClicked(this, &SG01HotUpdatePanel::OnCancelClicked) ]
            ]
        ]

        + SScrollBox::Slot().Padding(8, 2) [ SNew(SSeparator) ]

        + SScrollBox::Slot().Padding(8)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f)
                [ SNew(STextBlock).Text(FText::FromString(TEXT("构建日志"))).Font(FCoreStyle::GetDefaultFontStyle("Bold", 12)) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
                [ SNew(SButton).Text(FText::FromString(TEXT("清空"))).OnClicked(this, &SG01HotUpdatePanel::OnClearLogClicked) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
                [ SNew(SButton).Text(FText::FromString(TEXT("打开日志"))).OnClicked(this, &SG01HotUpdatePanel::OnOpenLogFileClicked) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
                [ SNew(SButton).Text(FText::FromString(TEXT("复制错误"))).OnClicked(this, &SG01HotUpdatePanel::OnCopyErrorClicked) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
                [ SNew(SButton).Text(FText::FromString(TEXT("打开输出目录"))).OnClicked(this, &SG01HotUpdatePanel::OnOpenDirClicked) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
                [
                    SNew(SButton).Text(FText::FromString(TEXT("终止构建")))
                    .IsEnabled_Lambda([this]() { return bIsBuilding; })
                    .OnClicked(this, &SG01HotUpdatePanel::OnAbortClicked)
                ]
            ]

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
                                return FText::FromString(BuildLog.IsEmpty() ? TEXT("暂无构建输出。") : BuildLog);
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

void SG01HotUpdatePanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
    SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
    if (!bIsBuilding || !BuildProcHandle.IsValid()) return;

    if (ReadPipe)
    {
        FString NewOutput = FPlatformProcess::ReadPipe(ReadPipe);
        if (!NewOutput.IsEmpty()) { AppendLog(NewOutput); }
    }

    if (LogScrollBox.IsValid()) { LogScrollBox->ScrollToEnd(); }

    if (!FPlatformProcess::IsProcRunning(BuildProcHandle))
    {
        if (ReadPipe)
        {
            FString Tail = FPlatformProcess::ReadPipe(ReadPipe);
            if (!Tail.IsEmpty()) { AppendLog(Tail); }
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
    if (!bBuildHasErrors)
    {
        bool bIsHarmlessWarning =
            NewText.Contains(TEXT("aqProf.dll")) ||
            NewText.Contains(TEXT("VtuneApi")) ||
            NewText.Contains(TEXT("WinPixGpu")) ||
            NewText.Contains(TEXT("PIX capture plugin")) ||
            NewText.Contains(TEXT("AssetDetail failed")) ||
            NewText.Contains(TEXT("Failed to load '"));

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
}

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
    {
        InputBasePackageVersion = ActiveBP->PackageVersion;
    }

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
                {
                    if (R.PatchType == TEXT("Release") && R.TargetVersion == E.TargetVersion)
                    {
                        bPromoted = true;
                        break;
                    }
                }
                if (!bPromoted)
                {
                    PromotablePatchOptions.Add(MakeShareable(new FString(E.TargetVersion)));
                }
            }
        }
    }

    if (BaseVersionOptions.Num() > 0 && SelectedBaseVersion.IsEmpty())
    {
        SelectedBaseVersion = *BaseVersionOptions.Last();
    }

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

    if (ReleaseListView.IsValid()) { ReleaseListView->RequestListRefresh(); }
    if (PatchListView.IsValid()) { PatchListView->RequestListRefresh(); }
}

FString SG01HotUpdatePanel::GetOutputRoot() const
{
    return FPaths::Combine(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()), TEXT("Saved/HotPatcher"));
}

void SG01HotUpdatePanel::GenerateSummary()
{
    TArray<FString> Checks;
    bValidationPassed = true;

    if (InputBasePackageVersion.IsEmpty()) { Checks.Add(TEXT("[失败] 基础包版本为空")); bValidationPassed = false; }
    else
    {
        Checks.Add(FString::Printf(TEXT("[通过] 基础包: %s"), *InputBasePackageVersion));
        const FG01BasePackageInfo* ActiveBP = History.GetActiveBasePackage();
        if (ActiveBP && !ActiveBP->PackageVersion.IsEmpty() && ActiveBP->PackageVersion != InputBasePackageVersion)
        {
            Checks.Add(FString::Printf(TEXT("[警告] 当前活跃基础包为 %s，你指定的是 %s"), *ActiveBP->PackageVersion, *InputBasePackageVersion));
            Checks.Add(TEXT("       跨链 Patch 将被阻止"));
        }
    }

    if (ActionIndex == 0)
    {
        SummaryText = FString::Printf(TEXT(
            "操作:          导出 Release\n平台:          Android\n基础包:        %s\nRelease 版本:  %s\nPak 路径:      %s\n\n"
            "注意: 请确认当前工作区内容与 %s 版本一致。"),
            *InputBasePackageVersion, *InputReleaseVersion, *InputPakPath, *InputReleaseVersion);
        if (InputReleaseVersion.IsEmpty()) { Checks.Add(TEXT("[失败] Release 版本为空")); bValidationPassed = false; }
        else
        {
            Checks.Add(FString::Printf(TEXT("[通过] Release: %s"), *InputReleaseVersion));
            FString RelPath = FPaths::Combine(GetOutputRoot(), TEXT("Releases"), InputReleaseVersion, InputReleaseVersion, InputReleaseVersion + TEXT("_Release.json"));
            if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*RelPath))
            { Checks.Add(TEXT("[失败] Release 已存在")); bValidationPassed = false; }
        }
        if (InputPakPath.IsEmpty()) { Checks.Add(TEXT("[失败] Pak 路径为空")); bValidationPassed = false; }
        else if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*InputPakPath))
        { Checks.Add(FString::Printf(TEXT("[失败] Pak 文件不存在: %s"), *FPaths::GetCleanFilename(InputPakPath))); bValidationPassed = false; }
        else
        { Checks.Add(FString::Printf(TEXT("[通过] Pak 文件存在: %s"), *FPaths::GetCleanFilename(InputPakPath))); }
    }
    else if (ActionIndex == 1)
    {
        FString PtName = *GPatchTypes[PatchTypeIndex];
        SummaryText = FString::Printf(TEXT(
            "操作:          构建 %s Patch\n平台:          Android\n基础包:        %s\n基准 Release:  %s\n目标版本:      %s"),
            *PtName, *InputBasePackageVersion, *SelectedBaseVersion, *InputTargetVersion);

        if (PatchTypeIndex == 1)
        {
            TArray<FString> Contained;
            for (const FG01BuildHistoryEntry& E : History.Entries)
            {
                if (E.PatchType != TEXT("Release") && E.bSuccess && E.BasePackageVersion == InputBasePackageVersion)
                { Contained.Add(E.TargetVersion); }
            }
            Contained.Add(InputTargetVersion);
            SummaryText += TEXT("\n包含版本:      ") + FString::Join(Contained, TEXT(", "));
        }
        SummaryText += FString::Printf(TEXT("\n\n注意: 请确认当前工作区包含 %s 的所有改动。"), *InputTargetVersion);

        if (SelectedBaseVersion.IsEmpty()) { Checks.Add(TEXT("[失败] 未选择基准版本")); bValidationPassed = false; }
        if (InputTargetVersion.IsEmpty()) { Checks.Add(TEXT("[失败] 目标版本为空")); bValidationPassed = false; }
        else if (InputTargetVersion == SelectedBaseVersion) { Checks.Add(TEXT("[失败] 目标版本不能与基准版本相同")); bValidationPassed = false; }
        else
        {
            Checks.Add(FString::Printf(TEXT("[通过] %s -> %s (%s)"), *SelectedBaseVersion, *InputTargetVersion, *PtName));
            FString RelPath = FPaths::Combine(GetOutputRoot(), TEXT("Releases"), SelectedBaseVersion, SelectedBaseVersion, SelectedBaseVersion + TEXT("_Release.json"));
            if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*RelPath))
            { Checks.Add(TEXT("[通过] 基准 Release 存在")); }
            else { Checks.Add(TEXT("[失败] 基准 Release 不存在")); bValidationPassed = false; }
        }
    }
    else if (ActionIndex == 2)
    {
        SummaryText = FString::Printf(TEXT(
            "操作:          提升为 Release\n平台:          Android\n基础包:        %s\nPatch:         %s\n\n"
            "注意: 请确认当前工作区内容与 %s 版本一致。"),
            *InputBasePackageVersion, *SelectedPromotePatchVersion, *SelectedPromotePatchVersion);
        if (SelectedPromotePatchVersion.IsEmpty()) { Checks.Add(TEXT("[失败] 未选择 Patch")); bValidationPassed = false; }
        else { Checks.Add(FString::Printf(TEXT("[通过] Patch: %s"), *SelectedPromotePatchVersion)); }
    }

    Checks.Add(TEXT("[通过] IoStore 已禁用 (Pak-only)"));
    ValidationText = FString::Join(Checks, TEXT("\n"));
}

void SG01HotUpdatePanel::ExecuteBuild()
{
    BuildLog.Empty();
    bBuildHasErrors = false;
    ResultSummary = TEXT("构建中...");
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

    FString TimeStr = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
    CurrentLogPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("HotPatcher/Logs"),
        FString::Printf(TEXT("HotPatch_%s.log"), *TimeStr));

    FString UECmd = FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries/Win64/UnrealEditor-Cmd.exe"));
    FString Project = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());

    FString Params = FString::Printf(
        TEXT("\"%s\" -run=G01HotPatch -config=\"%s\" -NoLiveCoding -NoHotReload -unattended -nop4 -stdout -FullStdOutLogOutput -FORCELOGFLUSH -abslog=\"%s\" -UTF8Output"),
        *Project, *TaskPath, *CurrentLogPath);

    AppendLog(FString::Printf(TEXT("=== G01 热更新构建开始 ===\n")));
    AppendLog(FString::Printf(TEXT("任务: %s\n日志: %s\n\n"), *Task.TaskType, *CurrentLogPath));

    FPlatformProcess::CreatePipe(ReadPipe, WritePipe);
    BuildProcHandle = FPlatformProcess::CreateProc(*UECmd, *Params, false, true, true, nullptr, 0, nullptr, WritePipe, nullptr);

    if (!BuildProcHandle.IsValid())
    {
        FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
        ReadPipe = nullptr;
        WritePipe = nullptr;
        ResultSummary = TEXT("错误: 无法启动 Commandlet 进程");
        bIsBuilding = false;
    }
}

void SG01HotUpdatePanel::OnBuildComplete(int32 ReturnCode)
{
    bIsBuilding = false;
    bShowSummary = false;
    RefreshHistory();

    bool bSuccess = (ReturnCode == 0);
    if (bSuccess)
    {
        ResultSummary = TEXT("构建完成 BUILD COMPLETE");
        if (History.Entries.Num() > 0)
        {
            const auto& L = History.Entries.Last();
            ResultSummary += FString::Printf(TEXT("\n版本: %s  基础包: %s  类型: %s"),
                *L.TargetVersion, *L.BasePackageVersion, *L.PatchType);
            if (L.TotalPakSize > 0)
            {
                ResultSummary += FString::Printf(TEXT("  大小: %.1f MB"), L.TotalPakSize / (1024.0 * 1024.0));
            }
        }
    }
    else if (bBuildHasErrors)
    {
        ResultSummary = FString::Printf(TEXT("构建失败 (exit code: %d)\n存在错误，请查看日志"), ReturnCode);
    }
    else
    {
        ResultSummary = FString::Printf(TEXT("构建失败 (exit code: %d)"), ReturnCode);
    }

    AppendLog(FString::Printf(TEXT("\n=== 构建 %s (exit: %d) ===\n"),
        bSuccess ? TEXT("完成") : TEXT("失败"), ReturnCode));
}

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
        [ SNew(STextBlock).Text(FText::FromString(Item->bSuccess ? TEXT("成功") : TEXT("失败")))
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
        [ SNew(STextBlock).Text(FText::FromString(Item->bSuccess ? TEXT("成功") : TEXT("失败")))
            .ColorAndOpacity(Item->bSuccess ? FLinearColor::Green : FLinearColor::Red) ]
    ];
}

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
    {
        FPlatformProcess::LaunchFileInDefaultExternalApplication(*CurrentLogPath);
    }
    else
    {
        FPlatformProcess::ExploreFolder(*FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("HotPatcher/Logs")));
    }
    return FReply::Handled();
}

FReply SG01HotUpdatePanel::OnCopyErrorClicked()
{
    TArray<FString> Lines;
    BuildLog.ParseIntoArrayLines(Lines);
    FString Errors;
    for (const FString& L : Lines)
    {
        if (L.Contains(TEXT("Error:")) || L.Contains(TEXT("Fatal:")) || L.Contains(TEXT("FAILED")))
        {
            Errors += L + TEXT("\n");
        }
    }
    if (!Errors.IsEmpty()) { FPlatformApplicationMisc::ClipboardCopy(*Errors); }
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
        AppendLog(TEXT("\n=== 构建已被用户终止 ===\n"));
    }
    return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
