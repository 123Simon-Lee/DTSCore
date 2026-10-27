// Measurement 模块入口。
//
// 每个模块必须有且仅有一个 IMPLEMENT_MODULE：
// 它负责导出 InitializeModule / ShutdownModule 等
// 模块管理器所需的入口函数。缺失时 DLL 虽能被
// Windows 加载，但 UE 会报
// "module 'Measurement' could not be initialized successfully"。
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, Measurement)
