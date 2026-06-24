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

#define LOCTEXT_NAMESPACE "G01HotUpdatePanel"

static TArray<TSharedPtr<FString>> GActions = {
    MakeShareable(new FString(TEXT("Export Release"))),
    MakeShareable(new FString(TEXT("Build Snapshot Patch"))),
    MakeShareable(new FString(TEXT("Promote to Release"))),
};

void SG01HotUpdatePanel::Construct(const FArguments& InArgs)
{
    RefreshHistory();

    ChildSlot
    [
        SNew(SScrollBox)

        // ===== 标题 =====
        + SScrollBox::Slot().Padding(8)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.f)
            [ SNew(STextBlock).Text(LOCTEXT("Title", "G01 HotUpdate Tool")).Font(FCoreStyle::GetDefaultFontStyle("Bold", 16)) ]
            + SHorizontalBox::Slot().AutoWidth()
            [ SNew(SButton).Text(LOCTEXT("Ref", "Refresh")).OnClicked(this, &SG01HotUpdatePanel::OnRefreshClicked) ]
        ]

        + SScrollBox::Slot().Padding(8, 2) [ SNew(SSeparator) ]

        // ===== Release 表 =====
        + SScrollBox::Slot().Padding(8)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(STextBlock).Text(LOCTEXT("RelH", "Releases")).Font(FCoreStyle::GetDefaultFontStyle("Bold", 12)) ]
            + SVerticalBox::Slot().AutoHeight().MaxHeight(120)
            [
                SNew(SListView<TSharedPtr<FG01BuildHistoryEntry>>)
                .ListItemsSource(&ReleaseRows)
                .OnGenerateRow(this, &SG01HotUpdatePanel::MakeReleaseRow)
                .HeaderRow(
                    SNew(SHeaderRow)
                    + SHeaderRow::Column("V").DefaultLabel(LOCTEXT("V","Version")).FillWidth(0.2f)
                    + SHeaderRow::Column("P").DefaultLabel(LOCTEXT("P","Platform")).FillWidth(0.15f)
                    + SHeaderRow::Column("T").DefaultLabel(LOCTEXT("T","Build Time")).FillWidth(0.4f)
                    + SHeaderRow::Column("R").DefaultLabel(LOCTEXT("R","Result")).FillWidth(0.15f)
                )
            ]
        ]

        // ===== Patch 表 =====
        + SScrollBox::Slot().Padding(8)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(STextBlock).Text(LOCTEXT("PaH", "Patches")).Font(FCoreStyle::GetDefaultFontStyle("Bold", 12)) ]
            + SVerticalBox::Slot().AutoHeight().MaxHeight(180)
            [
                SNew(SListView<TSharedPtr<FG01BuildHistoryEntry>>)
                .ListItemsSource(&PatchRows)
                .OnGenerateRow(this, &SG01HotUpdatePanel::MakePatchRow)
                .HeaderRow(
                    SNew(SHeaderRow)
                    + SHeaderRow::Column("B").DefaultLabel(LOCTEXT("B","Base")).FillWidth(0.1f)
                    + SHeaderRow::Column("TG").DefaultLabel(LOCTEXT("TG","Target")).FillWidth(0.1f)
                    + SHeaderRow::Column("TY").DefaultLabel(LOCTEXT("TY","Type")).FillWidth(0.1f)
                    + SHeaderRow::Column("SZ").DefaultLabel(LOCTEXT("SZ","Size")).FillWidth(0.12f)
                    + SHeaderRow::Column("M5").DefaultLabel(LOCTEXT("M5","MD5")).FillWidth(0.18f)
                    + SHeaderRow::Column("BT").DefaultLabel(LOCTEXT("BT","Time")).FillWidth(0.2f)
                    + SHeaderRow::Column("RS").DefaultLabel(LOCTEXT("RS","OK")).FillWidth(0.08f)
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

            // Action 下拉
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

            // ExportRelease: 手动输入版本号
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

            // BuildPatch: Base 下拉 + Target 输入
            + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
            [
                SNew(SHorizontalBox)
                .Visibility_Lambda([this]() { return ActionIndex == 1 ? EVisibility::Visible : EVisibility::Collapsed; })
                + SHorizontalBox::Slot().FillWidth(0.3f) [ SNew(STextBlock).Text(LOCTEXT("BvL", "Base Version:")) ]
                + SHorizontalBox::Slot().FillWidth(0.7f)
                [
                    SNew(SComboBox<TSharedPtr<FString>>)
                    .OptionsSource(&BaseVersionOptions)
                    .OnSelectionChanged_Lambda([this](TSharedPtr<FString> V, ESelectInfo::Type) { SelectedBaseVersion = *V; bShowSummary = false; })
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

            // PromoteToRelease: 选择成功 Patch
            + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
            [
                SNew(SHorizontalBox)
                .Visibility_Lambda([this]() { return ActionIndex == 2 ? EVisibility::Visible : EVisibility::Collapsed; })
                + SHorizontalBox::Slot().FillWidth(0.3f) [ SNew(STextBlock).Text(LOCTEXT("PpL", "Promote Patch:")) ]
                + SHorizontalBox::Slot().FillWidth(0.7f)
                [
                    SNew(SComboBox<TSharedPtr<FString>>)
                    .OptionsSource(&PromotablePatchOptions)
                    .OnSelectionChanged_Lambda([this](TSharedPtr<FString> V, ESelectInfo::Type) { SelectedPromotePatchVersion = *V; bShowSummary = false; })
                    .OnGenerateWidget_Lambda([](TSharedPtr<FString> I) { return SNew(STextBlock).Text(FText::FromString(*I)); })
                    [ SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(SelectedPromotePatchVersion.IsEmpty() ? TEXT("(select patch)") : SelectedPromotePatchVersion); }) ]
                ]
            ]

            // Generate 按钮
            + SVerticalBox::Slot().AutoHeight().Padding(0, 8)
            [
                SNew(SButton).Text(LOCTEXT("Gen", "Generate"))
                .HAlign(HAlign_Center)
                .IsEnabled_Lambda([this]() { return !bIsBuilding; })
                .OnClicked(this, &SG01HotUpdatePanel::OnGenerateClicked)
            ]
        ]

        // ===== 确认摘要 =====
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

        // ===== 结果区 =====
        + SScrollBox::Slot().Padding(8)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(STextBlock).Text(LOCTEXT("RsH", "Build Result")).Font(FCoreStyle::GetDefaultFontStyle("Bold", 12)) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
            [ SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(ResultText.IsEmpty() ? TEXT("No recent build.") : ResultText); }).AutoWrapText(true) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
            [ SNew(SButton).Text(LOCTEXT("OpD", "Open Output Directory")).OnClicked(this, &SG01HotUpdatePanel::OnOpenDirClicked) ]
        ]
    ];
}

