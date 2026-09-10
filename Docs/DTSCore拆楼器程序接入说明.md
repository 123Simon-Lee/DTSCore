# DTSCore 拆楼器程序接入说明

## 1. 文档范围

本文说明如何在 Unreal Engine 5.1 项目中接入当前版本的 DTSCore 拆楼器，包括：

- 使用现有 `Space / SpaceStruct` 场景结构接入；
- 创建并配置 `ABuildingManager`；
- 创建 `FBuildingFloorConfig` 数据表；
- 从 C++、蓝图和 TypeScript 调用拆楼功能；
- 在不依赖 Space 的项目中实现新的数据源 Adapter；
- 常见问题与性能注意事项。

本文对应当前工程中的以下模块：

| 模块 | 职责 | 是否依赖 Space |
| --- | --- | --- |
| `DTSCore` | 拆楼 RuntimeData、Manager、Component、动画、显隐和交互 | 否 |
| `DTSCoreSpaceAdapter` | 把 `ASpace / ASpaceStruct` 转换成通用拆楼数据 | 是 |
| `SpaceSystemUnit` | 维护现有 Space 层级、Mark、Code 和 Struct 数据 | 是 |

核心原则是：`DTSCore` 只处理 `AActor + ID + RuntimeNode`，具体场景结构由 Adapter 转换。

---

## 2. 运行结构

```text
场景数据源
  ASpace / ActorTag / DataAsset / Datasmith Metadata
                    │
                    ▼
          IBuildingSourceAdapter
                    │
                    ▼
          FBuildingRuntimeData
                    │
          ┌─────────┴─────────┐
          ▼                   ▼
 ABuildingManager   UBuildingFloorComponent
          │                   │
          ├─ SceneScope       ├─ Drawer 分层/抽屉动画
          ├─ 点击/Hover       ├─ Visibility 楼层显隐
          ├─ 楼栋切换         ├─ 节点查询/高亮
          └─ 聚焦时机编排     └─ 聚焦 Bounds/长短轴计算
                    │
                    ▼
       IBuildingCameraFocusInterface
                    │
                    ▼
          ATwinSpectatorPawn
```

主要类型：

- `FBuildingSourceRecord`：Adapter 发现的一栋楼，只描述 ID、显示名称和源对象；
- `UBuildingRuntimeNode`：通用运行时节点，可表示楼栋、楼层或其他层级；
- `FBuildingRuntimeData`：一栋楼完整的运行时数据；
- `ABuildingManager`：负责发现、初始化、输入、状态切换和对外 API；
- `UBuildingFloorComponent`：负责具体拆楼动画、显隐、查询和高亮；
- `FSceneScope`：记录当前楼栋、楼层、组件和拆楼模式。
- `FBuildingFocusTarget`：与 Space 无关的楼栋 Bounds、长轴和短轴；
- `IBuildingCameraFocusInterface`：拆楼器与具体 Pawn/CameraActor 之间的相机接口。

---

## 3. 插件和模块接入

### 3.1 使用现有 Space 结构

项目需要启用：

```json
{
  "Name": "DTSCoreSpaceAdapter",
  "Enabled": true
}
```

`DTSCoreSpaceAdapter` 会自动声明对以下插件的依赖：

- `DTSCore`，其中同时包含 `DTSCore` 与 `SpaceSystemUnit` 两个运行时模块。

当前项目已经在 `YongZhouGuoHua.uproject` 中启用该插件。

如果项目 C++ 模块需要直接调用拆楼器，在项目的 `.Build.cs` 中加入：

```csharp
PublicDependencyModuleNames.AddRange(new string[]
{
    "DTSCore"
});
```

如果项目自己的公开头文件继承 `ASpace` 或 `ASpaceStruct`，还需要加入：

```csharp
PublicDependencyModuleNames.Add("SpaceSystemUnit");
```

业务模块通常不需要直接依赖 `DTSCoreSpaceAdapter` 模块。该模块在启动时注册默认 Adapter。

### 3.2 不使用 Space 结构

只依赖 `DTSCore`，不要启用 `DTSCoreSpaceAdapter`。然后通过以下任一方式提供数据：

1. 实现 `IBuildingSourceAdapter`，并调用 `SetBuildingSourceAdapter()`；
2. 由独立插件调用 `SetDefaultBuildingSourceAdapterFactory()` 注册默认 Adapter；
3. 直接构建 `FBuildingRuntimeData`，通过 `RegisterBuildingRuntimeData()` 注册。

---

## 4. 现有 Space 场景接入要求

