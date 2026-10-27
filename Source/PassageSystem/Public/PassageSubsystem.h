// 数据入口、通道注册、队列调度
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TimerManager.h"
#include "PassageTypes.h"

#include "PassageSubsystem.generated.h"

class APassageLaneActor;
class UPassageMovementComponent;

struct FPassageChannelRuntime
{
  TWeakObjectPtr<APassageLaneActor> Channel;

  EPassageSubjectType SubjectType = EPassageSubjectType::Person;

  TArray<FPassageEventData> Pending;

  FPassageEventData CurrentData;
  FString RunId;

  TWeakObjectPtr<AActor> ActiveActor;
  TWeakObjectPtr<UPassageMovementComponent> ActiveMovement;

  FTimerHandle PumpTimer;

  bool bPumpScheduled = false;
  bool bDispatchEnabled = true;
  bool bFaulted = false;
};

UClASS() class PASSAGESYSTEM_API UPassageSubsystem : Public UWorldSubsystem
{
  GENERATED_BODY()
public:
  UFUNCTION(BlueprintPure, Category = "Passage", meta = (WorldContext = "WorldContextObject"))
  static UPassageSubsystem *GetPassageSubsystem(const UObject *WorldContextObject);

  UFUNCTION(BlueprintCallable, Category = "Passage")
  bool RegisterChannel(APassageLaneActor *Channel);

  UFUNCTION(BlueprintCallable, Category = "Passage")
  void UnregisterChannel(APassageLaneActor *Channel);

  UFUNCTION(BlueprintPure, Category = "Passage")
  APassageLaneActor *FindChannel(FName ChannelId) const;

  UFUNCTION(BlueprintCallable, Category = "Passage")
  bool ReceivePersonPassageData(const FPassageEventData &Data);

  UFUNCTION(BlueprintCallable, Category = "Passage")
  bool ReceiveCarPassageData(const FPassageEventData &Data);

  UFUNCTION(BlueprintCallable, Category = "Passage")
  bool ReceivePersonJson(const FString &Json, FString &OutError);

  UFUNCTION(BlueprintCallable, Category = "Passage")
  bool ReceiveCarJson(const FString &Json, FString &OutError);

  // 控制下一条是否开始，不影响当前对象移动。
  UFUNCTION(BlueprintCallable, Category = "Passage")
  void SetChannelDispatchEnabled(FName ChannelId, bool bEnabled);

  UFUNCTION(BlueprintCallable, Category = "Passage")
  void CancelChannel(FName ChannelId, bool bClearPending);

  UFUNCTION(BlueprintCallable, Category = "Passage")
  void ResetChannel(FName ChannelId);

  UFUNCTION(BlueprintPure, Category = "Passage")
  int32 GetPendingCount(FName ChannelId) const;

  UFUNCTION(BlueprintPure, Category = "Passage")
  bool IsChannelFaulted(FName ChannelId) const;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Passage", meta = (ClampMin = "1.0"))
  float DuplicateRetentionSeconds = 300.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Passage", meta = (ClampMin = "1"))
  int32 MaxRememberedEvents = 4096;

  UPROPERTY(BlueprintAssignable, Category = "Passage")
  FPassageAcceptedEvent OnDataAccepted;

  // Actor 已创建但尚未开始移动，可设置车牌、外观等。
  UPROPERTY(BlueprintAssignable, Category = "Passage")
  FPassageActorEvent OnPassageSpawned;

  UPROPERTY(BlueprintAssignable, Category = "Passage")
  FPassageActorEvent OnPassageStarted;

  // 成功时，展示 Actor 已隐藏并销毁。
  UPROPERTY(BlueprintAssignable, Category = "Passage")
  FPassageCompletedEvent OnPassageFinished;

  UPROPERTY(BlueprintAssignable, Category = "Passage")
  FPassageRejectedEvent OnPassageRejected;

  virtual void Initialize(FSubsystemCollectionBase &Collection) override;

  virtual void Deinitialize() override;

protected:
  virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
  bool ReceiveJson(
      const FString &Json,
      EPassageSubjectType Type,
      FString &OutError);

  bool Enqueue(
      const FPassageEventData &Input,
      EPassageSubjectType Type,
      FString &OutError);

  bool Reject(
      const FPassageEventData &Data,
      EPassageSubjectType Type,
      const FString &Reason,
      FString &OutError);

  bool FindRegisteredId(
      const APassageLaneActor *Channel,
      FName &OutId) const;

  bool IsCurrentRun(
      FName ChannelId,
      const FString &RunId) const;

  void SchedulePump(FName ChannelId);
  void PumpChannel(FName ChannelId);

  void FinishTask(
      FName ChannelId,
      bool bSuccess,
      const FString &Reason,
      bool bMarkFaulted);

  void DisposeActor(
      AActor *Actor,
      UPassageMovementComponent *Movement);

  void RemoveChannel(
      FName ChannelId,
      const FString &Reason);

  void RemoveDeviceMappings(FName ChannelId);
  void PruneRememberedEvents();

  UFUNCTION()
  void HandleMovementFinished(UPassageMovementComponent *Movement, bool bSuccess, const FString &Reason);

  TMap<FName, FPassageChannelRuntime> Channels;
  TMap<FString, FName> DeviceToChannel;

  TMap<TWeakObjectPtr<UPassageMovementComponent>, FName> MovementToChannel;

  TMap<FString, double> RememberedEvents;
  TSet<FString> InFlightEvents;

  bool bShuttingDown = false;
};