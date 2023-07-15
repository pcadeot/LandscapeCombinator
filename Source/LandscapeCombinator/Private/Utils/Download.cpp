// Copyright 2023 LandscapeCombinator. All Rights Reserved.

#include "Utils/Download.h"
#include "Utils/Logging.h"
#include "LandscapeCombinatorStyle.h"
#include "Async/Async.h"

#include "Http.h"
#include "Misc/FileHelper.h"
#include "Widgets/Notifications/SProgressBar.h"

#define LOCTEXT_NAMESPACE "FLandscapeCombinatorModule"

namespace Download {
	float SleepSeconds = 0.05;
	float TimeoutSeconds = 10;
}

// FIXME: use TAtomic<bool> for bTriggered, but with TAtomic<bool> bTriggered { false}, everything blocks
// We use bTriggered to avoid OnProcessRequestComplete being invoked multiple times

bool Download::SynchronousFromURL(FString URL, FString File)
{
	UE_LOG(LogLandscapeCombinator, Log, TEXT("Downloading '%s' to '%s'"), *URL, *File);

	if (ExpectedSizeCache.Contains(URL))
	{
		UE_LOG(LogLandscapeCombinator, Log, TEXT("Cache says expected size for '%s' is '%d'"), *URL, ExpectedSizeCache[URL]);
		return SynchronousFromURLExpecting(URL, File, ExpectedSizeCache[URL]);
	}
	else
	{
		TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
		Request->SetURL(URL);
		Request->SetVerb("HEAD");
		Request->SetHeader("User-Agent", "X-UnrealEngine-Agent");

		bool bIsComplete = false;
		int32 ExpectedSize = 0;
		
		bool *bTriggered = new bool(false);
		Request->OnProcessRequestComplete().BindLambda([URL, File, &ExpectedSize, &bIsComplete, bTriggered](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful) {
			if (*bTriggered) return;
			*bTriggered = true;
			if (bWasSuccessful && Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode()))
			{
				ExpectedSize = FCString::Atoi(*Response->GetHeader("Content-Length"));
			}
			bIsComplete = true;
		});
		Request->ProcessRequest();

		float StartTime = FPlatformTime::Seconds();
		while (!bIsComplete && FPlatformTime::Seconds() - StartTime <= TimeoutSeconds)
		{
			FPlatformProcess::Sleep(SleepSeconds);
		}

		Request->CancelRequest();

		return SynchronousFromURLExpecting(URL, File, ExpectedSize);
	}
}

bool Download::SynchronousFromURLExpecting(FString URL, FString File, int32 ExpectedSize)
{
	IPlatformFile &PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (PlatformFile.FileExists(*File) && ExpectedSize != 0 && PlatformFile.FileSize(*File) == ExpectedSize)
	{
		UE_LOG(LogLandscapeCombinator, Log, TEXT("File already exists with the correct size, skipping download of '%s' to '%s' "), *URL, *File);
		return true;
	}

	bool bDownloadResult = false;
	bool bIsComplete = false;

	TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(URL);
	Request->SetVerb("GET");
	Request->SetHeader("User-Agent", "X-UnrealEngine-Agent");
	bool *bTriggered = new bool(false);
	Request->OnProcessRequestComplete().BindLambda([URL, File, &bDownloadResult, &bIsComplete, bTriggered](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
	{
		if (*bTriggered) return;
		*bTriggered = true;
		IPlatformFile &PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		bool DownloadSuccess = bWasSuccessful && Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode());
		bool SavedFile = false;
		if (DownloadSuccess)
		{
			SavedFile = FFileHelper::SaveArrayToFile(Response->GetContent(), *File);
			if (SavedFile)
			{
				AddExpectedSize(URL, PlatformFile.FileSize(*File));
				bDownloadResult = true;
				bIsComplete = true;

				UE_LOG(LogLandscapeCombinator, Log, TEXT("Finished downloading '%s' to '%s'"), *URL, *File);
			}
			else
			{
				bDownloadResult = false;
				bIsComplete = true;
				UE_LOG(LogLandscapeCombinator, Error, TEXT("Error while saving '%s' to '%s'"), *URL, *File);
			}
		}
		else
		{
			UE_LOG(LogLandscapeCombinator, Error, TEXT("Error while downloading '%s' to '%s'"), *URL, *File);
			UE_LOG(LogLandscapeCombinator, Error, TEXT("Request was not successful. Error %d."), Response->GetResponseCode());
			bDownloadResult = false;
			bIsComplete = true;
		}
	});
	Request->ProcessRequest();
		

	float StartTime = FPlatformTime::Seconds();
	while (!bIsComplete && FPlatformTime::Seconds() - StartTime <= TimeoutSeconds)
	{
		FPlatformProcess::Sleep(SleepSeconds);
	}
		
	Request->CancelRequest();

	return bDownloadResult;
}