使用 `DTSCoreSpaceAdapter` 时，不需要修改 `ASpace` 和 `ASpaceStruct` 的内部逻辑，但场景数据必须满足以下约定。

### 4.1 推荐场景层级

```text
园区或其他上级节点
└─ ABuilding                 Code=A1，Marks 包含 Building
   ├─ ABuildingFloor        Code=B1F，Marks 包含 BuildingFloor
   │  ├─ ASpaceStruct       设备、摄像头、管线等
   │  └─ 普通模型 Actor
   ├─ ABuildingFloor        Code=1F，Marks 包含 BuildingFloor
   └─ ABuildingFloor        Code=2F，Marks 包含 BuildingFloor
```

Space 的父子关系仍由 Actor 附加关系和 `InitializeSpaceData()` 维护。

### 4.2 楼栋要求

- 楼栋必须是 `ASpace` 或其子类；
- `Marks` 必须包含 Manager 的 `BuildingMark`，当前默认值是 `Building`；
- `Code` 应在场景中唯一，例如 `A1`、`SW`；
- `Code` 会成为 `FBuildingRuntimeData.BuildingId`；
- 数据表优先使用 `BuildingId` 查找同名 Row。

当前项目的 `ABuilding` 构造函数已经自动添加 `Building` Mark。

### 4.3 楼层要求

- 楼层必须位于楼栋的 Space 子层级中；
- `Marks` 必须包含 Manager 的 `FloorMark`；
- 当前 `ABuildingFloor` 自动添加的 Mark 是 `BuildingFloor`；
- Manager 当前默认 `FloorMark` 也是 `BuildingFloor`；
- 楼层 `Code` 推荐使用 `1F`、`2F`、`B1F`、`RF` 等稳定值。

如果使用旧的 `BP_BuildingManager` 蓝图实例，请检查它是否保存了旧值 `FloorMark=Floor`。如果是，应改为 `BuildingFloor`，或点击属性旁的“重置为默认值”。

### 4.4 SpaceStruct 要求

Adapter 会把 `Space->GetStructs()` 返回的 `ASpaceStruct` 加入对应 RuntimeNode 的 `ControlledActors`。

这些 Actor 会参与：

- 节点显隐；
- 点击命中反向查询；
- 楼层相关设备、摄像头和点位的统一控制。

不需要为了拆楼器修改 `ASpaceStruct` 的继承结构或注册逻辑。

### 4.5 Space 数据刷新

`ASpace::BeginPlay()` 当前会执行 `Init()`，其中调用 `InitializeSpaceData()` 并注册到 `USpaceSubsystem`。`ABuildingManager` 会延迟到下一帧初始化，因此正常静态场景通常保持：

```text
bRefreshSourceDataOnBeginPlay = false
```

以下情况可以设为 `true`：

- BeginPlay 前后动态改变了附加关系；
- Space 缓存可能尚未更新；
- 运行时重新组织了楼栋、楼层或 Struct。

---

## 5. 创建拆楼配置表

### 5.1 创建 DataTable

在内容浏览器中创建 Data Table，行结构选择：

```text
FBuildingFloorConfig
```

当前项目已有配置资产：

```text
/Game/YZGH/DT/BuildingCompoentConfig
```

### 5.2 RowName 匹配规则

Manager 查找配置的顺序为：

1. 使用 `BuildingId` 查找同名 Row；
2. 如果未找到且 `bUseDefaultConfigRow=true`，查找 `DefaultConfigRowName`；
3. 两者都不存在时，该楼栋不会初始化进 `BuildingMap`。

推荐配置：

```text
RowName=A1       A1 楼专属配置
RowName=SW       室外层专属配置
RowName=Default  其他楼栋共用配置
```

`RowName` 不会按 DisplayName 自动匹配。

### 5.3 FBuildingFloorConfig 字段

| 字段 | 说明 | 常用值 |
| --- | --- | --- |
| `bCanDisassemble` | 整栋楼是否允许拆楼 | `true` |
| `DisassembleMode` | 拆楼方式 | `Drawer` 或 `Visibility` |
| `Disassemble` | 排除 Mark、Code 或楼层 | 按项目需要 |
| `FloorIndex` | 楼层编号解析规则 | 一般使用 `Auto` |
| `Component` | 自定义拆楼组件类 | 留空使用默认组件 |
| `Animation` | Timeline 曲线 | 留空使用 DTSCore 默认曲线 |
| `Focus` | 进入、退出拆楼时的相机聚焦参数 | 一般使用默认值 |
| `Drawer` | 分层和抽屉参数 | Drawer 模式使用 |
| `Visibility` | 显隐模式参数 | Visibility 模式使用 |
| `Name` | TH、WQ 等名称识别规则 | 按模型命名设置 |

