#include "Core/Game/TwinSpectatorPawn.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PrimitiveComponent.h"
#include "Components/WidgetComponent.h"
#include "InputModifiers.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/GameUserSettings.h"
#include <Kismet/KismetMathLibrary.h>
#include "Core/FunctionLibrary/UDSTCoreFunctionLibrary.h"
#include "Engine/Engine.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Engine/World.h"

ATwinSpectatorPawn::ATwinSpectatorPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>("Root");
	SetRootComponent(Root);
	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = true;
	bUseControllerRotationRoll = false;

	SpringArm = CreateDefaultSubobject<USpringArmComponent>("SpringArm");
	SpringArm->SetupAttachment(Root);
	SpringArm->TargetArmLength = 6000.f;
	SpringArm->bDoCollisionTest = false;
	SpringArm->bEnableCameraLag = true;
	SpringArm->bEnableCameraRotationLag = true;
	SpringArm->CameraRotationLagSpeed = 10.f;
	SpringArm->bUsePawnControlRotation = true;

	Camera = CreateDefaultSubobject<UCameraComponent>("Camera");
	Camera->SetupAttachment(SpringArm);
	Camera->bUsePawnControlRotation = false;
}

FString ATwinSpectatorPawn::GetCameraConfigFilePath() const
{
	return FPaths::ConvertRelativePathToFull(
		FPaths::Combine(
			FPaths::ProjectContentDir(),
			TEXT("Softwareconfig.ini")));
}

void ATwinSpectatorPawn::ReloadCameraConfig(const bool bForceReload)
{
	const FString ConfigPath = GetCameraConfigFilePath();
	IFileManager& FileManager = IFileManager::Get();

	if (!FileManager.FileExists(*ConfigPath))
	{
		if (!bCameraConfigMissingLogged)
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[Softwareconfig] 配置文件不存在: %s"),
				*ConfigPath);
			bCameraConfigMissingLogged = true;
		}
		return;
	}

	bCameraConfigMissingLogged = false;
	const FDateTime CurrentTimestamp = FileManager.GetTimeStamp(*ConfigPath);
	if (!bForceReload && CurrentTimestamp == LastCameraConfigTimestamp)
	{
		return;
	}

	FConfigFile RuntimeConfig;
	RuntimeConfig.Read(ConfigPath);
	if (RuntimeConfig.Num() == 0)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[CameraConfig] 配置文件为空或解析失败: %s"),
			*ConfigPath);
		return;
	}

	// 只有成功读到配置后才记录时间；解析失败时下次轮询仍会重试。
	LastCameraConfigTimestamp = CurrentTimestamp;

	// 兼容项目现有的 CameraSetttings（三个 t）以及标准 CameraSettings 拼写。
	const auto GetCameraFloat =
		[&RuntimeConfig](const TCHAR* Key, float& OutValue)
		{
			return RuntimeConfig.GetFloat(
				TEXT("CameraSetttings"),
				Key,
				OutValue) ||
				RuntimeConfig.GetFloat(
					TEXT("CameraSettings"),
					Key,
					OutValue);
		};

	bool bSpeedChanged = false;
	float NewValue = 0.f;

	if (GetCameraFloat(TEXT("ZoomSpeed"), NewValue) &&
		FMath::IsFinite(NewValue) &&
		NewValue >= 0.f &&
		!FMath::IsNearlyEqual(ZoomSpeed, NewValue))
	{
		ZoomSpeed = NewValue;
		ZoomVelocity = 0.f;
		bSpeedChanged = true;
	}

	// MoveSpeed 允许为负数：当前鸟瞰平移方向依靠其符号保持原有手感。
	if (GetCameraFloat(TEXT("MoveSpeed"), NewValue) &&
		FMath::IsFinite(NewValue) &&
		!FMath::IsNearlyEqual(MoveSpeed, NewValue))
	{
		MoveSpeed = NewValue;
		bSpeedChanged = true;
	}

	if (GetCameraFloat(TEXT("RotateSpeed"), NewValue) &&
		FMath::IsFinite(NewValue) &&
		NewValue >= 0.f &&
		!FMath::IsNearlyEqual(RotateSpeed, NewValue))
	{
		RotateSpeed = NewValue;
		bSpeedChanged = true;
	}

	if (GetCameraFloat(TEXT("PollInterval"), NewValue) &&
		FMath::IsFinite(NewValue) &&
		NewValue >= 0.1f)
	{
		CameraConfigPollInterval = NewValue;
	}

	int32 ConfigWidth = 0;
	int32 ConfigHeight = 0;
	const bool bHasWidth = RuntimeConfig.GetInt(
		TEXT("Resolution"),
		TEXT("Width"),
		ConfigWidth);
	const bool bHasHeight = RuntimeConfig.GetInt(
		TEXT("Resolution"),
		TEXT("Height"),
		ConfigHeight);

	if (bHasWidth && bHasHeight &&
		ConfigWidth >= 320 && ConfigHeight >= 200 &&
		ConfigWidth <= 16384 && ConfigHeight <= 16384)
	{
		ApplyConfiguredResolution(ConfigWidth, ConfigHeight);
	}
	else if (bHasWidth || bHasHeight)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[Softwareconfig] 分辨率无效，Width=%d Height=%d"),
			ConfigWidth,
			ConfigHeight);
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[Softwareconfig] 已加载: Zoom=%.3f Move=%.3f Rotate=%.3f Resolution=%dx%d%s"),
		ZoomSpeed,
		MoveSpeed,
		RotateSpeed,
		ConfigWidth,
		ConfigHeight,
		bSpeedChanged ? TEXT(" [SpeedChanged]") : TEXT(""));
}

void ATwinSpectatorPawn::ApplyConfiguredResolution(
	const int32 Width,
	const int32 Height)
{
	const FIntPoint RequestedResolution(Width, Height);
	if (RequestedResolution == LastAppliedConfigResolution)
	{
		return;
	}

	if (!GEngine)
	{
		return;
	}

	UGameUserSettings* UserSettings = GEngine->GetGameUserSettings();
	if (!UserSettings)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[Softwareconfig] GameUserSettings 无效，无法应用分辨率 %dx%d"),
			Width,
			Height);
		return;
	}

	UserSettings->SetScreenResolution(RequestedResolution);
	UserSettings->ApplyResolutionSettings(false);
	LastAppliedConfigResolution = RequestedResolution;

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[Softwareconfig] 已请求切换分辨率: %dx%d"),
		Width,
		Height);
}