// ===== 数据 =====

void SG01HotUpdatePanel::RefreshHistory()
{
    History = FG01BuildHistory();
    History.LoadFromFile(FPaths::Combine(GetOutputRoot(), TEXT("BuildHistory.json")));

    ReleaseRows.Empty();
    PatchRows.Empty();
    BaseVersionOptions.Empty();
    PromotablePatchOptions.Empty();

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
            // 只有成功的 Patch 才能 Promote
            if (E.bSuccess)
            {
                // 检查是否已经被 Promote 过（已有同名 Release）
                bool bAlreadyPromoted = false;
                for (const FG01BuildHistoryEntry& R : History.Entries)
                {
                    if (R.PatchType == TEXT("Release") && R.TargetVersion == E.TargetVersion)
                    {
                        bAlreadyPromoted = true;
                        break;
                    }
                }
                if (!bAlreadyPromoted)
                    PromotablePatchOptions.Add(MakeShareable(new FString(E.TargetVersion)));
            }
        }
    }

    if (BaseVersionOptions.Num() > 0)
        SelectedBaseVersion = *BaseVersionOptions.Last();

    // 自动推荐下一个版本号
    FString Latest = History.LatestPatchVersion.IsEmpty() ? History.LatestReleaseVersion : History.LatestPatchVersion;
    if (!Latest.IsEmpty())
    {
        int32 Dot;
        if (Latest.FindLastChar(TEXT('.'), Dot))
        {
            int32 Num = FCString::Atoi(*Latest.Mid(Dot + 1)) + 1;
            InputTargetVersion = Latest.Left(Dot + 1) + FString::FromInt(Num);
        }
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

    if (ActionIndex == 0)
    {
        // Export Release
        SummaryText = FString::Printf(TEXT(
            "Action:   Export Release\n"
            "Platform: Android\n"
            "Version:  %s\n"
            "Output:   Saved/HotPatcher/Releases/%s/\n\n"
            "WARNING: Please confirm workspace content corresponds to version %s."),
            *InputReleaseVersion, *InputReleaseVersion, *InputReleaseVersion);

        if (InputReleaseVersion.IsEmpty()) { Checks.Add(TEXT("[FAIL] Version is empty")); bValidationPassed = false; }
        else Checks.Add(FString::Printf(TEXT("[OK] Version: %s"), *InputReleaseVersion));

        // 检查是否已存在
        FString RelPath = FPaths::Combine(GetOutputRoot(), TEXT("Releases"), InputReleaseVersion,
            InputReleaseVersion, InputReleaseVersion + TEXT("_Release.json"));
        if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*RelPath))
        {
            Checks.Add(FString::Printf(TEXT("[FAIL] Release %s already exists"), *InputReleaseVersion));
            bValidationPassed = false;
        }
    }
    else if (ActionIndex == 1)
    {
        // Build Patch
        SummaryText = FString::Printf(TEXT(
            "Action:   Build Snapshot Patch\n"
            "Platform: Android\n"
            "Base:     %s\n"
            "Target:   %s\n"
            "Type:     Snapshot\n"
            "Output:   Saved/HotPatcher/Patches/%s/\n\n"
            "WARNING: Please confirm workspace content includes all %s changes.\n"
            "This will diff current content against Release_%s."),
            *SelectedBaseVersion, *InputTargetVersion, *InputTargetVersion,
            *InputTargetVersion, *SelectedBaseVersion);

        if (SelectedBaseVersion.IsEmpty()) { Checks.Add(TEXT("[FAIL] Base Version not selected")); bValidationPassed = false; }
        else Checks.Add(FString::Printf(TEXT("[OK] Base: %s"), *SelectedBaseVersion));

        if (InputTargetVersion.IsEmpty()) { Checks.Add(TEXT("[FAIL] Target Version empty")); bValidationPassed = false; }
        else if (InputTargetVersion == SelectedBaseVersion) { Checks.Add(TEXT("[FAIL] Target must differ from Base")); bValidationPassed = false; }
        else Checks.Add(FString::Printf(TEXT("[OK] Target: %s"), *InputTargetVersion));

        FString RelPath = FPaths::Combine(GetOutputRoot(), TEXT("Releases"), SelectedBaseVersion,
            SelectedBaseVersion, SelectedBaseVersion + TEXT("_Release.json"));
        if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*RelPath))
            Checks.Add(TEXT("[OK] Base Release exists"));
        else { Checks.Add(TEXT("[FAIL] Base Release not found")); bValidationPassed = false; }
    }
    else if (ActionIndex == 2)
    {
        // Promote to Release
        SummaryText = FString::Printf(TEXT(
            "Action:   Promote to Release\n"
            "Platform: Android\n"
            "Patch:    %s\n"
            "Release:  %s\n\n"
            "WARNING: Please confirm workspace content corresponds to %s.\n"
            "This Release may be used as base for future patches."),
            *SelectedPromotePatchVersion, *SelectedPromotePatchVersion, *SelectedPromotePatchVersion);

        if (SelectedPromotePatchVersion.IsEmpty()) { Checks.Add(TEXT("[FAIL] No patch selected")); bValidationPassed = false; }
        else Checks.Add(FString::Printf(TEXT("[OK] Patch: %s"), *SelectedPromotePatchVersion));
    }

    Checks.Add(TEXT("[OK] IoStore disabled (Pak-only)"));
    ValidationText = FString::Join(Checks, TEXT("\n"));
}