即使 `bCanDisassemble=false`，Manager 仍可能为该楼栋创建组件并加入 `BuildingMap`，以便 Visibility 模式正确隐藏或恢复其他楼栋；但该楼栋自身不能执行拆楼。

### 5.4 Drawer 配置

| 字段 | 默认值 | 说明 |
| --- | ---: | --- |
| `PullDirection` | `Auto` | 抽屉拉出方向 |
| `PullDistanceScale` | `1.5` | 抽屉距离倍率 |
| `SpreadScale` | `1.2` | 楼层分层间距倍率 |
| `CampusMaxBuildingHeight` | `0` | 分层整体离地高度，0 表示自动计算 |
| `MaxFloorSizeX` | `0` | 手动覆盖楼层最大 X 尺寸 |
| `MaxFloorSizeY` | `0` | 手动覆盖楼层最大 Y 尺寸 |
| `FloorPullScaleOverride` | 空 | 指定楼层的拉出倍率覆盖 |

### 5.5 Visibility 配置

| 字段 | 默认值 | 说明 |
| --- | ---: | --- |
| `bHideOtherBuildingsOnEnter` | `true` | 进入模式时隐藏其他楼栋 |
| `bShowAllFloorsOnEnter` | `true` | 进入模式时先显示当前楼全部楼层 |
| `bRestoreOtherBuildingsOnExit` | `true` | 退出时恢复其他楼栋 |
| `FloorDisplayMode` | `ShowCurrentAndBelow` | 点击楼层后的显示范围 |

`FloorDisplayMode` 支持：

- `ShowCurrentAndBelow`：显示当前楼层及以下；
- `ShowOnlyCurrent`：只显示当前楼层；
- `ShowCurrentAndAbove`：显示当前楼层及以上。

### 5.6 Name 配置

`Name.THRule` 和 `Name.WQRule` 用于在每个楼层的 Runtime 子树与楼层附加 Actor 中识别天花和外墙。组件只在初始化时建立缓存，不会在 Tick 或每次点击时遍历 Actor。

默认规则：

```text
THRule = EndsWith("TH")
WQRule = EndsWith("WQ")
bHideCurrentFloorCeiling = true
```

Datasmith 模型经常在语义名称后追加数字编号。匹配前会自动忽略最后一个纯数字段，因此：

```text
C1_1F_TH_001  -> C1_1F_TH -> 命中 THRule
CK1_WQ_145    -> CK1_WQ   -> 命中 WQRule
```

选择具体楼层时，Visibility 会在完成楼层显隐后隐藏当前层 TH；Drawer 会在抽出当前层时隐藏 TH，切换或收回楼层时恢复上一层 TH。退出拆楼和 Reset 时会恢复全部天花。WQ Actor 当前只缓存和提供查询，不自动隐藏。

C++ 和蓝图可以调用：

```cpp
Component->GetFloorCeilingActors(FloorIndex);
Component->GetFloorWallActors(FloorIndex);
Component->SetFloorCeilingHidden(FloorIndex, true);
```

### 5.7 Focus 配置

Drawer 和 Visibility 两种模式共用同一套聚焦配置。聚焦由 DTSCore 自动触发，TS 不需要再调用 `CameraMove` 或 `FocusOnActor`。

| 字段 | 默认值 | 说明 |
| --- | ---: | --- |
| `bAutoFocusOnEnter` | `true` | 进入或切换当前拆楼楼栋时自动聚焦 |
| `bFocusAfterLayering` | `true` | Drawer 等分层完成后，再按展开后的 Bounds 聚焦 |
| `bAutoFocusFloorOnSelect` | `true` | 选择楼层后将镜头从整栋收紧到该楼层 |
| `bRefocusBuildingOnFloorClose` | `true` | Drawer 收回当前楼层后重新聚焦整栋楼 |
| `bRestoreViewOnExit` | `true` | 真正退出拆楼时恢复进入前的园区视角 |
| `Padding` | `1.3` | 楼栋在画面边缘的留白倍率 |
| `Pitch` | `-32` | 相机俯角 |
| `DiagonalViewRatio` | `0.25` | 沿长轴增加的斜视比例，避免完全正投影 |
| `InterpTime` | `0.8` | 聚焦和恢复的移动时间（秒） |
| `MinDistance` | `1000` | 最小 SpringArm 长度 |
| `MaxDistance` | `60000` | 最大 SpringArm 长度 |