void ATwinSpectatorPawn::BeginPlay()
{
	Super::BeginPlay();

	// 蓝图默认值初始化完成后读取外部配置，保证 INI 拥有最终优先级。
	ReloadCameraConfig(true);

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		// 鸟瞰默认限制 Pitch
		PC->PlayerCameraManager->ViewPitchMin = -89.f;
		PC->PlayerCameraManager->ViewPitchMax = 0.f;

		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(MappingContext, 0);
		}
	}

	SetActorLocation(CameraStartLocation);

	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		PC->SetControlRotation(CameraStartRotate);
	}

	if (SpringArm)
	{
		SpringArm->TargetArmLength = SpringStartLength;
	}

	CameraLocation = CameraStartLocation;
	CameraRotate = CameraStartRotate;
	SpringLen = SpringStartLength;

	if (CameraCurve)
	{
		FOnTimelineFloat UpdateFunc;
		UpdateFunc.BindUFunction(this, FName("OnCameraTimelineUpdate"));

		FOnTimelineEvent FinishFunc;
		FinishFunc.BindUFunction(this, FName("OnCameraTimelineFinished"));

		CameraTimeline.AddInterpFloat(CameraCurve, UpdateFunc);
		CameraTimeline.SetTimelineFinishedFunc(FinishFunc);
		CameraTimeline.SetLooping(false);
	}
}

void ATwinSpectatorPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 实时配置只轮询时间戳；文件未变化时不会重复解析 INI。
	CameraConfigPollElapsed += DeltaTime;
	if (CameraConfigPollElapsed >= FMath::Max(CameraConfigPollInterval, 0.1f))
	{
		CameraConfigPollElapsed = 0.f;
		ReloadCameraConfig(false);
	}

	CameraTimeline.TickTimeline(DeltaTime);

	/*
	 * 脚本相机移动期间由 Timeline 独占 SpringArm，避免滚轮惯性每帧
	 * 抢写臂长。移动结束后再按照当前模式的范围响应滚轮。
	 */
	if (SpringArm && !CameraTimeline.IsPlaying())
	{
		ZoomVelocity = FMath::FInterpTo(
			ZoomVelocity,
			0.f,
			DeltaTime,
			Damping);

		const float NewLength =
			SpringArm->TargetArmLength -
			ZoomVelocity * DeltaTime;
		const float ClampedLength = FMath::Clamp(
			NewLength,
			GetActiveMinZoom(),
			GetActiveMaxZoom());

		SpringArm->TargetArmLength = ClampedLength;

		// 到达边界后清掉向外的惯性，反向滚轮可以立即生效。
		if (!FMath::IsNearlyEqual(NewLength, ClampedLength))
		{
			ZoomVelocity = 0.f;
		}
	}
	else
	{
		ZoomVelocity = 0.f;
	}

	// DeviceFocus 模式下 WASD 持续移动
	if (CameraControlMode == ECameraControlMode::DeviceFocus)
	{
		FVector MoveInput = WASDMoveInput;
		if (!MoveInput.IsNearlyZero())
		{
			FRotator MoveRotation = GetActorRotation();

			if (const AController* PawnController = GetController())
			{
				MoveRotation = PawnController->GetControlRotation();
			}

			MoveRotation.Pitch = 0.f;
			MoveRotation.Roll = 0.f;

			const FVector Forward =
				FRotationMatrix(MoveRotation).GetUnitAxis(EAxis::X);
			const FVector Right =
				FRotationMatrix(MoveRotation).GetUnitAxis(EAxis::Y);

			FVector Delta = (Forward * WASDMoveInput.X + Right * WASDMoveInput.Y)
				* WASDMoveSpeed * DeltaTime;

			SetActorLocation(GetActorLocation() + Delta);
		}
	}
}

// ===== 模式切换 =====
void ATwinSpectatorPawn::SetCameraControlMode(ECameraControlMode NewMode)
{
	bRightMouseDown = false;
	EndMousePointPan();

	CameraControlMode = NewMode;
	ZoomVelocity = 0.f;

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		switch (NewMode)
		{
		case ECameraControlMode::BirdsEye:
			// 鸟瞰：Pitch 限制 -89 ~ 0
			PC->PlayerCameraManager->ViewPitchMin = -89.f;
			PC->PlayerCameraManager->ViewPitchMax = 0.f;
			break;

		case ECameraControlMode::DeviceFocus:
			// 设备定位：Pitch 放开 -89 ~ 89
			PC->PlayerCameraManager->ViewPitchMin = -89.f;
			PC->PlayerCameraManager->ViewPitchMax = 89.f;
			break;
		}
	}

	// 切换时清空 WASD 输入，防止残留
	WASDMoveInput = FVector::ZeroVector;
}

bool ATwinSpectatorPawn::CanUseWASD() const
{
	return CameraControlMode == ECameraControlMode::DeviceFocus;
}

bool ATwinSpectatorPawn::CanRightMousePan() const
{
	return CameraControlMode == ECameraControlMode::BirdsEye;
}

bool ATwinSpectatorPawn::CanLeftMouseRotate() const
{
	// 两种模式都支持左键旋转，行为由 Pitch 限制区分
	return true;
}

