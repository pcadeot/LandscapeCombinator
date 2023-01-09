#pragma once

#include "HMInterfaceTiles.h"

#define LOCTEXT_NAMESPACE "FLandscapeCombinatorModule"

class HMViewFinder1or3 : public HMInterfaceTiles
{
protected:
	bool Initialize() override;
	int TileToX(FString Tile) const override;
	int TileToY(FString Tile) const override;

	bool GetSpatialReference(OGRSpatialReference &InRs) const override;

public:
	HMViewFinder1or3(FString LandscapeName0, const FText &KindText0, FString Descr0, int Precision0);

};

#undef LOCTEXT_NAMESPACE