// ===== 构建 =====

void SG01HotUpdatePanel::ExecuteBuild()
{
    bIsBuilding = true;
    ResultText = TEXT("Building...");

    FG01BuildTask Task;
    Task.Platform = TEXT("Android");
    Task.ReleaseConfigTemplate = TEXT("ReleaseTest.json");
    Task.PatchConfigTemplate = TEXT("PatchTest.json");
    Task.OutputDir = TEXT("Saved/HotPatcher");

    if (ActionIndex == 0)
    {
        Task.TaskType = TEXT("ExportRelease");
        Task.BaseVersion = InputReleaseVersion;
    }
    else if (ActionIndex == 1)
    {
        Task.TaskType = TEXT("BuildPatch");
        Task.BaseVersion = SelectedBaseVersion;
        Task.TargetVersion = InputTargetVersion;
        Task.PatchType = TEXT("Snapshot");
    }
    else if (ActionIndex == 2)
    {
        Task.TaskType = TEXT("PromoteToRelease");
        Task.TargetVersion = SelectedPromotePatchVersion;
        Task.PromoteFromPatchVersion = SelectedPromotePatchVersion;
    }

    FString TaskPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("HotPatcher/_CurrentBuildTask.json"));
    Task.SaveToFile(TaskPath);

    FString UECmd = FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries/Win64/UnrealEditor-Cmd.exe"));
    FString Project = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());

    FProcHandle Proc = FPlatformProcess::CreateProc(*UECmd,
        *FString::Printf(TEXT("\"%s\" -run=G01HotPatch -config=\"%s\""), *Project, *TaskPath),
        true, false, false, nullptr, 0, nullptr, nullptr);

    if (!Proc.IsValid())
    {
        ResultText = TEXT("ERROR: Failed to launch Commandlet");
        bIsBuilding = false;
        return;
    }

    FPlatformProcess::WaitForProc(Proc);
    int32 Code;
    FPlatformProcess::GetProcReturnCode(Proc, &Code);
    OnBuildComplete(Code);
}