// ===== 输入绑定 =====
void ATwinSpectatorPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	MappingContext = NewObject<UInputMappingContext>(this);

	IA_LeftMouse = NewObject<UInputAction>(this);
	IA_LeftMouse->ValueType = EInputActionValueType::Boolean;

	IA_RightMouse = NewObject<UInputAction>(this);
	IA_RightMouse->ValueType = EInputActionValueType::Boolean;

	IA_LookAndMouse = NewObject<UInputAction>(this);
	IA_LookAndMouse->ValueType = EInputActionValueType::Axis2D;

	IA_Zoom = NewObject<UInputAction>(this);
	IA_Zoom->ValueType = EInputActionValueType::Axis1D;

	IA_Touch = NewObject<UInputAction>(this);
	IA_Touch->ValueType = EInputActionValueType::Axis2D;
	MappingContext->MapKey(IA_Touch, EKeys::TouchKeys[0]);
	MappingContext->MapKey(IA_Touch, EKeys::TouchKeys[1]);
	MappingContext->MapKey(IA_Touch, EKeys::TouchKeys[2]);

	// WASD
	IA_Move = NewObject<UInputAction>(this);
	IA_Move->ValueType = EInputActionValueType::Axis2D;
	// W 前进 X=+1，布尔键默认值就是 X=1，不需要任何 Modifier
	MappingContext->MapKey(IA_Move, EKeys::W);
	// S 后退 X=-1，只需要 Negate
	{
		FEnhancedActionKeyMapping& M = MappingContext->MapKey(IA_Move, EKeys::S);
		UInputModifierNegate* Negate = NewObject<UInputModifierNegate>();
		M.Modifiers.Add(Negate);
	}
	// D 右移 → 把 X 轴换到 Y 轴：Swizzle YXZ
	{
		FEnhancedActionKeyMapping& M = MappingContext->MapKey(IA_Move, EKeys::D);
		UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>();
		Swizzle->Order = EInputAxisSwizzle::YXZ;
		M.Modifiers.Add(Swizzle);
	}
	// A 左移 → 先 Swizzle 换到 Y 轴，再 Negate 取反
	{
		FEnhancedActionKeyMapping& M = MappingContext->MapKey(IA_Move, EKeys::A);
		UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>();
		Swizzle->Order = EInputAxisSwizzle::YXZ;
		UInputModifierNegate* Negate = NewObject<UInputModifierNegate>();
		// 注意顺序：先 Swizzle 再 Negate
		M.Modifiers.Add(Swizzle);
		M.Modifiers.Add(Negate);
	}


	MappingContext->MapKey(IA_LookAndMouse, EKeys::Mouse2D);
	MappingContext->MapKey(IA_Zoom, EKeys::MouseWheelAxis);
	MappingContext->MapKey(IA_LeftMouse, EKeys::LeftMouseButton);
	MappingContext->MapKey(IA_RightMouse, EKeys::RightMouseButton);

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		EIC->BindAction(IA_LookAndMouse, ETriggerEvent::Triggered, this, &ATwinSpectatorPawn::MouseMove);
		EIC->BindAction(IA_Zoom, ETriggerEvent::Triggered, this, &ATwinSpectatorPawn::Zoom);

		EIC->BindAction(IA_LeftMouse, ETriggerEvent::Started, this, &ATwinSpectatorPawn::OnLeftDown);
		EIC->BindAction(IA_LeftMouse, ETriggerEvent::Completed, this, &ATwinSpectatorPawn::OnLeftUp);
		EIC->BindAction(IA_RightMouse, ETriggerEvent::Started, this, &ATwinSpectatorPawn::OnRightDown);
		EIC->BindAction(IA_RightMouse, ETriggerEvent::Completed, this, &ATwinSpectatorPawn::OnRightUp);

		EIC->BindAction(IA_Move, ETriggerEvent::Triggered, this, &ATwinSpectatorPawn::WASDMove);
		EIC->BindAction(IA_Move, ETriggerEvent::Completed, this, &ATwinSpectatorPawn::WASDMoveStop);
	}

	PlayerInputComponent->BindTouch(IE_Pressed, this, &ATwinSpectatorPawn::OnTouchPressed);
	PlayerInputComponent->BindTouch(IE_Released, this, &ATwinSpectatorPawn::OnTouchReleased);
	PlayerInputComponent->BindTouch(IE_Repeat, this, &ATwinSpectatorPawn::OnTouchMoved);
}

// ===== 鼠标按键 =====
void ATwinSpectatorPawn::OnLeftDown(const FInputActionValue&)
{
	if (!CanLeftMouseRotate()) return;
	bLeftMouseDown = true;
}

void ATwinSpectatorPawn::OnLeftUp(const FInputActionValue&)
{
	bLeftMouseDown = false;
}

void ATwinSpectatorPawn::OnRightDown(const FInputActionValue&)
{
	if (CameraControlMode == ECameraControlMode::DeviceFocus)
	{
		// 右键与 WBP_MainUI.WBP_DeviceFoucsBlackBtn 共用业务层返回流程。
		// Pawn 只发送请求，场景、拆解、UI 和相机由控制器统一恢复。
		OnDeviceFocusReturnRequested.Broadcast();
		return;
	}

	if (!CanRightMousePan() || CameraTimeline.IsPlaying())
	{
		return;
	}

	bRightMouseDown = true;
	bMousePanAnchorValid = CaptureMousePanAnchor();

	// 没点到有效场景时，本次不进入拖动。
	if (!bMousePanAnchorValid)
	{
		bRightMouseDown = false;
	}
}

void ATwinSpectatorPawn::OnRightUp(const FInputActionValue&)
{
	bRightMouseDown = false;
	EndMousePointPan();
}

// ===== 鼠标移动 =====
void ATwinSpectatorPawn::MouseMove(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();

	// 右键平移：仅鸟瞰
	if (bRightMouseDown && CanRightMousePan())
	{
		HandleMousePointPan(Axis);
	}
	// 左键旋转：两种模式都支持，Pitch 限制由 PlayerCameraManager 控制
	else if (bLeftMouseDown && CanLeftMouseRotate())
	{
		SetCameraRotate(Axis.X, true);
		SetCameraRotate(Axis.Y, false);
	}
}

void ATwinSpectatorPawn::SetCameraMove(float Val, bool bX)
{
	FRotator Rot = GetActorRotation();
	Rot.Pitch = 0.f;
	Rot.Roll = 0.f;

	FVector Right = FRotationMatrix(Rot).GetUnitAxis(EAxis::Y);
	FVector Forward = FRotationMatrix(Rot).GetUnitAxis(EAxis::X);
	FVector Dir = bX ? Right : Forward;

	// 平移速度随臂长线性缩放：臂长越短移动越慢，臂长越长移动越快
	float ArmLength = SpringArm ? SpringArm->TargetArmLength : MinZoom;
	float T = FMath::Clamp((ArmLength - MinZoom) / (MaxZoom - MinZoom), 0.f, 1.f);
	float ScaledSpeed = FMath::Lerp(MoveSpeed * MinMoveSpeedScale, MoveSpeed, T);

	SetActorLocation(GetActorLocation() + Dir * Val * ScaledSpeed);
}

void ATwinSpectatorPawn::SetCameraRotate(float Val, bool bX)
{
	if (FMath::IsNearlyZero(Val)) return;

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		FRotator Rot = PC->GetControlRotation();

		if (bX)
		{
			Rot.Yaw += Val * RotateSpeed;
		}
		else
		{
			Rot.Pitch += Val * RotateSpeed;

			// 鸟瞰模式手动 Clamp（PlayerCameraManager 也会 Clamp，双保险）
			if (CameraControlMode == ECameraControlMode::BirdsEye)
			{
				Rot.Pitch = FMath::Clamp(Rot.Pitch, -89.f, 0.f);
			}
			else
			{
				Rot.Pitch = FMath::Clamp(Rot.Pitch, -89.f, 89.f);
			}
		}

		PC->SetControlRotation(Rot);
	}
}