视角方向的规则是：相机主要位于楼栋短轴一侧，朝向楼栋中心，因此画面面对的是长立面；同时根据当前相机位置自动选择较近的一侧，并沿长轴加入少量斜角。距离按水平/垂直 FOV 与目标 Bounds 自适应计算，不使用固定楼栋坐标。

完整时序如下：

1. UI 选择 Visibility 楼栋：显示当前楼栋、隐藏其他楼栋，然后聚焦整栋；选择具体楼层后立即按该楼层 Bounds 再聚焦。
2. UI 选择 Drawer 楼栋：当前楼栋分层并抬起，其他楼栋保持不动；分层完成后聚焦整栋。选择具体楼层后，等待楼层抽出动画完成，再按移动后的楼层 Bounds 聚焦。
3. Drawer 收回当前楼层：楼层动画完成后重新聚焦整栋。
4. 切换楼栋：不恢复园区视角；真正退出拆楼时才恢复最初保存的园区视角。

### 5.8 排除拆楼节点

`FBuildingDisassembleSettings` 提供三种排除方式：

```text
DisableDisassembleMark = NoDisassemble
DisabledSpaceCodes     = { SW, RoofDevice }
DisabledFloors         = { -1, 9999 }
```

命中任意一项时，对应 RuntimeNode 的 `bCanDisassemble` 会变为 `false`。

### 5.9 楼层编号解析

自动解析模式依次尝试：

1. `Space.Code`；
2. `Space.DisplayName`；
3. Actor 名称。

当前自动解析结果：

| 输入 | FloorIndex |
| --- | ---: |
| `1F` | `1` |
| `2F` | `2` |
| `B1F` | `-1` |
| `B2F` | `-2` |
| `RF` / `ROOF` | `9999` |

非标准名称使用 `FloorIndexMap` 显式配置，例如：

```text
ResolveMode = CodeMapThenAuto
FloorIndexMap:
  FSSJQ -> -1
  RF    -> 9999
```

`BUILDING_INVALID_FLOOR_INDEX` 的值为 `-999999`，解析失败的节点不会作为可拆楼层加入 `FloorMap`。

---

## 6. 配置 BuildingManager

### 6.1 放置 Manager

在关卡中放置 `ABuildingManager` 或其蓝图子类。当前项目已有：

```text
/Game/Game/BluePrint/BP_BuildingManager
```

至少设置：

```text
BuildingFloorConfigTable = /Game/YZGH/DT/BuildingCompoentConfig
BuildingMark             = Building
FloorMark                = BuildingFloor
```

### 6.2 常用属性

| 属性 | 默认值 | 说明 |
| --- | ---: | --- |
| `BuildingMark` | `Building` | Adapter 识别楼栋的标签 |
| `FloorMark` | `BuildingFloor` | Adapter 识别楼层的标签 |
| `bRefreshSourceDataOnBeginPlay` | `false` | 初始化前刷新数据源 |
| `bUseDefaultConfigRow` | `true` | 允许使用默认配置行 |
| `DefaultConfigRowName` | `Default` | 默认配置行名称 |
| `bEnableClick` | `true` | 启用点击拆楼 |
| `bEnableHover` | `true` | 启用 Hover 高亮 |
| `TraceLength` | `100000` | 鼠标射线长度 |
| `TraceChannel` | `Visibility` | 鼠标射线通道 |
| `CameraFocusProvider` | 空 | 可选相机实现；空时自动使用本地玩家 Pawn 上的接口实现 |

### 6.3 自动初始化

`ABuildingManager::BeginPlay()` 会在下一帧自动调用 `InitializeBuildings()`。一般不需要业务代码再次初始化。

只有以下情况需要手动调用：

- 运行时新增或删除楼栋；
- 运行时调整 Space 层级；
- 替换 DataTable；
- 替换 Adapter。

重复初始化会销毁之前由 Manager 动态创建的拆楼组件，并重建 `BuildingMap`、SceneScope 和命中缓存。

### 6.4 碰撞要求

点击和 Hover 依赖 Manager 的射线检测。需要保证：

- 可点击模型具有 Collision；
- 模型阻挡 `TraceChannel`；
- 当前 PlayerController 能取得鼠标位置；
- UI 没有在不需要时持续拦截点击；
- 如果关闭 `bEnableClick` 或 `bEnableHover`，对应交互不会执行。

---

## 7. C++ 程序调用

### 7.1 按 BuildingId 调用

推荐业务层优先使用 ID API，不依赖具体 Actor 类型：

