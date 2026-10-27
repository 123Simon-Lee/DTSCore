#include "Components/PersonPassageComponent.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"

UPersonPassageComponent::UPersonPassageComponent()
{
  // 人物通行默认参数：步行速度、抬升胶囊半高、不跟随俯仰。
  DefaultSpeed = 140.0f;
  bUseCharacterCapsuleHeight = true;
  bFollowPitch = false;
}

// 准备表现：校验 Mesh/动画/速度，缓存 Mesh，并强制恢复人物可见。
bool UPersonPassageComponent::PreparePresentation(FString &OutError)
{
  OutError.Reset();
  CachedMesh.Reset();

  USkeletalMeshComponent *Mesh = FindOwnerMesh();

  // 人物必须带骨骼网格组件。
  if (!IsValid(Mesh))
  {
    OutError = TEXT("Person actor has no SkeletalMeshComponent.");
    return false;
  }

  // 播放行走动画时校验动画资源与基准速度。
  if (bPlayWalkAnimation)
  {
    if (!IsValid(WalkAnimation.Get()))
    {
      OutError = TEXT("Assign WalkAnimation to PersonPassageComponent.");
      return false;
    }

    if (!FMath::IsFinite(ReferenceWalkSpeed) || ReferenceWalkSpeed <= 0.0f)
    {
      OutError = TEXT("ReferenceWalkSpeed must be positive.");
      return false;
    }
  }

  CachedMesh = Mesh;

  // 展示人物不受第一人称隐藏 Mesh 设置影响。
  Mesh->SetHiddenInGame(false, true);
  Mesh->SetVisibility(true, true);
  Mesh->SetOwnerNoSee(false);
  Mesh->SetOnlyOwnerSee(false);

  return true;
}

// 开始表现：循环播放行走动画。
void UPersonPassageComponent::BeginPresentation()
{
  if (bPlayWalkAnimation && CachedMesh.IsValid())
  {
    CachedMesh->PlayAnimation(WalkAnimation.Get(), true);
  }
}

// 每帧按“当前速度 / 基准速度”换算动画播放速率。
void UPersonPassageComponent::UpdatePresentation(float DeltaTime, float DeltaDistance)
{
  if (bPlayWalkAnimation && CachedMesh.IsValid())
  {
    CachedMesh->SetPlayRate(FMath::Clamp(
        CurrentSpeed / ReferenceWalkSpeed,
        0.1f,
        3.0f));
  }
}

// 结束表现：停止动画。
void UPersonPassageComponent::EndPresentation()
{
  if (bPlayWalkAnimation && CachedMesh.IsValid())
  {
    CachedMesh->Stop();
  }
}