// ===== WASD =====
void ATwinSpectatorPawn::WASDMove(const FInputActionValue& Value)
{
	if (!CanUseWASD()) return;
	FVector2D Axis = Value.Get<FVector2D>();
	WASDMoveInput.X = Axis.X; // 前后
	WASDMoveInput.Y = Axis.Y; // 左右
}

void ATwinSpectatorPawn::WASDMoveStop(const FInputActionValue&)
{
	WASDMoveInput = FVector::ZeroVector;
}

// ===== 缩放 =====
void ATwinSpectatorPawn::Zoom(const FInputActionValue& Value)
{
	if (!SpringArm ||
		CameraTimeline.IsPlaying() ||
		bMousePanAnchorValid)
	{
		return;
	}

	const float ActiveZoomSpeed =
		CameraControlMode == ECameraControlMode::DeviceFocus
		? DeviceFocusZoomSpeed
		: ZoomSpeed;

	ZoomVelocity += Value.Get<float>() * ActiveZoomSpeed;
}

float ATwinSpectatorPawn::GetActiveMinZoom() const
{
	if (CameraControlMode == ECameraControlMode::DeviceFocus)
	{
		return FMath::Max(DeviceFocusMinZoom, 1.f);
	}

	return FMath::Max(MinZoom, 1.f);
}

float ATwinSpectatorPawn::GetActiveMaxZoom() const
{
	const float ActiveMinZoom = GetActiveMinZoom();

	if (CameraControlMode == ECameraControlMode::DeviceFocus)
	{
		return FMath::Max(DeviceFocusMaxZoom, ActiveMinZoom);
	}

	return FMath::Max(MaxZoom, ActiveMinZoom);
}

// ===== 触控 =====
void ATwinSpectatorPawn::OnTouchPressed(ETouchIndex::Type FingerIndex, FVector Location)
{
	TouchMap.Add(FingerIndex, Location);
}

void ATwinSpectatorPawn::OnTouchReleased(ETouchIndex::Type FingerIndex, FVector Location)
{
	TouchMap.Remove(FingerIndex);
	if (TouchMap.Num() < 2) LastPinchDistance = 0.f;
}

void ATwinSpectatorPawn::OnTouchMoved(ETouchIndex::Type FingerIndex, FVector Location)
{
	if (!TouchMap.Contains(FingerIndex)) return;

	FVector Last = TouchMap[FingerIndex];
	FVector Delta3D = Location - Last;
	TouchMap[FingerIndex] = Location;

	FVector2D Delta(Delta3D.X, Delta3D.Y);
	int32 FingerCount = TouchMap.Num();

	if (FingerCount == 1)
	{
		SetCameraRotate(Delta.X, true);
		SetCameraRotate(Delta.Y, false);
	}
	else if (FingerCount >= 2)
	{
		SetCameraMove(Delta.X, true);
		SetCameraMove(Delta.Y, false);

		auto It = TouchMap.CreateConstIterator();
		FVector P1 = It.Value(); ++It;
		FVector P2 = It.Value();

		float Dist = FVector::Dist(P1, P2);
		if (LastPinchDistance > 0.f)
		{
			ZoomVelocity += (Dist - LastPinchDistance) * 5.f;
		}
		LastPinchDistance = Dist;
	}
}

// ===== Camera 工具函数 =====
void ATwinSpectatorPawn::GetCameraLocation()
{
	CameraLocation = GetActorLocation();

	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (PC->GetPawn() == this)
		{
			CameraRotate = PC->GetControlRotation();
		}
		else
		{
			CameraRotate = this->GetViewRotation();
		}
	}


	if (SpringArm)
		SpringLen = SpringArm->TargetArmLength;
}

void ATwinSpectatorPawn::SetCameraNewLocation(
	float Time,
	FVector CameraNewLocation,
	FRotator CameraNewRotate,
	float SpringNewLength)
{
	SetActorLocation(CameraNewLocation);

	// 避免精确 ±90° 导致欧拉角奇异
	FRotator ViewRotation = CameraNewRotate.GetNormalized();
	ViewRotation.Pitch = FMath::Clamp(ViewRotation.Pitch, -89.f, 89.f);
	ViewRotation.Roll = 0.f;

	// Pawn 只保存水平朝向
	const FRotator PawnRotation(0.f, ViewRotation.Yaw, 0.f);
	SetActorRotation(PawnRotation, ETeleportType::TeleportPhysics);

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->SetControlRotation(ViewRotation);
	}

	if (SpringArm)
	{
		SpringArm->TargetArmLength = SpringNewLength;
	}
}

void ATwinSpectatorPawn::CameraMove(FVector CameraNewLocation, FRotator CameraNewRotate, float SpringNewLength)
{
	const float DefaultDuration = CameraTimeline.GetTimelineLength() > SMALL_NUMBER
		? CameraTimeline.GetTimelineLength()
		: 1.f;
	CameraMoveWithDuration(CameraNewLocation, CameraNewRotate, SpringNewLength, DefaultDuration);
}

void ATwinSpectatorPawn::CameraMoveWithDuration(
	FVector CameraNewLocation,
	FRotator CameraNewRotate,
	float SpringNewLength,
	float Duration)
{
	bRightMouseDown = false;
	EndMousePointPan();
	ZoomVelocity = 0.f;

	GetCameraLocation();

	TargetLocation = CameraNewLocation;
	TargetRotation = CameraNewRotate;
	TargetArmLength = FMath::Clamp(
		SpringNewLength,
		GetActiveMinZoom(),
		GetActiveMaxZoom());

	if (CameraCurve)
	{
		const float TimelineLength = CameraTimeline.GetTimelineLength();
		const float SafeDuration = FMath::Max(Duration, 0.01f);
		CameraTimeline.SetPlayRate(
			TimelineLength > SMALL_NUMBER
			? TimelineLength / SafeDuration
			: 1.f);
		CameraTimeline.PlayFromStart();
	}
	else
	{
		SetCameraNewLocation(
			1.f,
			TargetLocation,
			TargetRotation,
			TargetArmLength);
		OnCameraMoveFinished.Broadcast();
	}
}

