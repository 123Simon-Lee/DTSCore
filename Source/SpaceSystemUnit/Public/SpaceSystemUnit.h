#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/** 导出日志类别，允许其他模块使用。 */
SPACESYSTEMUNIT_API DECLARE_LOG_CATEGORY_EXTERN(
	LogSpaceSystem,
	Log,
	All
);

class SPACESYSTEMUNIT_API FSpaceSystemUnitModule
	: public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};