#include "LaunchPlayerController.h"
#include "HotUpdateSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SConstraintCanvas.h"

void ALaunchPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// 显示鼠标，让用户可以点击按钮
	bShowMouseCursor = true;

	// 安全检查：GEngine 和 GameViewport 必须有效才能添加 Slate UI
	if (!GEngine || !GEngine->GameViewport)
	{
		UE_LOG(LogTemp, Error, TEXT("[Launch] GameViewport not available, cannot create UI"));
		return;
	}

	// 屏幕提示：帮助确认 LaunchMap 已正确加载（左上角可见）
	GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Yellow, TEXT("[Launch] LaunchMap ready, click Enter Game to apply patch"));

	// 使用 SConstraintCanvas 布局，将按钮固定在屏幕正中央
	LaunchWidget =
		SNew(SConstraintCanvas)
		+ SConstraintCanvas::Slot()
		.Anchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f))
		.Alignment(FVector2D(0.5f, 0.5f))
		.AutoSize(true)
		[
			SNew(SButton)
			.OnClicked_Lambda([this]()
			{
				OnEnterGameClicked();
				return FReply::Handled();
			})
			.ContentPadding(FMargin(40.f, 20.f))
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Enter Game")))
				.Font(FSlateFontInfo(FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Bold.ttf"), 24))
				.Justification(ETextJustify::Center)
			]
		];

	GEngine->GameViewport->AddViewportWidgetContent(LaunchWidget.ToSharedRef());

	UE_LOG(LogTemp, Log, TEXT("[Launch] UI created, waiting for user input"));
}

void ALaunchPlayerController::OnEnterGameClicked()
{
	// 防止重复点击
	if (bIsTransitioning)
		return;
	bIsTransitioning = true;

	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, TEXT("[Launch] Scanning patches..."));
	UE_LOG(LogTemp, Log, TEXT("[Launch] Enter Game clicked, running hot update..."));

	// 调用热更子系统扫描并挂载补丁
	UHotUpdateSubsystem* Subsystem = GetGameInstance()->GetSubsystem<UHotUpdateSubsystem>();
	if (Subsystem)
	{
		int32 InstalledCount = Subsystem->ScanAndInstallPatches();

		// 屏幕上显示安装结果
		FString Msg = FString::Printf(TEXT("[Launch] %d patch(es) installed, loading TestMap..."), InstalledCount);
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, Msg);
		UE_LOG(LogTemp, Log, TEXT("%s"), *Msg);
	}
	else
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Orange, TEXT("[Launch] No subsystem, skipping patch"));
		UE_LOG(LogTemp, Warning, TEXT("[Launch] HotUpdateSubsystem not found"));
	}

	// 移除 Slate UI，防止切关卡后残留
	if (GEngine && GEngine->GameViewport && LaunchWidget.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(LaunchWidget.ToSharedRef());
		LaunchWidget.Reset();
	}

	UE_LOG(LogTemp, Log, TEXT("[Launch] Opening TestMap..."));
	UGameplayStatics::OpenLevel(this, TEXT("TestMap"));
}
