// Copyright 2023 LandscapeCombinator. All Rights Reserved.

#include "Foliage/PCGOGRFilter.h"

#include "PCGContext.h"
#include "PCGCustomVersion.h"
#include "PCGParamData.h"
#include "PCGElement.h"
#include "Elements/PCGSurfaceSampler.h"
#include "PCGPin.h"
#include "Helpers/PCGAsync.h"
#include "Data/PCGPointData.h"
#include "Data/PCGSpatialData.h"
#include "Metadata/Accessors/IPCGAttributeAccessorTpl.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/Accessors/PCGCustomAccessor.h"

#include "Utils/Logging.h"
#include "Utils/Overpass.h"
#include "Utils/Download.h"
#include "Utils/Time.h"
#include "GlobalSettings.h"


#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGOGRFilter)

#define LOCTEXT_NAMESPACE "FLandscapeCombinatorModule"

using namespace GlobalSettings;


TArray<FPCGPinProperties> UPCGOGRFilterSettings::InputPinProperties() const {

	TArray<FPCGPinProperties> Properties;
	Properties.Emplace(PCGPinConstants::DefaultInputLabel, EPCGDataType::Point);
	Properties.Emplace(TEXT("BoundingShape"), EPCGDataType::Spatial, /*bAllowMultipleConnections=*/false, /*bAllowMultipleData=*/false,
		LOCTEXT("OGRFilterSurfacePinTooltip", "Please connect the landscape here")
	);
	return Properties;
}

FPCGElementPtr UPCGOGRFilterSettings::CreateElement() const
{
	return MakeShared<FPCGOGRFilterElement>();
}

OGRGeometry* UPCGOGRFilterSettings::GetGeometryFromQuery(FString Query) const
{
	FString IntermediateDir = FPaths::ConvertRelativePathToFull(FPaths::EngineIntermediateDir());
	FString LandscapeCombinatorDir = FPaths::Combine(IntermediateDir, "LandscapeCombinator");
	FString DownloadDir = FPaths::Combine(LandscapeCombinatorDir, "Download");
	FString XmlFilePath = FPaths::Combine(DownloadDir,
		FString::Format(TEXT("overpass_query_{0}.xml"),
		{
			FTextLocalizationResource::HashString(Query)
		})
	);

	Download::SynchronousFromURL(Query, XmlFilePath);
	return GetGeometryFromPath(XmlFilePath);

}

OGRGeometry* UPCGOGRFilterSettings::GetGeometryFromShortQuery(FBox Bounds, FString ShortQuery) const
{
	UE_LOG(LogLandscapeCombinator, Log, TEXT("Resimulating foliage with short query: '%s'"), *ShortQuery);

	FVector Origin = Bounds.GetCenter();
	FVector BoxExtent = Bounds.GetExtent();
	//GetActorBounds(false, Origin, BoxExtent, true);
	
	WorldParametersV1 GlobalParams;
	if (!GetWorldParameters(GlobalParams)) return nullptr;
	
	double WorldWidthCm  = ((double) GlobalParams.WorldWidthKm) * 1000 * 100;
	double WorldHeightCm = ((double) GlobalParams.WorldHeightKm) * 1000 * 100;
	double WorldOriginX = GlobalParams.WorldOriginX;
	double WorldOriginY = GlobalParams.WorldOriginY;
	
	double South = UnrealCoordinatesToEPSG326Y(Origin.Y + BoxExtent.Y, WorldWidthCm, WorldHeightCm, WorldOriginX, WorldOriginY);
	double North = UnrealCoordinatesToEPSG326Y(Origin.Y - BoxExtent.Y, WorldWidthCm, WorldHeightCm, WorldOriginX, WorldOriginY);
	double West = UnrealCoordinatesToEPSG326X(Origin.X - BoxExtent.X, WorldWidthCm, WorldHeightCm, WorldOriginX, WorldOriginY);
	double East = UnrealCoordinatesToEPSG326X(Origin.X + BoxExtent.X, WorldWidthCm, WorldHeightCm, WorldOriginX, WorldOriginY);

	FString OverpassQuery = Overpass::QueryFromShortQuery(South, West, North, East, ShortQuery);
	return GetGeometryFromQuery(OverpassQuery);
}

OGRGeometry* UPCGOGRFilterSettings::GetGeometryFromPath(FString Path) const
{
	GDALDataset* Dataset = (GDALDataset*) GDALOpenEx(TCHAR_TO_UTF8(*Path), GDAL_OF_VECTOR, NULL, NULL, NULL);

	if (!Dataset)
	{
		return nullptr;
	}

	UE_LOG(LogLandscapeCombinator, Log, TEXT("Got a valid dataset from OSM data, continuing..."));


	OGRGeometry *UnionGeometry = OGRGeometryFactory::createGeometry(OGRwkbGeometryType::wkbMultiPolygon);
	if (!UnionGeometry)
	{
		UE_LOG(LogLandscapeCombinator, Error, TEXT("Internal error while creating OGR Geometry. Please try again."));
		return nullptr;
	}
		
	int n = Dataset->GetLayerCount();
	for (int i = 0; i < n; i++)
	{
		OGRLayer* Layer = Dataset->GetLayer(i);

		if (!Layer) continue;

		for (auto& Feature : Layer)
		{
			if (!Feature) continue;
			OGRGeometry* Geometry = Feature->GetGeometryRef();
			if (!Geometry) continue;

			OGRGeometry* NewUnion = UnionGeometry->Union(Geometry);
			if (NewUnion)
			{
				UnionGeometry = NewUnion;
			}
			else
			{
				UE_LOG(LogLandscapeCombinator, Warning, TEXT("There was an error while taking union of geometries in OGR, we'll still try to filter"))
			}
		}

	}

	return UnionGeometry;
}