void ATwinSpectatorPawn::SetSpringArmLength(
	const float NewArmLength,
	const float InterpTime)
{
	if (!SpringArm)
	{
		return;
	}

	ZoomVelocity = 0.f;
	GetCameraLocation();

	const float ClampedArmLength = FMath::Clamp(
		NewArmLength,
		GetActiveMinZoom(),
		GetActiveMaxZoom());

	if (InterpTime <= KINDA_SMALL_NUMBER)
	{
		CameraTimeline.Stop();
		SpringArm->TargetArmLength = ClampedArmLength;
		SpringLen = ClampedArmLength;
		TargetArmLength = ClampedArmLength;
		return;
	}

	CameraMoveWithDuration(
		CameraLocation,
		CameraRotate,
		ClampedArmLength,
		InterpTime);
}

void ATwinSpectatorPawn::SetDeviceFocusZoomRange(
	const float NewMinZoom,
	const float NewMaxZoom,
	const bool bClampCurrentArm)
{
	DeviceFocusMinZoom = FMath::Max(NewMinZoom, 1.f);
	DeviceFocusMaxZoom = FMath::Max(
		NewMaxZoom,
		DeviceFocusMinZoom);

	if (!bClampCurrentArm ||
		CameraControlMode != ECameraControlMode::DeviceFocus ||
		!SpringArm)
	{
		return;
	}

	ZoomVelocity = 0.f;
	TargetArmLength = FMath::Clamp(
		TargetArmLength,
		DeviceFocusMinZoom,
		DeviceFocusMaxZoom);
	SpringArm->TargetArmLength = FMath::Clamp(
		SpringArm->TargetArmLength,
		DeviceFocusMinZoom,
		DeviceFocusMaxZoom);
	SpringLen = SpringArm->TargetArmLength;
}

void ATwinSpectatorPawn::SetDeviceDismantleViewEnabled(
	const bool bEnabled)
{
	if (bEnabled)
	{
		if (!bHasSavedDeviceDismantleZoom)
		{
			GetCameraLocation();
			SavedPreDismantleArmLength = SpringLen;
			SavedPreDismantleMinZoom = DeviceFocusMinZoom;
			SavedPreDismantleMaxZoom = DeviceFocusMaxZoom;
			bHasSavedDeviceDismantleZoom = true;
		}

		SetDeviceFocusZoomRange(
			DeviceDismantleMinZoom,
			DeviceDismantleMaxZoom,
			false);
		SetSpringArmLength(
			DeviceDismantleArmLength,
			DeviceDismantleZoomInterpTime);
		return;
	}

	if (!bHasSavedDeviceDismantleZoom)
	{
		return;
	}

	const float RestoreArmLength =
		SavedPreDismantleArmLength;
	const float RestoreMinZoom =
		SavedPreDismantleMinZoom;
	const float RestoreMaxZoom =
		SavedPreDismantleMaxZoom;
	bHasSavedDeviceDismantleZoom = false;

	SetDeviceFocusZoomRange(
		RestoreMinZoom,
		RestoreMaxZoom,
		false);
	SetSpringArmLength(
		RestoreArmLength,
		DeviceDismantleZoomInterpTime);
}

void ATwinSpectatorPawn::FocusOnActor(
	AActor* TargetActor,
	float InterpTime,
	float ZoomScale,
	FRotator NewRotation)
{
	SetCameraControlMode(ECameraControlMode::BirdsEye);
	FocusOnActorInternal(TargetActor, InterpTime, ZoomScale, NewRotation);
}

void ATwinSpectatorPawn::FocusOnDevice(AActor* TargetDevice, float InterpTime, float ZoomScale)
{
	if (!IsValid(TargetDevice))
	{
		return;
	}

	if (!bHasSavedDeviceFocusView)
	{
		GetCameraLocation();
		SavedDeviceFocusLocation = CameraLocation;
		SavedDeviceFocusRotation = CameraRotate;
		SavedDeviceFocusArmLength = SpringLen;
		bHasSavedDeviceFocusView = true;
	}

	SetCameraControlMode(ECameraControlMode::DeviceFocus);

	// 当前项目设备聚焦轴使用设备局部空间；具体轴仍由全局配置决定，默认 Y+。
	// 在这里固定为局部空间，避免蓝图中遗留的 World 默认值覆盖新规则。
	DeviceFocusAxisSpace = EDeviceFocusAxisSpace::DeviceLocal;

	const FRotator FocusRotation = GetDeviceFocusRotation(TargetDevice);

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[DeviceFocus] Target=%s Location=%s Rotation=%s"),
		*GetNameSafe(TargetDevice),
		*TargetDevice->GetActorLocation().ToCompactString(),
		*FocusRotation.ToCompactString());

	FocusOnActorInternal(TargetDevice, InterpTime, ZoomScale, FocusRotation);
}

void ATwinSpectatorPawn::FocusOnDeviceImmediate(
	AActor* TargetDevice,
	float ZoomScale)
{
	// FocusOnActorInternal 会把非正时长识别为立即应用，同时仍复用完整的
	// 设备边界、观察轴、机械臂长度以及返回视角缓存逻辑。
	FocusOnDevice(TargetDevice, 0.f, ZoomScale);
}

void ATwinSpectatorPawn::RestoreDeviceFocusView(float InterpTime)
{
	if (!bHasSavedDeviceFocusView)
	{
		SetCameraControlMode(ECameraControlMode::BirdsEye);
		return;
	}

	const FVector RestoreLocation = SavedDeviceFocusLocation;
	const FRotator RestoreRotation = SavedDeviceFocusRotation;
	const float RestoreArmLength = SavedDeviceFocusArmLength;
	bHasSavedDeviceFocusView = false;

	SetCameraControlMode(ECameraControlMode::BirdsEye);
	CameraMoveWithDuration(
		RestoreLocation,
		RestoreRotation,
		RestoreArmLength,
		FMath::Max(InterpTime, 0.01f));
}

void ATwinSpectatorPawn::DiscardDeviceFocusView()
{
	bHasSavedDeviceFocusView = false;
	CameraTimeline.Stop();
	SetCameraControlMode(ECameraControlMode::BirdsEye);
}

