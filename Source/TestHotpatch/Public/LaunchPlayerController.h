#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Widgets/SWidget.h"
#include "LaunchPlayerController.generated.h"

UCLASS()
class TESTHOTPATCH_API ALaunchPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;

private:
	void OnEnterGameClicked();

	TSharedPtr<SWidget> LaunchWidget;
	bool bIsTransitioning = false;
};
