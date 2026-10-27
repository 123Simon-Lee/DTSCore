# 通行对象池

只按人物/车辆 Pawn 蓝图类管理对象池。已移除 Lane 的场景 PassageActor 引用，不再克隆场景模板。DeviceBase、BP_Door、RevolvingDoorComponent 的开关与方向实现不变。

## 场景配置

每条 Lane 配置 ChannelId、DeviceCodes、SubjectType、Passage Class、GateActor 和内外路径点。

- 人行选择 BP_PassagePersonActor，车辆选择 BP_PassageCarActor；相同类共用池，不同通道租用独立实例。
- 模型、动画和 TriggerBoxExtent / TriggerBoxOffset 配置在人物/车辆蓝图默认值中。以前只在场景模板上改过的设置，需要同步到蓝图。
- GateActor 直接引用原 BP_Door / BP_BarrierGate。门片、曲线、PassArrow 和感应框仍由原蓝图配置。
- 移除旧字段不会自动改写保存的关卡：原 Lane 必须选择 Passage Class。场景里已有的人物/车辆未被删除，确认没有其他用途后可自行移除。

## 沿用原自动门

通行人/车是轻量 APawn，禁用自动控制、AIController、Actor Tick，不引入 CharacterMovement。通行组件创建并缓存一个普通 Box 查询框；只在通行时开启 QueryOnly，停止/回收时关闭；模型不阻挡场景。

TS 首次通行前把 Passage Class 登记到 GateActor 原有 AllowList，之后缓存门组件和类。TS 不修改门源码，不直接调用 Open/Close，不等待门时间轴；原门仍按“Actor 位置到触发框中心”的方向开门，离开自动关门。

查询框为 Pawn 对象类型，对 WorldDynamic 开启 Overlap；原门感应框应已经开启查询和 GenerateOverlapEvents，并能与 Pawn 重叠。TS 只检查和提示，不覆盖门碰撞设置。

路线应实际穿过感应框，两路径点放在门两侧，感应距离要足够提前开门。不同通道并行，同通道上一个人/车完成再处理下一条；池满排队。没有业务 Tick 轮询或逐消息全场景查找。

## 门配置问题仍需在原蓝图处理

增加查询不能修复空旋转数据。若出现 From=0 To=0，需要在原门蓝图初始化门片/栏杆的旋转数据。当前 BP_BarrierGate 是栏杆自身，没有子 Actor，不能仅枚举附加子 Actor 来初始化它。

上一版门组件内的自动初始化、栏杆抬起、特殊对象识别、多对象占用计数全部撤回。车辆开闸方向由原车闸蓝图/部件配置决定，通行系统不代算。旧门原本“一次离开即关门”的多对象行为也不在本次改动中变更。

## 容量

Config/DefaultGame.ini：

```ini
[/Script/PassageSystem.PassageSubsystem]
InitialPoolSize=2
MaxPoolSize=32
```

按蓝图类分别计数。仅预热/扩容 Spawn，正常结束回收；空闲关闭移动/骨骼/Actor Tick 与查询碰撞。最后一个通道注销或 World 结束才销毁整池。2/32 是起始配置，不是实测最佳值。

## 验证与生效

```text
npm run typecheck
npm run build
node --test Tests/passage-pool.test.cjs
```

原生测试：

- GZNY.Passage.ExistingDoorDirectionAndRecycle
- GZNY.Passage.ExistingDoorWhitelist
- GZNY.Passage.CarExistingDoorCompatibility

测试使用已配置门片、曲线和白名单的原门组件，不代表所有场景门配置均已验证。删除了原生属性并修改继承，必须保存并关闭 UE，完整编译后重开，不能仅 Live Coding。
