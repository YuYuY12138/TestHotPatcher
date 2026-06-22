#include "LaunchPlayerController.h"
#include "HotUpdateSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SConstraintCanvas.h"

void ALaunchPlayerController::BeginPlay()
{
	Super::BeginPlay();

	bShowMouseCursor = true;

	if (!GEngine || !GEngine->GameViewport)
		return;

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

	UE_LOG(LogTemp, Log, TEXT("[Launch] Launch UI created"));
}

void ALaunchPlayerController::OnEnterGameClicked()
{
	if (bIsTransitioning)
		return;
	bIsTransitioning = true;

	UE_LOG(LogTemp, Log, TEXT("[Launch] Enter Game clicked, starting hot update..."));

	UHotUpdateSubsystem* Subsystem = GetGameInstance()->GetSubsystem<UHotUpdateSubsystem>();
	if (Subsystem)
	{
		int32 Count = Subsystem->ScanAndInstallPatches();
		UE_LOG(LogTemp, Log, TEXT("[Launch] Installed %d patches, loading TestMap..."), Count);
	}

	if (GEngine && GEngine->GameViewport && LaunchWidget.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(LaunchWidget.ToSharedRef());
		LaunchWidget.Reset();
	}

	UGameplayStatics::OpenLevel(this, TEXT("TestMap"));
}