```cpp
#include "Core/Manager/BuildingManager.h"

void UMyBuildingService::OpenBuilding(
    ABuildingManager* Manager,
    const FString& BuildingId)
{
    if (!IsValid(Manager))
    {
        return;
    }

    if (!Manager->LayeringBuildingByCode(BuildingId))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("无法进入拆楼：%s"), *BuildingId);
    }
}
```

切换楼层：

```cpp
Manager->ToggleBuildingFloorByCode(TEXT("A1"), 3);
Manager->ToggleCurrentBuildingFloor(2);
```

退出当前拆楼：

```cpp
Manager->BackCurrentBuildingToNormal();
```

### 7.2 按 Actor 调用

```cpp
AActor* BuildingActor =
    Manager->FindBuildingActorByCode(TEXT("A1"));

if (IsValid(BuildingActor))
{
    Manager->LayeringBuilding(BuildingActor);
    Manager->ToggleBuildingFloor(BuildingActor, 3);
    Manager->BackBuildingToNormal(BuildingActor);
}
```

### 7.3 获取组件和查询楼层

```cpp
UBuildingFloorComponent* Component =
    Manager->GetBuildingComponent(BuildingActor);

if (Component)
{
    UBuildingRuntimeNode* FloorNode =
        Component->FindFloorWithIndex(3);

    const bool bLayered = Component->IsLayering();
    const bool bAnimating =
        Component->IsLayeringRunning() ||
        Component->IsExpandRunning();
}
```

业务层通常应通过 Manager 调用拆楼。只有查询、绑定组件事件或实现特殊 UI 时才直接访问 `UBuildingFloorComponent`。

### 7.4 获取当前状态

```cpp
const FSceneScope& Scope = Manager->GetSceneScope();

FString BuildingId = Scope.NowBuildingId;
FString FloorId = Scope.NowFloorId;
int32 FloorIndex = Scope.NowFloorIndex;
AActor* Building = Scope.NowBuilding;
AActor* Floor = Scope.NowFloor;
```

不要把 `NowBuilding` 或 `NowFloor` 强制转换成 `ASpace` 来取得 Code。业务标识应读取 `NowBuildingId` 和 `NowFloorId`。

### 7.5 事件绑定

Manager 提供：

- `OnBuildingNodeClicked`；
- `OnBuildingNodeHovered`。

组件提供：

- `OnLayeringTimeLineFinished`；
- `OnFloorExpandTimelineFinished`；
- `OnFloorVisibilityChanged`。

动态委托示例：

```cpp
UFUNCTION()
void HandleBuildingNodeClicked(
    UBuildingRuntimeNode* Node,
    int32 FloorIndex);

Manager->OnBuildingNodeClicked.AddDynamic(
    this,
    &UMyBuildingService::HandleBuildingNodeClicked);
```

对象销毁或 UI 关闭时应解除不再需要的绑定，避免同一回调被重复添加。

---

## 8. 蓝图调用

蓝图中推荐使用以下节点：

| 目的 | 节点 |
| --- | --- |
| 按 ID 展开楼栋 | `Layering Building By Code` |
| 按 ID 切换楼层 | `Toggle Building Floor By Code` |
| 当前楼栋切换楼层 | `Toggle Current Building Floor` |
| 当前楼栋归位 | `Back Current Building To Normal` |
| 查找楼栋 Actor | `Find Building Actor By Code` |
| 获取当前状态 | `Get Scene Scope` |
| 获取当前楼栋 | `Get Now Building` |
| 获取当前楼层 | `Get Now Floor` |

推荐用 `NowBuildingId` 和 `NowFloorId` 驱动 UI 选中状态。

---

## 9. TypeScript / Puerts 接入

### 9.1 按 ID 调用

```ts
const manager = this.BuildManager;
if (!manager) return;

manager.LayeringBuildingByCode("A1");
manager.ToggleBuildingFloorByCode("A1", 3);
manager.BackBuildingByCode("A1");
```

### 9.2 使用 Actor 调用

```ts
const buildingActor = manager.FindBuildingActorByCode("A1");

if (buildingActor) {
  manager.LayeringBuilding(buildingActor);
  manager.ToggleBuildingFloor(buildingActor, 3);
}
```

### 9.3 读取 SceneScope

```ts
const scope = manager.GetSceneScope();

const buildingId = scope.NowBuildingId;
const floorId = scope.NowFloorId;
const floorIndex = scope.NowFloorIndex;
const buildingActor = scope.NowBuilding;
const floorActor = scope.NowFloor;
```

TS 业务代码不要使用：

```ts
scope.NowBuilding.Code
scope.NowFloor.Code
buildingActor as UE.Space
```