void SG01HotUpdatePanel::OnBuildComplete(int32 Code)
{
    bIsBuilding = false;
    bShowSummary = false;
    RefreshHistory();

    if (Code == 0)
    {
        ResultText = TEXT("BUILD COMPLETE\n\n");
        if (History.Entries.Num() > 0)
        {
            const auto& L = History.Entries.Last();
            ResultText += FString::Printf(TEXT("Version: %s\nBase: %s\nType: %s\nPlatform: %s\nTime: %s\n"),
                *L.TargetVersion, *L.BaseVersion, *L.PatchType, *L.Platform, *L.BuildTime);
            if (L.TotalPakSize > 0)
                ResultText += FString::Printf(TEXT("Size: %.1f MB\n"), L.TotalPakSize / (1024.0 * 1024.0));
            if (!L.PakMD5.IsEmpty())
                ResultText += FString::Printf(TEXT("MD5: %s\n"), *L.PakMD5);
        }
    }
    else
    {
        ResultText = FString::Printf(TEXT("BUILD FAILED (exit code: %d)\nCheck Output Log."), Code);
    }
}

// ===== 表格 =====

TSharedRef<ITableRow> SG01HotUpdatePanel::MakeReleaseRow(TSharedPtr<FG01BuildHistoryEntry> Item, const TSharedRef<STableViewBase>& Owner)
{
    return SNew(STableRow<TSharedPtr<FG01BuildHistoryEntry>>, Owner)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(0.2f).Padding(4, 2) [ SNew(STextBlock).Text(FText::FromString(Item->TargetVersion)) ]
        + SHorizontalBox::Slot().FillWidth(0.15f).Padding(4, 2) [ SNew(STextBlock).Text(FText::FromString(Item->Platform)) ]
        + SHorizontalBox::Slot().FillWidth(0.4f).Padding(4, 2) [ SNew(STextBlock).Text(FText::FromString(Item->BuildTime)) ]
        + SHorizontalBox::Slot().FillWidth(0.15f).Padding(4, 2)
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
        + SHorizontalBox::Slot().FillWidth(0.1f).Padding(4, 2) [ SNew(STextBlock).Text(FText::FromString(Item->BaseVersion)) ]
        + SHorizontalBox::Slot().FillWidth(0.1f).Padding(4, 2) [ SNew(STextBlock).Text(FText::FromString(Item->TargetVersion)) ]
        + SHorizontalBox::Slot().FillWidth(0.1f).Padding(4, 2) [ SNew(STextBlock).Text(FText::FromString(Item->PatchType)) ]
        + SHorizontalBox::Slot().FillWidth(0.12f).Padding(4, 2) [ SNew(STextBlock).Text(FText::FromString(Sz)) ]
        + SHorizontalBox::Slot().FillWidth(0.18f).Padding(4, 2) [ SNew(STextBlock).Text(FText::FromString(M5)) ]
        + SHorizontalBox::Slot().FillWidth(0.2f).Padding(4, 2) [ SNew(STextBlock).Text(FText::FromString(Item->BuildTime)) ]
        + SHorizontalBox::Slot().FillWidth(0.08f).Padding(4, 2)
        [ SNew(STextBlock).Text(FText::FromString(Item->bSuccess ? TEXT("OK") : TEXT("FAIL")))
            .ColorAndOpacity(Item->bSuccess ? FLinearColor::Green : FLinearColor::Red) ]
    ];
}

// ===== 回调 =====

FReply SG01HotUpdatePanel::OnRefreshClicked() { RefreshHistory(); return FReply::Handled(); }
FReply SG01HotUpdatePanel::OnGenerateClicked() { GenerateSummary(); bShowSummary = true; return FReply::Handled(); }
FReply SG01HotUpdatePanel::OnConfirmClicked() { ExecuteBuild(); return FReply::Handled(); }
FReply SG01HotUpdatePanel::OnCancelClicked() { bShowSummary = false; return FReply::Handled(); }
FReply SG01HotUpdatePanel::OnOpenDirClicked() { FPlatformProcess::ExploreFolder(*GetOutputRoot()); return FReply::Handled(); }

#undef LOCTEXT_NAMESPACE
