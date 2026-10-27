#include "Components/CarPassageComponent.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"

UCarPassageComponent::UCarPassageComponent()
{
  TriggerBoxExtent = FVector(200.0, 90.0, 75.0);
  TriggerBoxOffset = FVector(0.0, 0.0, 75.0);
  // 车辆通行使用基类默认参数，无需额外初始化。
}

// 准备表现：缓存 Mesh 并恢复其显示状态。
bool UCarPassageComponent::PreparePresentation(FString &OutError)
{
  OutError.Reset();
  CachedMesh = FindOwnerMesh();

  // 车辆必须带骨骼网格组件。
  if (!CachedMesh.IsValid())
  {
    OutError = TEXT("Car actor has no SkeletalMeshComponent.");
    return false;
  }

  // 每次复用都恢复 Mesh 自身的显示状态。
  CachedMesh->SetHiddenInGame(false, true);
  CachedMesh->SetVisibility(true, true);
  CachedMesh->SetOwnerNoSee(false);
  CachedMesh->SetOnlyOwnerSee(false);

  return true;
}

// 开始表现：循环播放驾驶动画并按配置设置播放速率。
void UCarPassageComponent::BeginPresentation()
{
  if (CachedMesh.IsValid() && IsValid(DriveAnimation.Get()))
  {
    CachedMesh->PlayAnimation(DriveAnimation.Get(), true);
    CachedMesh->SetPlayRate(AnimationPlayRate);
  }
}

// 结束表现：停止动画并释放缓存引用。
void UCarPassageComponent::EndPresentation()
{
  if (CachedMesh.IsValid() && IsValid(DriveAnimation.Get()))
  {
    CachedMesh->Stop();
  }

  CachedMesh.Reset();
}
