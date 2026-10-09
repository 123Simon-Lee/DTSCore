// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Blueprint/UserWidget.h"
#include "TwinScriptHUD.generated.h"

//定义当视口大小发生变化时的代理
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnViewportResized);

/**
 * 
 */
UCLASS()
class DTSCORE_API ATwinScriptHUD : public AHUD
{
	GENERATED_BODY()



public:
	/** 设置视口渲染区域：Origin 为左上角比例，Size 为宽高比例（0~1）。 */
	UFUNCTION(BlueprintCallable)
	void SetViewportRegion(FVector2D Origin, FVector2D Size);

	/** 恢复视口渲染区域为引擎默认的全屏。 */
	UFUNCTION(BlueprintCallable)
	void ResetViewportRegion();

	UPROPERTY(BlueprintAssignable)
	FOnViewportResized OnViewportResized;

protected:
	virtual void BeginPlay() override;

private:
	/** 视口尺寸变化回调。 */
	void ViewportResize(FViewport* Viewport, uint32 Size);

	/** 使用当前保存的比例设置视口渲染区域。 */
	void ApplyViewportSize();

	/** 是否已启动视口跟随（自定义渲染区域比例）。 */
	bool bViewportFollowing = false;

	/** 渲染区域原点比例（0~1）。 */
	FVector2D ViewportOrigin = FVector2D::ZeroVector;

	/** 渲染区域大小比例（0~1），默认全屏。 */
	FVector2D ViewportSize = FVector2D::UnitVector;
 
};
