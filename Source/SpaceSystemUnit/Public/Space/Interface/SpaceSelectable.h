// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "SpaceSelectable.generated.h"

class ASpace;

// This class does not need to be modified.
UINTERFACE()
class USpaceSelectable : public UInterface
{
  GENERATED_BODY()
};

/**
 * 空间选择接口，实现此接口以从空间子系统获取选择信息
 * @attention 由于空间子系统只能选择空间，请使用空间子类实现此接口
 */
class SPACESYSTEMUNIT_API ISpaceSelectable
{
  GENERATED_BODY()
public:
  // --- C++使用API

  /**
   * 当前空间被选择
   */
  virtual void OnSpaceSelected() = 0;

  /**
   * 当前空间取消选择
   */
  virtual void OnSpaceDeselected() = 0;

  // --- 蓝图继承API

  /**
   * 当前空间被选择
   */
  UFUNCTION(BlueprintImplementableEvent, Category = "SpaceSelector", meta = (DisplayName = "OnSpaceSelected"))
  void ReceiveOnSpaceSelected();

  /**
   * 当前空间取消选择
   */
  UFUNCTION(BlueprintImplementableEvent, Category = "SpaceSelector", meta = (DisplayName = "OnSpaceDeselected"))
  void ReceiveOnSpaceDeselected();
};
