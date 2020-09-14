﻿#pragma once

#include "FINModuleBase.h"
#include "WidgetComponent.h"
#include "Computer/FINComputerScreen.h"
#include "FINModuleScreen.generated.h"

UCLASS()
class AFINModuleScreen : public AFINModuleBase, public IFINScreenInterface {
	GENERATED_BODY()
private:
    UPROPERTY(SaveGame, Replicated)
    FFINNetworkTrace GPU;

	bool bWasGPUValid = false;
	
public:
    TSharedPtr<SWidget> Widget;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite)
	UWidgetComponent* WidgetComponent;

	/**
	* This event gets triggered when a new widget got set by the GPU
	*/
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, BlueprintAssignable)
    FScreenWidgetUpdate OnWidgetUpdate;

	/**
	* This event gets triggered when a new GPU got bound
	*/
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, BlueprintAssignable)
    FScreenGPUUpdate OnGPUUpdate;

	AFINModuleScreen();

	// Begin AActor
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type endPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	// End AActor
	
	// Begin IFINScreen
	virtual void BindGPU(const FFINNetworkTrace& gpu) override;
	virtual FFINNetworkTrace GetGPU() const override;
	virtual void SetWidget(TSharedPtr<SWidget> widget) override;
	virtual TSharedPtr<SWidget> GetWidget() const override;
	// End IFINScreen

	// Begin IFINNetworkCustomType
	virtual FString GetCustomTypeName_Implementation() const override { return TEXT("ModuleScreen"); }
	// End IFINNetworkCustomType

	UFUNCTION(NetMulticast, Reliable)
	void OnGPUValidationChanged(bool bWasValid);

	UFUNCTION(NetMulticast, Reliable)
	void NetMulti_OnGPUUpdate();
};