void ATwinSpectatorPawn::FocusOnActorInternal(AActor* TargetActor, float InterpTime, float ZoomScale, FRotator NewRotation)
{
	if (!IsValid(TargetActor) || !Camera || !SpringArm)
	{
		return;
	}

	/*
	 * 同时兼容：
	 * 1. 楼栋 -> 楼层 -> 模型 Actor；
	 * 2. 容器 Actor -> StaticMeshActor；
	 * 3. 设备 Actor 自身直接持有无碰撞 MeshComponent。
	 */
	TArray<AActor*> FocusActors;
	FocusActors.Add(TargetActor);

	TArray<AActor*> AttachedActors;
	TargetActor->GetAttachedActors(AttachedActors, true, true);
	FocusActors.Append(AttachedActors);

	FBox BoundingBox(ForceInit);
	for (AActor* FocusActor : FocusActors)
	{
		if (!IsValid(FocusActor))
		{
			continue;
		}

		TArray<UPrimitiveComponent*> PrimitiveComponents;
		FocusActor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

		for (UPrimitiveComponent* Primitive : PrimitiveComponents)
		{
			if (!IsValid(Primitive) ||
				!Primitive->IsRegistered() ||
				Primitive->IsA<UWidgetComponent>())
			{
				continue;
			}

			// 直接读取组件 Bounds，不依赖碰撞是否开启。
			const FVector Origin = Primitive->Bounds.Origin;
			const FVector Extent = Primitive->Bounds.BoxExtent;
			if (Extent.ContainsNaN() ||
				Extent.IsNearlyZero(UE_KINDA_SMALL_NUMBER))
			{
				continue;
			}

			BoundingBox += FBox(Origin - Extent, Origin + Extent);
		}
	}

	if (!BoundingBox.IsValid)
	{
		// 纯空节点或尚未完成渲染注册时，以 Actor 位置建立安全聚焦范围。
		constexpr float SafeFocusExtent = 50.f;
		const FVector Center = TargetActor->GetActorLocation();
		BoundingBox = FBox(
			Center - FVector(SafeFocusExtent),
			Center + FVector(SafeFocusExtent));
	}

	const FVector Center = BoundingBox.GetCenter();

	// Camera 的 FieldOfView 是水平 FOV，同时计算垂直 FOV，避免窄屏裁切。
	const float HalfHorizontalFOV = FMath::DegreesToRadians(
		FMath::Clamp(Camera->FieldOfView, 5.f, 170.f) * 0.5f);
	const float AspectRatio = FMath::Max(Camera->AspectRatio, 0.1f);
	const float HalfVerticalFOV = FMath::Atan(
		FMath::Tan(HalfHorizontalFOV) / AspectRatio);
	const float LimitingHalfFOV = FMath::Min(
		HalfHorizontalFOV,
		HalfVerticalFOV);

	const float Radius = FMath::Max(
		BoundingBox.GetExtent().Size(),
		1.f);
	const float Distance = LimitingHalfFOV > SMALL_NUMBER
		? Radius / FMath::Tan(LimitingHalfFOV)
		: Radius * 2.f;
	const float SafeZoomScale = FMath::Max(ZoomScale, 0.01f);
	const float NewArmLength = FMath::Clamp(
		Distance * SafeZoomScale,
		GetActiveMinZoom(),
		GetActiveMaxZoom());

	if (InterpTime <= KINDA_SMALL_NUMBER)
	{
		// 专用设备过渡由 PlayerController 的 TransitionCamera 负责画面插值；
		// MainPawn 在画面背后直接准备到最终姿态，不能再启动第二条 Timeline。
		CameraTimeline.Stop();
		TargetLocation = Center;
		TargetRotation = NewRotation;
		TargetArmLength = NewArmLength;
		SetCameraNewLocation(
			1.f,
			TargetLocation,
			TargetRotation,
			TargetArmLength);
		OnCameraMoveFinished.Broadcast();
		return;
	}

	CameraMoveWithDuration(Center, NewRotation, NewArmLength, InterpTime);
}

bool ATwinSpectatorPawn::CaptureMousePanAnchor()
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !PC->PlayerCameraManager)
	{
		return false;
	}

	float MouseX = 0.f;
	float MouseY = 0.f;
	int32 ViewportWidth = 0;
	int32 ViewportHeight = 0;

	if (!PC->GetMousePosition(MouseX, MouseY))
	{
		return false;
	}

	PC->GetViewportSize(ViewportWidth, ViewportHeight);
	if (ViewportWidth <= 0 || ViewportHeight <= 0)
	{
		return false;
	}

	FVector RayOrigin;
	FVector RayDirection;
	if (!PC->DeprojectMousePositionToWorld(RayOrigin, RayDirection))
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	RayDirection.Normalize();
	const FVector RayEnd =
		RayOrigin + RayDirection * MousePointPanTraceDistance;

	FCollisionQueryParams QueryParams(
		SCENE_QUERY_STAT(MousePointPan),
		false);
	QueryParams.AddIgnoredActor(this);

	FHitResult Hit;
	if (World->LineTraceSingleByChannel(
		Hit,
		RayOrigin,
		RayEnd,
		MousePointPanTraceChannel,
		QueryParams))
	{
		MousePanAnchorWorld = Hit.ImpactPoint;
	}
	else
	{
		// 没有简单碰撞时，退回到 Pawn 所在高度的水平面。
		const float VerticalDirection = FVector::DotProduct(
			RayDirection,
			FVector::UpVector);
		if (FMath::Abs(VerticalDirection) < 0.001f)
		{
			return false;
		}

		const FVector FallbackPlanePoint(
			0.f,
			0.f,
			GetActorLocation().Z);
		const float RayDistance = FVector::DotProduct(
			FallbackPlanePoint - RayOrigin,
			FVector::UpVector) / VerticalDirection;
		if (RayDistance <= 0.f)
		{
			return false;
		}

		MousePanAnchorWorld = RayOrigin + RayDirection * RayDistance;
	}

	MousePanStartPawnLocation = GetActorLocation();
	MousePanStartCameraLocation =
		PC->PlayerCameraManager->GetCameraLocation();
	MousePanStartCameraRotation =
		PC->PlayerCameraManager->GetCameraRotation();
	MousePanStartFOV = FMath::Clamp(
		PC->PlayerCameraManager->GetFOVAngle(),
		5.f,
		170.f);
	MousePanStartViewportSize = FIntPoint(
		ViewportWidth,
		ViewportHeight);
	MousePanVirtualScreenPosition = FVector2D(MouseX, MouseY);

	if (!GetFrozenMousePointOnPanPlane(
		MousePanVirtualScreenPosition,
		MousePanStartWorldPoint))
	{
		return false;
	}

	ZoomVelocity = 0.f;

	return true;
}

void ATwinSpectatorPawn::EndMousePointPan()
{
	bMousePanAnchorValid = false;
}