void Download::FromURL(FString URL, FString File, TFunction<void(bool)> OnComplete)
{
	UE_LOG(LogLandscapeCombinator, Log, TEXT("Downloading from URL '%s' to '%s'"), *URL, *File);

	if (ExpectedSizeCache.Contains(URL))
	{
		UE_LOG(LogLandscapeCombinator, Log, TEXT("Cache says expected size for '%s' is '%d'"), *URL, ExpectedSizeCache[URL]);
		FromURLExpecting(URL, File, ExpectedSizeCache[URL], OnComplete);
	}
	else
	{
		TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
		Request->SetURL(URL);
		Request->SetVerb("HEAD");
		Request->SetHeader("User-Agent", "X-UnrealEngine-Agent");
		bool *bTriggered = new bool(false);
		Request->OnProcessRequestComplete().BindLambda([URL, File, OnComplete, bTriggered](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful) {
			if (*bTriggered) return;
			*bTriggered = true;

			if (bWasSuccessful && Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode()))
			{
				int32 ExpectedSize = FCString::Atoi(*Response->GetHeader("Content-Length"));
				FromURLExpecting(URL, File, ExpectedSize, OnComplete);
			}
			else
			{
				FromURLExpecting(URL, File, 0, OnComplete);
			}
		});
		Request->ProcessRequest();
	}

}