这些写法会重新把 UI 和 Space 强耦合。应使用 `NowBuildingId`、`NowFloorId` 或 `component.GetBuildingId()`。

TS 也不应为拆楼流程调用 `MainPawn.CameraMove()`、`FocusOnActor()`，或监听动画完成事件来移动相机。`manager.LayeringBuilding()` 负责整栋聚焦，`manager.ToggleCurrentBuildingFloor()` 负责楼层聚焦，`BackCurrentBuildingToNormal()` 负责恢复视角。

### 9.4 遍历 BuildingMap

```ts
for (const [actor, component] of manager.BuildingMap) {
  if (!actor || !component) continue;

  const buildingId = component.GetBuildingId();
  const actorName = UE.KismetSystemLibrary.GetDisplayName(actor);
}
```

`BuildingMap` 的 Key 是 `UE.Actor`，不是 `UE.Space`。

### 9.5 绑定点击事件

```ts
const handler = (
  node: $Nullable<UE.BuildingRuntimeNode>,
  floorIndex: number,
) => {
  if (!node) return;

  console.log(
    `点击节点=${node.NodeId}, 楼层=${floorIndex}`,
  );
};

manager.OnBuildingNodeClicked.Add(handler);

// UI 销毁或解绑阶段
manager.OnBuildingNodeClicked.Remove(handler);
```

修改 C++ UPROPERTY 或 UFUNCTION 后，应重新生成或同步 `Typing/ue/ue.d.ts`，再执行：

```powershell
npx tsc --noEmit
npx tsc
```

---

## 10. SceneScope 状态规则

`FSceneScope` 是拆楼器的唯一活动状态来源。

| 字段 | 含义 |
| --- | --- |
| `NowBuilding` | 当前楼栋 Actor |
| `NowBuildingId` | 当前稳定楼栋 ID |
| `NowFloor` | 当前楼层 Actor |
| `NowFloorId` | 当前稳定楼层 ID |
| `NowFloorIndex` | 当前楼层编号 |
| `NowComponent` | 当前楼栋拆楼组件 |
| `NowFloorNode` | 当前楼层 RuntimeNode |
| `NowMode` | `None`、`Drawer` 或 `Visibility` |

状态使用规则：

- 进入一栋新楼时，Manager 会先退出旧楼状态；
- Drawer 再次点击当前楼层会收回，并清空当前楼层状态；
- Visibility 模式会根据配置隐藏或恢复其他楼栋；
- 业务 UI 应在操作后重新读取 SceneScope，不要自行推测拆楼状态；
- 动画运行中组件可能拒绝某些切换操作，因此不能只根据按钮点击更新 UI。

---

## 11. 自定义非 Space Adapter

### 11.1 实现接口

自定义 Adapter 需要实现：

```cpp
class FActorTagBuildingAdapter final : public IBuildingSourceAdapter
{
public:
    virtual void DiscoverBuildings(
        UWorld* World,
        const FBuildingSourceSettings& Settings,
        TArray<FBuildingSourceRecord>& OutBuildings) override;

    virtual bool BuildRuntimeData(
        const FBuildingSourceRecord& Source,
        UObject* NodeOuter,
        const FBuildingSourceSettings& Settings,
        const FBuildingFloorConfig& Config,
        FBuildingRuntimeData& OutRuntime) override;
};
```

`DiscoverBuildings()` 只负责发现楼栋并输出：

```cpp
FBuildingSourceRecord& Record =
    OutBuildings.AddDefaulted_GetRef();

Record.BuildingId = TEXT("A1");
Record.DisplayName = TEXT("A1 楼");
Record.BuildingActor = BuildingActor;
Record.SourceObject = MetadataAsset; // 可选
```

`BuildRuntimeData()` 负责创建完整树：

```cpp
OutRuntime = FBuildingRuntimeData();
OutRuntime.BuildingId = Source.BuildingId;
OutRuntime.BuildingActor = Source.BuildingActor.Get();
OutRuntime.Config = Config;

OutRuntime.Root =
    NewObject<UBuildingRuntimeNode>(NodeOuter);

OutRuntime.Root->NodeId = Source.BuildingId;
OutRuntime.Root->RuntimeActor = Source.BuildingActor.Get();

UBuildingRuntimeNode* FloorNode =
    NewObject<UBuildingRuntimeNode>(NodeOuter);

FloorNode->NodeId = TEXT("1F");
FloorNode->RuntimeActor = FloorActor;
FloorNode->Parent = OutRuntime.Root;
FloorNode->bIsFloor = true;
FloorNode->bCanDisassemble = Config.bCanDisassemble;
FloorNode->FloorIndex = 1;

OutRuntime.Root->Children.Add(FloorNode);
return true;
```

