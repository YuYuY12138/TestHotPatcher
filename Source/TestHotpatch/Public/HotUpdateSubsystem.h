#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "HotUpdateSubsystem.generated.h"

UCLASS()
class TESTHOTPATCH_API UHotUpdateSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UFUNCTION(BlueprintCallable)
	int32 ScanAndInstallPatches();

private:
	FString GetPendingDir() const;
	FString GetPaksDir() const;
};
