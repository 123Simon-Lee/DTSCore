#include "Core/Adapter/BuildingSourceAdapter.h"

namespace
{
    FBuildingSourceAdapterFactory GDefaultBuildingSourceAdapterFactory;
}

void SetDefaultBuildingSourceAdapterFactory(FBuildingSourceAdapterFactory InFactory)
{
    GDefaultBuildingSourceAdapterFactory = MoveTemp(InFactory);
}

TUniquePtr<IBuildingSourceAdapter> CreateDefaultBuildingSourceAdapter()
{
    return GDefaultBuildingSourceAdapterFactory
        ? GDefaultBuildingSourceAdapterFactory()
        : nullptr;
}