### 11.2 RuntimeNode 必填建议

每个可操作节点至少应设置：

- `NodeId`；
- `RuntimeActor`；
- `Parent`；
- `Children`；
- `bIsFloor`；
- `bCanDisassemble`；
- `FloorIndex`。

可选内容：

- `SourceObject`：追溯原始数据；
- `ControlledActors`：随节点显隐的设备或模型；
- `Tags`：通用标签；
- `Bounds`：可预先计算，组件初始化时也会更新；
- `OriginalTransform`：组件初始化时会以 Actor 当前 Transform 为准重新记录。

所有 RuntimeNode 必须使用传入的 `NodeOuter` 创建，不能用栈对象，也不要使用临时 UObject 作为 Outer。

### 11.3 设置到单个 Manager

在 `InitializeBuildings()` 之前调用：

```cpp
Manager->SetBuildingSourceAdapter(
    MakeUnique<FActorTagBuildingAdapter>());
```

### 11.4 注册为默认 Adapter

独立模块启动时：

```cpp
void FMyBuildingAdapterModule::StartupModule()
{
    SetDefaultBuildingSourceAdapterFactory([]()
    {
        return MakeUnique<FActorTagBuildingAdapter>();
    });
}
```

模块关闭时解除注册：

```cpp
void FMyBuildingAdapterModule::ShutdownModule()
{
    SetDefaultBuildingSourceAdapterFactory(
        FBuildingSourceAdapterFactory());
}
```

同一时刻只应有一个默认工厂。后注册的工厂会覆盖之前的工厂。

### 11.5 直接注册 RuntimeData

如果业务系统已经拥有完整建筑树，可以跳过发现阶段：

```cpp
FBuildingRuntimeData RuntimeData;
// 填充 BuildingId、BuildingActor、Config、Root……

const bool bRegistered =
    Manager->RegisterBuildingRuntimeData(RuntimeData);
```

该接口当前是 C++ 接口，不是 BlueprintCallable。

---

## 12. 生命周期和性能

### 12.1 初始化阶段

初始化时会执行：

1. Adapter 发现楼栋；
2. 根据 BuildingId 查找配置；
3. 构建 RuntimeNode 树；
4. 动态创建 `UBuildingFloorComponent`；
5. 构建 FloorMap、ActorMap 和 Manager 命中反向索引；
6. 计算楼层尺寸和 Transform；
7. 初始化 Timeline。

不要在每帧调用 `InitializeBuildings()`。

### 12.2 Tick

- `UBuildingFloorComponent` 只在 Timeline 播放期间启用 Tick；
- 动画结束和 Reset 后会关闭组件 Tick；
- Manager 在 Hover、Click、Debug 全部关闭时会提前退出 Tick；
- 楼层排序只在缓存初始化时执行一次；
- 命中查询使用 Actor 反向索引，不遍历全部楼栋。
- 聚焦 Bounds 只在进入或切换拆楼楼栋时计算，不在 Tick 中计算；
- Bounds 只遍历当前 RuntimeData 的楼层与 `ControlledActors`，不会扫描整个 World，也不会要求 Space/SpaceStruct 增加相机逻辑。

### 12.3 大场景建议

- 不需要鼠标交互时关闭 `bEnableHover` 和 `bEnableClick`；
- 为楼层使用合适的简单碰撞；
- `ControlledActors` 只加入确实需要跟随控制的 Actor；
- 避免在运行时频繁重建 Space 层级；
- 大量楼栋共用配置时使用 `Default` Row；
- 自定义 Adapter 的 Discover 阶段应使用 Subsystem、缓存或 Tag 查询，避免每帧全场景遍历。

---

## 13. 常见问题

### 13.1 Manager 初始化数量为 0

检查：

1. `DTSCoreSpaceAdapter` 是否启用；
2. 楼栋是否为 `ASpace`；
3. 楼栋 Marks 是否包含 `BuildingMark`；
4. `USpaceSubsystem` 是否已注册数据；
5. 必要时开启 `bRefreshSourceDataOnBeginPlay`；
6. Output Log 是否出现 Adapter 或配置相关警告。

### 13.2 找到楼栋但没有加入 BuildingMap

检查：

- `BuildingFloorConfigTable` 是否赋值；
- 是否存在与 BuildingId 同名的 Row；
- 是否存在 `Default` Row；
- `bUseDefaultConfigRow` 是否开启；
- BuildingActor 和 BuildingId 是否有效。

### 13.3 有楼栋但找不到楼层