void Download::FromURLExpecting(FString URL, FString File, int32 ExpectedSize, TFunction<void(bool)> OnComplete)
{
	// make sure we are in game thread to spawn download progress windows
	AsyncTask(ENamedThreads::GameThread, [=]()
	{
		double *Downloaded = new double();

		TSharedPtr<SWindow> Window = SNew(SWindow)
			.SizingRule(ESizingRule::Autosized)
			.AutoCenter(EAutoCenter::PrimaryWorkArea)
			.Title(LOCTEXT("DownloadProgress", "Download Progress"));

		IPlatformFile &PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		if (PlatformFile.FileExists(*File) && ExpectedSize != 0 && PlatformFile.FileSize(*File) == ExpectedSize)
		{
			UE_LOG(LogLandscapeCombinator, Log, TEXT("File already exists with the correct size, skipping download of '%s' to '%s' "), *URL, *File);
			if (OnComplete) OnComplete(true);
			return;
		}

		TSharedPtr<IHttpRequest> Request = FHttpModule::Get().CreateRequest().ToSharedPtr();
		Request->SetURL(URL);
		Request->SetVerb("GET");
		Request->SetHeader("User-Agent", "X-UnrealEngine-Agent");
		Request->OnRequestProgress().BindLambda([Downloaded](FHttpRequestPtr Request, int32 Sent, int32 Received) {
			*Downloaded = Received;
		});
		bool *bTriggered = new bool(false);
		Request->OnProcessRequestComplete().BindLambda([URL, File, OnComplete, Window, bTriggered](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
		{
			if (*bTriggered) return;
			*bTriggered = true;

			IPlatformFile &PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
			bool DownloadSuccess = bWasSuccessful && Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode());
			bool SavedFile = false;
			if (DownloadSuccess)
			{
				SavedFile = FFileHelper::SaveArrayToFile(Response->GetContent(), *File);
				if (SavedFile)
				{
					AddExpectedSize(URL, PlatformFile.FileSize(*File));
					UE_LOG(LogLandscapeCombinator, Log, TEXT("Finished downloading '%s' to '%s'"), *URL, *File);
				}
				else
				{
					UE_LOG(LogLandscapeCombinator, Error, TEXT("Error while saving '%s' to '%s'"), *URL, *File);
				}
			}
			else
			{
				UE_LOG(LogLandscapeCombinator, Error, TEXT("Error while downloading '%s' to '%s'"), *URL, *File);
				if (Response.IsValid())
				{
					UE_LOG(LogLandscapeCombinator, Error, TEXT("Request was not successful. Error %d."), Response->GetResponseCode());
				}
			}
			if (OnComplete) OnComplete(DownloadSuccess && SavedFile);
			Window->RequestDestroyWindow();
		});


		Request->ProcessRequest();

		Window->SetContent(
			SNew(SBox).Padding(FMargin(30, 30, 30, 30))
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(
						FText::Format(
							LOCTEXT("DowloadingURL", "Dowloading {0} to {1}."),
							FText::FromString(URL.Left(20)),
							FText::FromString(File)
						)
					).Font(FLandscapeCombinatorStyle::RegularFont())
				]
				+SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 0, 0, 20))
				[
					SNew(SProgressBar)
					.Percent_Lambda([Downloaded, ExpectedSize, Request]() {
						if (ExpectedSize) return *Downloaded / ExpectedSize;
						return *Downloaded / MAX_int32;
					})
					.RefreshRate(0.1)
				]
				+SVerticalBox::Slot().AutoHeight().HAlign(EHorizontalAlignment::HAlign_Center)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot().AutoWidth().HAlign(EHorizontalAlignment::HAlign_Center)
					[
						SNew(SButton)
						.OnClicked_Lambda([Window, Request]()->FReply {
							Request->CancelRequest();
							Window->RequestDestroyWindow();
							return FReply::Handled();
						})
						[
							SNew(STextBlock).Font(FLandscapeCombinatorStyle::RegularFont()).Text(FText::FromString(" Cancel "))
						]
					]
				]
			]
		);
		Window->SetOnWindowClosed(FOnWindowClosed::CreateLambda([Request](const TSharedRef<SWindow>& Window) {
			Request->CancelRequest();
		}));
		FSlateApplication::Get().AddWindow(Window.ToSharedRef());
	});
}

void Download::AddExpectedSize(FString URL, int32 ExpectedSize)
{
	if (!ExpectedSizeCache.Contains(URL))
	{
		ExpectedSizeCache.Add(URL, ExpectedSize);
		SaveExpectedSizeCache();
	}
}

FString Download::ExpectedSizeCacheFile()
{
	IPlatformFile::GetPlatformPhysical().CreateDirectory(*FPaths::ProjectSavedDir());
	return FPaths::Combine(FPaths::ProjectSavedDir(), "ExpectedSizeCache"); 
}

void Download::LoadExpectedSizeCache()
{
	FString CacheFile = ExpectedSizeCacheFile();
	//UE_LOG(LogLandscapeCombinator, Log, TEXT("Loading expected size cache from '%s'"), *CacheFile);
		
	FArchive* FileReader = IFileManager::Get().CreateFileReader(*CacheFile);
	if (FileReader)
	{
		*FileReader << ExpectedSizeCache;
		FileReader->Close();
	}
}

void Download::SaveExpectedSizeCache()
{
	FString CacheFile = ExpectedSizeCacheFile();
	//UE_LOG(LogLandscapeCombinator, Log, TEXT("Saving expected size cache to '%s'"), *CacheFile);
	FArchive* FileWriter = IFileManager::Get().CreateFileWriter(*CacheFile);
	if (FileWriter)
	{
		*FileWriter << ExpectedSizeCache;
		if (FileWriter->Close()) return;
	}

	UE_LOG(LogLandscapeCombinator, Error, TEXT("Failed to save expected size cache to '%s'"), *CacheFile);
}

#undef LOCTEXT_NAMESPACE