bool ATwinSpectatorPawn::GetFrozenMousePointOnPanPlane(
	const FVector2D& ScreenPosition,
	FVector& OutWorldPoint) const
{
	if (MousePanStartViewportSize.X <= 0 ||
		MousePanStartViewportSize.Y <= 0)
	{
		return false;
	}

	const float ViewportWidth =
		static_cast<float>(MousePanStartViewportSize.X);
	const float ViewportHeight =
		static_cast<float>(MousePanStartViewportSize.Y);
	const float AspectRatio = ViewportWidth / ViewportHeight;

	const float NormalizedX =
		ScreenPosition.X / ViewportWidth * 2.f - 1.f;
	const float NormalizedY =
		1.f - ScreenPosition.Y / ViewportHeight * 2.f;

	const float HalfHorizontalFOV = FMath::DegreesToRadians(
		MousePanStartFOV * 0.5f);
	const float HalfVerticalFOV = FMath::Atan(
		FMath::Tan(HalfHorizontalFOV) / AspectRatio);

	const FVector LocalRayDirection(
		1.f,
		NormalizedX * FMath::Tan(HalfHorizontalFOV),
		NormalizedY * FMath::Tan(HalfVerticalFOV));
	const FVector WorldRayDirection = MousePanStartCameraRotation
		.RotateVector(LocalRayDirection)
		.GetSafeNormal();

	const float Denominator = FVector::DotProduct(
		WorldRayDirection,
		FVector::UpVector);
	if (FMath::Abs(Denominator) < 0.001f)
	{
		return false;
	}

	const float RayDistance = FVector::DotProduct(
		MousePanAnchorWorld - MousePanStartCameraLocation,
		FVector::UpVector) / Denominator;
	if (RayDistance <= 0.f)
	{
		return false;
	}

	OutWorldPoint = MousePanStartCameraLocation +
		WorldRayDirection * RayDistance;
	return true;
}

void ATwinSpectatorPawn::HandleMousePointPan(const FVector2D& MouseDelta)
{
	if (!bMousePanAnchorValid ||
		!CanRightMousePan() ||
		CameraTimeline.IsPlaying() || MouseDelta.IsNearlyZero())
	{
		return;
	}

	APlayerController* PC = Cast<APlayerController>(GetController());
	float MouseX = 0.f;
	float MouseY = 0.f;

	if (PC && PC->GetMousePosition(MouseX, MouseY))
	{
		// 与蓝图的 DeprojectMousePositionToWorld 一致，直接使用当前光标位置。
		MousePanVirtualScreenPosition = FVector2D(MouseX, MouseY);
	}
	else
	{
		// 光标被视口捕获时，使用输入增量维护等价的虚拟屏幕位置。
		MousePanVirtualScreenPosition.X += MouseDelta.X;
		MousePanVirtualScreenPosition.Y -= MouseDelta.Y;
	}

	FVector CurrentWorldPoint;
	if (!GetFrozenMousePointOnPanPlane(
		MousePanVirtualScreenPosition,
		CurrentWorldPoint))
	{
		return;
	}

	FVector TargetPawnLocation = MousePanStartPawnLocation +
		(MousePanStartWorldPoint - CurrentWorldPoint) * MousePointPanScale;
	TargetPawnLocation.Z = MousePanStartPawnLocation.Z;

	if (TargetPawnLocation.ContainsNaN())
	{
		return;
	}

	SetActorLocation(TargetPawnLocation, false);
}

FRotator ATwinSpectatorPawn::GetDeviceFocusRotation(
	AActor* TargetActor) const
{
	FVector FrontAxis = FVector::RightVector;

	switch (DeviceFocusAxis)
	{
	case EDeviceFocusAxis::PositiveX:
		FrontAxis = FVector::ForwardVector;
		break;

	case EDeviceFocusAxis::NegativeX:
		FrontAxis = -FVector::ForwardVector;
		break;

	case EDeviceFocusAxis::PositiveY:
		FrontAxis = FVector::RightVector;
		break;

	case EDeviceFocusAxis::NegativeY:
		FrontAxis = -FVector::RightVector;
		break;

	case EDeviceFocusAxis::PositiveZ:
		FrontAxis = FVector::UpVector;
		break;

	case EDeviceFocusAxis::NegativeZ:
		FrontAxis = -FVector::UpVector;
		break;

	default:
		break;
	}

	if (DeviceFocusAxisSpace == EDeviceFocusAxisSpace::DeviceLocal &&
		IsValid(TargetActor))
	{
		// 把设备局部正面轴转换成世界方向，兼容不同世界旋转的设备。
		FrontAxis = TargetActor->GetActorTransform()
			.TransformVectorNoScale(FrontAxis);
	}

	FrontAxis = FrontAxis.GetSafeNormal();
	if (FrontAxis.IsNearlyZero())
	{
		FrontAxis = FVector::RightVector;
	}

	/*
	 * 设备 Actor 的局部 Y+ 直接作为 Pawn Forward。
	 * 例如设备世界 Yaw=45°，局部 Y+ 转到世界空间后为 Yaw=135°。
	 */
	FRotator Result = FrontAxis.Rotation();
	Result.Roll = 0.f;
	return Result;
}

bool ATwinSpectatorPawn::IsCameraMoveRunning() const
{
	return CameraTimeline.IsPlaying();
}

void ATwinSpectatorPawn::ResetCameraLocation()
{
	SetCameraControlMode(ECameraControlMode::BirdsEye);
	CameraMove(CameraStartLocation, CameraStartRotate, SpringStartLength);
}

float ATwinSpectatorPawn::GetFOV()
{
	return Camera ? Camera->FieldOfView : 0.f;
}

void ATwinSpectatorPawn::SaveBuildingFocusView_Implementation()
{
	if (bHasSavedBuildingFocusView)
	{
		return;
	}

	GetCameraLocation();
	SavedBuildingFocusLocation = CameraLocation;
	SavedBuildingFocusRotation = CameraRotate;
	SavedBuildingFocusArmLength = SpringLen;
	bHasSavedBuildingFocusView = true;
}