检查：

- `FloorMark` 是否为 `BuildingFloor`；
- 楼层 Marks 是否包含同名 Mark；
- 楼层是否位于楼栋的 `GetSubspaces()` 结果中；
- 楼层 Code 是否能解析；
- FloorIndex 是否为 `-999999`；
- 是否被 `DisabledSpaceCodes`、`DisabledFloors` 或 `NoDisassemble` 排除。

### 13.4 点击没有反应

检查：

- `bEnableClick=true`；
- 模型是否阻挡 Manager 的 `TraceChannel`；
- 楼层或 ControlledActor 是否进入 ActorMap；
- 当前楼栋 `bCanDisassemble=true`；
- 点击的楼层节点 `bCanDisassemble=true`；
- Drawer 动画是否仍在运行。

### 13.5 Hover 没有高亮

检查：

- `bEnableHover=true`；
- PrimitiveComponent 是否允许 Custom Depth；
- 项目渲染设置是否启用 Custom Depth-Stencil Pass；
- 射线是否命中正确 Actor。

### 13.6 动画不播放

检查：

- DataTable 的 `Animation.Curve` 是否有效；
- 如果未指定，自带资产 `/DTSCore/CameraTimeline.CameraTimeline` 是否存在；
- Output Log 是否出现 `Curve is not Vaild.`；
- 组件是否正确 Initialize；
- FloorMap 是否至少包含一个有效楼层。

### 13.7 TS 报 Actor 没有 Code

这是预期行为。拆楼器公开类型已经从 `UE.Space` 改为 `UE.Actor`。

使用：

```ts
scope.NowBuildingId
scope.NowFloorId
component.GetBuildingId()
node.NodeId
```

不要读取 `actor.Code`。

### 13.8 进入拆楼后相机没有聚焦

检查：

- DataTable Row 的 `Focus.bAutoFocusOnEnter` 是否开启；
- 当前本地玩家 Pawn 是否为 `ATwinSpectatorPawn`，或是否实现 `IBuildingCameraFocusInterface`；
- 使用自定义相机时，Manager 的 `CameraFocusProvider` 是否已指定；
- RuntimeNode 的楼层或 `ControlledActors` 是否具有有效 Bounds；
- `MinDistance`、`MaxDistance` 是否符合当前场景尺度。

---

## 14. 最小接入检查表

- [ ] 启用 `DTSCore`；
- [ ] 使用 Space 时启用 `DTSCoreSpaceAdapter`；
- [ ] 项目 Build.cs 添加 `DTSCore`；
- [ ] 场景中存在 BuildingManager；
- [ ] Manager 已绑定 `FBuildingFloorConfig` DataTable；
- [ ] DataTable 有 BuildingId 同名 Row 或 `Default` Row；
- [ ] 楼栋 Mark 为 `Building`；
- [ ] 楼层 Mark 为 `BuildingFloor`；
- [ ] Building Code 唯一；
- [ ] 楼层 Code 可解析或已配置 FloorIndexMap；
- [ ] Space 父子附加关系正确；
- [ ] 点击模型阻挡 TraceChannel；
- [ ] Focus 配置已开启，Pawn 实现相机聚焦接口或 Manager 已指定 Provider；
- [ ] C++ 编译通过；
- [ ] 使用 TS 时 `npx tsc --noEmit` 通过。

---

## 15. 相关源码

```text
Plugins/DTSCore/Source/DTSCore/Public/Core/Adapter/BuildingSourceAdapter.h
Plugins/DTSCore/Source/DTSCore/Public/Core/Runtime/BuildingRuntimeData.h
Plugins/DTSCore/Source/DTSCore/Public/Core/DataTable/BuildingFloorConfig.h
Plugins/DTSCore/Source/DTSCore/Public/Core/Manager/BuildingManager.h
Plugins/DTSCore/Source/DTSCore/Public/Core/Component/BuildingFloorComponent.h
Plugins/DTSCore/Source/DTSCore/Public/Core/Camera/BuildingCameraFocusInterface.h
Plugins/DTSCore/Source/DTSCore/Public/Core/Game/TwinSpectatorPawn.h
Plugins/DTSCoreSpaceAdapter/Source/DTSCoreSpaceAdapter/Private/DTSCoreSpaceAdapter.cpp
```

当前工程参考资产：

```text
Content/Game/BluePrint/BP_BuildingManager.uasset
Content/YZGH/DT/BuildingCompoentConfig.uasset
Content/YZGH/Core/BP_Building.uasset
Content/YZGH/Core/BP_BuildingFloor.uasset
```
