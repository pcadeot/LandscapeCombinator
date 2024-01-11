// Copyright 2023 LandscapeCombinator. All Rights Reserved.

#include "ImageDownloader/Downloaders/HMXYZ.h"
#include "ImageDownloader/Directories.h"
#include "ImageDownloader/LogImageDownloader.h"

#include "ConsoleHelpers/Console.h"
#include "ConcurrencyHelpers/Concurrency.h"
#include "FileDownloader/Download.h"
#include "GDALInterface/GDALInterface.h"
#include "MapboxHelpers/MapboxHelpers.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "HAL/FileManagerGeneric.h"
#include "Logging/StructuredLog.h"

#define LOCTEXT_NAMESPACE "FImageDownloaderModule"

void HMXYZ::Fetch(FString InputCRS, TArray<FString> InputFiles, TFunction<void(bool)> OnComplete)
{
	FString XYZFolder = FPaths::Combine(Directories::ImageDownloaderDir(), Name + "-XYZ");

	if (!IPlatformFile::GetPlatformPhysical().DeleteDirectoryRecursively(*XYZFolder) || !IPlatformFile::GetPlatformPhysical().CreateDirectory(*XYZFolder))
	{
		Directories::CouldNotInitializeDirectory(XYZFolder);
		if (OnComplete) OnComplete(false);
		return;
	}

	if (!bGeoreferenceSlippyTiles && CRS.IsEmpty())
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("HMXYZ::Fetch::CRS", "Please provide a valid CRS for your XYZ tiles.")
		);
		if (OnComplete) OnComplete(false);
		return;
	}
	
	if (bGeoreferenceSlippyTiles)
	{
		OutputCRS = "EPSG:3857";
	}
	else
	{
		OutputCRS = CRS;
	}
	
	if (MinX > MaxX || MinY > MaxY)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
			LOCTEXT("HMXYZ::Fetch::Tiles", "For XYZ tiles, MinX ({0}) must be <= than MaxX ({1}), and MinY ({2}) must be <= MaxY ({3})."),
			FText::AsNumber(MinX),
			FText::AsNumber(MaxX),
			FText::AsNumber(MinY),
			FText::AsNumber(MaxY)
		));
		if (OnComplete) OnComplete(false);
		return;
	}

	int NumTiles = (MaxX - MinX + 1) * (MaxY - MinY + 1);

	if (NumTiles > 16)
	{
		EAppReturnType::Type UserResponse = FMessageDialog::Open(EAppMsgType::OkCancel,
			FText::Format(
				LOCTEXT(
					"HMXYZ::Fetch::ManyTiles",
					"Your parameters require downloading and processing {0} tiles.\nPress OK if you want to continue, or Cancel."
				),
				FText::AsNumber(NumTiles)
			)
		);
		if (UserResponse == EAppReturnType::Cancel)
		{
			if (OnComplete) OnComplete(false);
			return;
		}
	}
	
	if (Format.Contains("."))
	{
		if (!Console::ExecProcess(TEXT("7z"), TEXT(""), false, false))
		{
			FMessageDialog::Open(EAppMsgType::Ok,
				LOCTEXT(
					"MissingRequirement",
					"Please make sure 7z is installed on your computer and available in your PATH if you want to use a compressed format."
				)
			);

			if (OnComplete) OnComplete(false);
			return;
		}
	}

	bool *bShowedDialog = new bool(false);

	FScopedSlowTask *Task = new FScopedSlowTask(NumTiles,
		FText::Format(
			LOCTEXT("HMXYZ::Fetch::Task", "Downloading and Georeferencing {0} Tiles"),
			FText::AsNumber(NumTiles)
		)
	);
	Task->MakeDialog();

	Concurrency::RunMany(
		NumTiles,

		[this, Task, XYZFolder, bShowedDialog](int i, TFunction<void(bool)> OnCompleteElement)
		{
			int X = i % (MaxX - MinX + 1) + MinX;
			int Y = i / (MaxX - MinX + 1) + MinY;

			FString ReplacedURL =
				URL.Replace(TEXT("{z}"), *FString::FromInt(Zoom))
				   .Replace(TEXT("{x}"), *FString::FromInt(X))
				   .Replace(TEXT("{y}"), *FString::FromInt(Y));
			FString DownloadFile = FPaths::Combine(Directories::DownloadDir(), FString::Format(TEXT("{0}-{1}-{2}-{3}.{4}"), { Layer, Zoom, X, Y, Format }));
			int XOffset = X - MinX;
			int YOffset = bMaxY_IsNorth ? MaxY - Y : Y - MinY;

			FString FileName = FString::Format(TEXT("{0}_x{1}_y{2}"), { Name, XOffset, YOffset });

			Download::FromURL(ReplacedURL, DownloadFile, false,
				[this, Task, bShowedDialog, OnCompleteElement, ReplacedURL, DownloadFile, FileName, XYZFolder, X, Y](bool bOneSuccess)
				{
					if (bOneSuccess)
					{
						FString DecodedFile = DownloadFile;

						if (bDecodeMapbox)
						{
							DecodedFile = FPaths::Combine(Directories::DownloadDir(), FString::Format(TEXT("MapboxTerrainDEMV1-{0}-{1}-{2}-decoded.tif"), { Zoom, X, Y }));
							if (!MapboxHelpers::DecodeMapboxThreeBands(DownloadFile, DecodedFile, bShowedDialog))
							{
								if (!(*bShowedDialog))
								{
									*bShowedDialog = true;
									FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
										LOCTEXT("HMXYZ::Fetch::Decode", "Could not decode file {0}."),
										FText::FromString(DownloadFile)
									));
								}
								if (OnCompleteElement) OnCompleteElement(false);
								return;
							}
						}

						if (Format.Contains("."))
						{
							FString ExtractionDir = FPaths::Combine(Directories::DownloadDir(), FString::Format(TEXT("{0}-{1}-{2}-{3}"), { Layer, Zoom, X, Y }));
							FString ExtractParams = FString::Format(TEXT("x -aos \"{0}\" -o\"{1}\""), { DownloadFile, ExtractionDir });

							if (!Console::ExecProcess(TEXT("7z"), *ExtractParams))
							{
								if (OnCompleteElement) OnCompleteElement(false);
								return;
							}

							TArray<FString> TileFiles;
							FString ImageFormat = Format.Left(Format.Find(FString(".")));

							FFileManagerGeneric::Get().FindFilesRecursive(TileFiles, *ExtractionDir, *(FString("*.") + ImageFormat), true, false);

							if (TileFiles.Num() != 1)
							{
								if (!(*bShowedDialog))
								{
									*bShowedDialog = true;
									FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
										LOCTEXT("HMXYZ::Fetch::Extract", "Expected one {0} file inside the archive {1}, but found {2}."),
										FText::FromString(ImageFormat),
										FText::FromString(DownloadFile),
										FText::AsNumber(TileFiles.Num())
									));
								}
								if (OnCompleteElement) OnCompleteElement(false);
								return;
							}

							DecodedFile = TileFiles[0];
						}
						
						if (bGeoreferenceSlippyTiles)
						{
							double MinLong, MaxLong, MinLat, MaxLat;

							GDALInterface::XYZTileToEPSG3857(X, Y, Zoom, MinLong, MaxLat);
							GDALInterface::XYZTileToEPSG3857(X+1, Y+1, Zoom, MaxLong, MinLat);

							UE_LOGFMT(LogTemp, Error, "XYZTileToEPSG {0} {1}; {2} {3}; {4} {5}", X, Y, MinLong, MaxLong, MinLat, MaxLat);
							//UE_LOGFMT(LogTemp, Error, "XYZTileToEPSG %f %f %f %f", X+1, Y+1, MinLong, MaxLat);

							FString OutputFile = FPaths::Combine(XYZFolder, FileName + ".tif");
							if (!GDALInterface::AddGeoreference(DecodedFile, OutputFile, "EPSG:3857", MinLong, MaxLong, MinLat, MaxLat))
							{
								if (OnCompleteElement) OnCompleteElement(false);
								return;
							}

							OutputFiles.Add(OutputFile);
						}
						else
						{
							FString OutputFile = FPaths::Combine(XYZFolder, FileName + FPaths::GetExtension(DecodedFile, true));
							if (IFileManager::Get().Copy(*OutputFile, *DecodedFile) != COPY_OK)
							{
								if (OnCompleteElement) OnCompleteElement(false);
								return;
							}

							OutputFiles.Add(OutputFile);
						}
					}

					Task->EnterProgressFrame(1);
					
					if (OnCompleteElement) OnCompleteElement(bOneSuccess);
				}
			);
		},

		[OnComplete, Task, bShowedDialog](bool bSuccess)
		{
			AsyncTask(ENamedThreads::GameThread, [Task]() { Task->Destroy(); });
			if (bShowedDialog) delete(bShowedDialog);
			if (OnComplete) OnComplete(bSuccess);
		}
	);
}

#undef LOCTEXT_NAMESPACE