void ATwinSpectatorPawn::FocusBuildingTarget_Implementation(
	const FBuildingFocusTarget& Target,
	const FBuildingFocusSettings& Settings)
{
	if (!Target.bIsValid || !Camera || !SpringArm)
	{
		return;
	}

	SetCameraControlMode(ECameraControlMode::BirdsEye);

	FVector LongAxis = Target.LongAxis.GetSafeNormal2D();
	FVector ShortAxis = Target.ShortAxis.GetSafeNormal2D();
	if (LongAxis.IsNearlyZero())
	{
		LongAxis = FVector::ForwardVector;
	}
	if (ShortAxis.IsNearlyZero())
	{
		ShortAxis = FVector::RightVector;
	}

	const FVector CurrentCameraLocation = Camera->GetComponentLocation();
	const FVector ToCurrentCamera = (CurrentCameraLocation - Target.Center).GetSafeNormal2D();

	// 保持在离当前镜头较近的一侧，切换楼栋时不绕场飞行。
	if (FVector::DotProduct(ShortAxis, ToCurrentCamera) < 0.f)
	{
		ShortAxis *= -1.f;
	}
	if (FVector::DotProduct(LongAxis, ToCurrentCamera) < 0.f)
	{
		LongAxis *= -1.f;
	}

	// 相机主要沿短轴观察长立面，同时加入少量长轴偏移显示纵深。
	const FVector FromCenter = (
		ShortAxis +
		LongAxis * FMath::Clamp(Settings.DiagonalViewRatio, 0.f, 1.f))
		.GetSafeNormal2D();
	const FVector HorizontalLookDirection = -FromCenter;
	const float FocusYaw = Settings.bUseAutoYaw
		? HorizontalLookDirection.Rotation().Yaw
		: Settings.Yaw;
	const FRotator FocusRotation(
		FMath::Clamp(Settings.Pitch, -89.f, 0.f),
		FocusYaw,
		FMath::Clamp(Settings.Roll, -180.f, 180.f));

	const FVector ViewForward = FocusRotation.Vector().GetSafeNormal();
	const FVector ViewRight = FRotationMatrix(FocusRotation)
		.GetUnitAxis(EAxis::Y);
	const FVector ViewUp = FRotationMatrix(FocusRotation)
		.GetUnitAxis(EAxis::Z);

	float HalfWidth = 0.f;
	float HalfHeight = 0.f;
	float HalfDepth = 0.f;
	for (int32 X = -1; X <= 1; X += 2)
	{
		for (int32 Y = -1; Y <= 1; Y += 2)
		{
			for (int32 Z = -1; Z <= 1; Z += 2)
			{
				const FVector RelativeCorner(
					Target.Extent.X * static_cast<float>(X),
					Target.Extent.Y * static_cast<float>(Y),
					Target.Extent.Z * static_cast<float>(Z));
				HalfWidth = FMath::Max(
					HalfWidth,
					FMath::Abs(FVector::DotProduct(RelativeCorner, ViewRight)));
				HalfHeight = FMath::Max(
					HalfHeight,
					FMath::Abs(FVector::DotProduct(RelativeCorner, ViewUp)));
				HalfDepth = FMath::Max(
					HalfDepth,
					FMath::Abs(FVector::DotProduct(RelativeCorner, ViewForward)));
			}
		}
	}

	const float HalfHorizontalFov = FMath::DegreesToRadians(
		FMath::Clamp(Camera->FieldOfView, 5.f, 170.f) * 0.5f);
	const float AspectRatio = FMath::Max(Camera->AspectRatio, 0.1f);
	const float HalfVerticalFov = FMath::Atan(
		FMath::Tan(HalfHorizontalFov) / AspectRatio);
	const float Padding = FMath::Max(Settings.Padding, 1.f);

	const float HorizontalDistance = HalfHorizontalFov > SMALL_NUMBER
		? (HalfWidth * Padding) / FMath::Tan(HalfHorizontalFov)
		: HalfWidth * Padding * 2.f;
	const float VerticalDistance = HalfVerticalFov > SMALL_NUMBER
		? (HalfHeight * Padding) / FMath::Tan(HalfVerticalFov)
		: HalfHeight * Padding * 2.f;

	const float ConfigMinDistance = FMath::Max(Settings.MinDistance, MinZoom);
	const float ConfigMaxDistance = Settings.MaxDistance > 0.f
		? FMath::Min(Settings.MaxDistance, MaxZoom)
		: MaxZoom;
	const float FocusDistance = FMath::Clamp(
		FMath::Max(HorizontalDistance, VerticalDistance) + HalfDepth,
		ConfigMinDistance,
		FMath::Max(ConfigMaxDistance, ConfigMinDistance));

	// 配置非零坐标时直接作为 Pawn 的最终世界坐标；Bounds 中心只在
	// 未配置坐标时作为自动机位，不再参与自定义坐标的换算。
	const FVector FinalFocusLocation = Settings.CameraLocation.IsNearlyZero()
		? Target.Center
		: Settings.CameraLocation;

	SavedBuildingFocusInterpTime = FMath::Max(Settings.InterpTime, 0.01f);
	CameraMoveWithDuration(
		FinalFocusLocation,
		FocusRotation,
		FocusDistance,
		SavedBuildingFocusInterpTime);
}

void ATwinSpectatorPawn::RestoreBuildingFocusView_Implementation()
{
	if (!bHasSavedBuildingFocusView)
	{
		return;
	}

	SetCameraControlMode(ECameraControlMode::BirdsEye);

	CameraMoveWithDuration(
		SavedBuildingFocusLocation,
		SavedBuildingFocusRotation,
		SavedBuildingFocusArmLength,
		SavedBuildingFocusInterpTime);
	bHasSavedBuildingFocusView = false;
}

void ATwinSpectatorPawn::ClearBuildingFocusView_Implementation()
{
	bHasSavedBuildingFocusView = false;
}

void ATwinSpectatorPawn::OnCameraTimelineUpdate(float Value)
{
	FVector   NewLocation = UKismetMathLibrary::VLerp(CameraLocation, TargetLocation, Value);
	FRotator  NewRotation = UKismetMathLibrary::RLerp(CameraRotate, TargetRotation, Value, true);
	float     NewArmLength = UKismetMathLibrary::Lerp(SpringLen, TargetArmLength, Value);
	SetCameraNewLocation(Value, NewLocation, NewRotation, NewArmLength);
}

void ATwinSpectatorPawn::OnCameraTimelineFinished()
{
	// 无论曲线末端值是否精确为 1，都把最终位置、旋转和臂长落到目标值。
	SetCameraNewLocation(
		1.f,
		TargetLocation,
		TargetRotation,
		TargetArmLength);

	if (CameraControlMode == ECameraControlMode::DeviceFocus)
	{
		const AController* PawnController = GetController();
		const FRotator ControlRotation = PawnController
			? PawnController->GetControlRotation()
			: FRotator::ZeroRotator;

		UE_LOG(
			LogTemp,
			Log,
			TEXT("[DeviceFocus] Finished PawnLocation=%s PawnRotation=%s ControlRotation=%s"),
			*GetActorLocation().ToCompactString(),
			*GetActorRotation().ToCompactString(),
			*ControlRotation.ToCompactString());
	}

	OnCameraMoveFinished.Broadcast();
}
