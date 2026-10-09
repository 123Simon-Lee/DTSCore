// Fill out your copyright notice in the Description page of Project Settings.


#include "Core/Game/TwinScriptHUD.h"

void ATwinScriptHUD::BeginPlay()
{
	Super::BeginPlay();
	// 获取Viewpoint object并绑定接口
	GEngine->GameViewport->Viewport->ViewportResizedEvent.AddUObject(this, &ATwinScriptHUD::ViewportResize);
}

void ATwinScriptHUD::ViewportResize(FViewport* Viewport, uint32 Size)
{
	// 已启动跟随时，窗口缩放后按保存的比例自动重新应用
	if (bViewportFollowing)
	{
		ApplyViewportSize();
	}
	OnViewportResized.Broadcast();
}

void ATwinScriptHUD::SetViewportRegion(FVector2D Origin, FVector2D Size)
{
	ViewportOrigin = Origin;
	ViewportSize = Size;
	bViewportFollowing = true;
	// 立即应用比例
	ApplyViewportSize();
}

void ATwinScriptHUD::ResetViewportRegion()
{
	// 还原为引擎默认的全屏渲染区域
	bViewportFollowing = false;
	ViewportOrigin = FVector2D::ZeroVector;
	ViewportSize = FVector2D::UnitVector;
	ApplyViewportSize();
}

void ATwinScriptHUD::ApplyViewportSize()
{
	if (GEngine && GEngine->GameViewport)
	{
		// SplitscreenInfo 使用 0~1 比例，直接赋值即可
		GEngine->GameViewport->SplitscreenInfo[0].PlayerData[0].OriginX = ViewportOrigin.X;
		GEngine->GameViewport->SplitscreenInfo[0].PlayerData[0].OriginY = ViewportOrigin.Y;
		GEngine->GameViewport->SplitscreenInfo[0].PlayerData[0].SizeX = ViewportSize.X;
		GEngine->GameViewport->SplitscreenInfo[0].PlayerData[0].SizeY = ViewportSize.Y;
	}
}