OGRGeometry* UPCGOGRFilterSettings::GetGeometry(FBox Bounds) const
{
	
	if (FoliageSourceType == EFoliageSourceType::LocalVectorFile)
	{
		return GetGeometryFromPath(OSMPath);
	}
	else if (FoliageSourceType == EFoliageSourceType::OverpassShortQuery)
	{
		return GetGeometryFromShortQuery(Bounds, OverpassShortQuery);
	}
	else if (FoliageSourceType == EFoliageSourceType::Forests)
	{
		return GetGeometryFromShortQuery(Bounds, "nwr[\"landuse\"=\"forest\"];nwr[\"natural\"=\"wood\"];");
	}
	else
	{
		check(false);
		return nullptr;
	}
}

// adapted from Unreal Engine 5.2 PCGDensityFilter.cpp
bool FPCGOGRFilterElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGOGRFilterElement::Execute);

	const UPCGOGRFilterSettings* Settings = Context->GetInputSettings<UPCGOGRFilterSettings>();
	check(Settings);

	TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputs();
	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;
	
	TArray<FPCGTaggedData> BoundingShapeInputs = Context->InputData.GetInputsByPin(TEXT("BoundingShape"));
	const UPCGSpatialData* BoundingShapeSpatialInput = nullptr;

	if (BoundingShapeInputs.Num() == 0)
	{
		PCGE_LOG_C(Error, GraphAndLog, Context, LOCTEXT("BoundingShapeInput", "Please connect the landscape to the bounding shape"));
		return true;
	}

	ensure(BoundingShapeInputs.Num() == 1);
	
	FBox Bounds = Cast<UPCGSpatialData>(BoundingShapeInputs[0].Data)->GetBounds();

	OGRGeometry *Geometry = Settings->GetGeometry(Bounds);

	if (!Geometry)
	{
		PCGE_LOG_C(Error, GraphAndLog, Context, LOCTEXT("NoGeometry", "Unable to get OGR Geometry. Please check the output log"));
		return true;
	}

	WorldParametersV1 GlobalParams;
	if (!GetWorldParameters(GlobalParams)) return true;
	
	double WorldWidthCm  = ((double) GlobalParams.WorldWidthKm) * 1000 * 100;
	double WorldHeightCm = ((double) GlobalParams.WorldHeightKm) * 1000 * 100;
	double WorldOriginX = GlobalParams.WorldOriginX;
	double WorldOriginY = GlobalParams.WorldOriginY;


	for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel))
	{
		FPCGTaggedData& Output = Outputs.Add_GetRef(Input);

		if (!Input.Data || Cast<UPCGSpatialData>(Input.Data) == nullptr)
		{
			PCGE_LOG_C(Error, GraphAndLog, Context, LOCTEXT("InvalidInputData", "Invalid input data"));
			continue;
		}

		const UPCGPointData* OriginalData = Cast<UPCGSpatialData>(Input.Data)->ToPointData(Context);

		if (!OriginalData)
		{
			PCGE_LOG_C(Error, GraphAndLog, Context, LOCTEXT("NoData", "Unable to get point data from input"));
			continue;
		}

		const TArray<FPCGPoint>& PCGPoints = OriginalData->GetPoints();
		
		UPCGPointData* FilteredData = NewObject<UPCGPointData>();
		FilteredData->InitializeFromData(OriginalData);
		TArray<FPCGPoint>& FilteredPoints = FilteredData->GetMutablePoints();

		Output.Data = FilteredData;

		TMap<const FVector, FVector2D> PCGPointToPoint;
		TSet<FVector2D> InsideLocations;
		OGRMultiPoint *AllPoints = (OGRMultiPoint*) OGRGeometryFactory::createGeometry(OGRwkbGeometryType::wkbMultiPoint);

		TIME("MultiPointProcessing",
			for (const FPCGPoint& PCGPoint : PCGPoints) {
				const FVector& Location = PCGPoint.Transform.GetLocation();
				FVector2D Coordinates4326 = GlobalSettings::UnrealCoordinatesToEPSG326(Location, WorldWidthCm, WorldHeightCm, WorldOriginX, WorldOriginY);
				OGRPoint Point4326(Coordinates4326[0], Coordinates4326[1]);

				PCGPointToPoint.Add(Location, Coordinates4326);

				AllPoints->addGeometry(&Point4326);
			}

			for (auto& Point : (OGRMultiPoint *) AllPoints->Intersection(Geometry))
			{
				InsideLocations.Add(FVector2D(Point->getX(), Point->getY()));
			}
		
			FPCGAsync::AsyncPointProcessing(Context, PCGPoints.Num(), FilteredPoints,
				[InsideLocations, PCGPointToPoint, PCGPoints](int32 Index, FPCGPoint &OutPoint)
				{
					const FPCGPoint& PCGPoint = PCGPoints[Index];
					const FVector& Location = PCGPoint.Transform.GetLocation();
					if (InsideLocations.Contains(PCGPointToPoint[Location]))
					{
						OutPoint = PCGPoint;
						return true;
					}
					else
					{
						return false;
					}
				}
			);

		);

		PCGE_LOG_C(Verbose, LogOnly, Context, FText::Format(
			LOCTEXT("GenerateReport", "Generated {0} points out of {1} source points"),
			FilteredPoints.Num(),
			PCGPoints.Num()
		));
		UE_LOG(LogLandscapeCombinator, Log, TEXT("Generated %d filtered points out of %d source points"),
			FilteredPoints.Num(),
			PCGPoints.Num());
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
