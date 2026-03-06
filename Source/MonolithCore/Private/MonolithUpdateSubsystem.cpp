#include "MonolithUpdateSubsystem.h"
#include "MonolithCoreModule.h"
#include "MonolithHttpServer.h"
#include "MonolithSettings.h"
#include "MonolithJsonUtils.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformFileManager.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Containers/Ticker.h"

#define MONOLITH_GITHUB_API TEXT("https://api.github.com/repos/tumourlove/monolith/releases/latest")

// ─── FMonolithVersionInfo ───────────────────────────────────────────────────

FString FMonolithVersionInfo::GetVersionFilePath()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Monolith"), TEXT("version.json"));
}

FMonolithVersionInfo FMonolithVersionInfo::LoadFromDisk()
{
	FMonolithVersionInfo Info;
	Info.Current = MONOLITH_VERSION;

	FString FilePath = GetVersionFilePath();
	FString JsonString;
	if (FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		TSharedPtr<FJsonObject> JsonObj = FMonolithJsonUtils::Parse(JsonString);
		if (JsonObj.IsValid())
		{
			JsonObj->TryGetStringField(TEXT("current"), Info.Current);
			JsonObj->TryGetStringField(TEXT("pending"), Info.Pending);
			JsonObj->TryGetBoolField(TEXT("staging"), Info.bStaging);
		}
	}

	return Info;
}

void FMonolithVersionInfo::SaveToDisk() const
{
	TSharedPtr<FJsonObject> JsonObj = MakeShared<FJsonObject>();
	JsonObj->SetStringField(TEXT("current"), Current);
	if (Pending.IsEmpty())
	{
		JsonObj->SetField(TEXT("pending"), MakeShared<FJsonValueNull>());
	}
	else
	{
		JsonObj->SetStringField(TEXT("pending"), Pending);
	}
	JsonObj->SetBoolField(TEXT("staging"), bStaging);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);

	FString FilePath = GetVersionFilePath();
	FString Dir = FPaths::GetPath(FilePath);
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*Dir))
	{
		PlatformFile.CreateDirectoryTree(*Dir);
	}

	FFileHelper::SaveStringToFile(OutputString, *FilePath);
}

// ─── UMonolithUpdateSubsystem ───────────────────────────────────────────────

void UMonolithUpdateSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	VersionInfo = FMonolithVersionInfo::LoadFromDisk();

	// Check for staged update from previous session
	if (VersionInfo.bStaging)
	{
		ApplyStagedUpdate();
	}

	// Auto-check for updates if enabled
	const UMonolithSettings* Settings = UMonolithSettings::Get();
	if (Settings && Settings->bAutoUpdateEnabled)
	{
		// Delay the check so the editor finishes loading
		TWeakObjectPtr<UMonolithUpdateSubsystem> WeakThis(this);
		UpdateCheckTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda([WeakThis](float)
			{
				if (UMonolithUpdateSubsystem* Self = WeakThis.Get())
				{
					Self->CheckForUpdate();
				}
				return false; // One-shot
			}),
			5.0f
		);
	}
}

void UMonolithUpdateSubsystem::Deinitialize()
{
	if (UpdateCheckTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(UpdateCheckTickerHandle);
		UpdateCheckTickerHandle.Reset();
	}

	Super::Deinitialize();
}

void UMonolithUpdateSubsystem::CheckForUpdate()
{
	UE_LOG(LogMonolith, Log, TEXT("Checking for Monolith updates..."));

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(MONOLITH_GITHUB_API);
	Request->SetVerb(TEXT("GET"));
	Request->SetHeader(TEXT("User-Agent"), TEXT("Monolith-UE-Plugin"));
	Request->SetHeader(TEXT("Accept"), TEXT("application/vnd.github.v3+json"));

	Request->OnProcessRequestComplete().BindLambda(
		[this](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bConnectedSuccessfully)
		{
			if (!bConnectedSuccessfully || !HttpResponse.IsValid())
			{
				UE_LOG(LogMonolith, Warning, TEXT("Failed to reach GitHub API for update check"));
				return;
			}

			if (HttpResponse->GetResponseCode() != 200)
			{
				UE_LOG(LogMonolith, Warning, TEXT("GitHub API returned %d"), HttpResponse->GetResponseCode());
				return;
			}

			TSharedPtr<FJsonObject> JsonObj = FMonolithJsonUtils::Parse(HttpResponse->GetContentAsString());
			if (!JsonObj.IsValid())
			{
				UE_LOG(LogMonolith, Warning, TEXT("Failed to parse GitHub release JSON"));
				return;
			}

			FString TagName;
			if (!JsonObj->TryGetStringField(TEXT("tag_name"), TagName))
			{
				UE_LOG(LogMonolith, Warning, TEXT("No tag_name in release JSON"));
				return;
			}

			FString RemoteVersion = ParseVersionFromTag(TagName);
			FString CurrentVersion = VersionInfo.Current;

			UE_LOG(LogMonolith, Log, TEXT("Current: %s, Latest: %s"), *CurrentVersion, *RemoteVersion);

			if (CompareVersions(CurrentVersion, RemoteVersion) > 0)
			{
				// Find the zip asset URL
				FString ZipUrl;
				const TArray<TSharedPtr<FJsonValue>>* Assets;
				if (JsonObj->TryGetArrayField(TEXT("assets"), Assets))
				{
					for (const TSharedPtr<FJsonValue>& AssetVal : *Assets)
					{
						const TSharedPtr<FJsonObject>* AssetObj;
						if (AssetVal->TryGetObject(AssetObj))
						{
							FString Name;
							(*AssetObj)->TryGetStringField(TEXT("name"), Name);
							if (Name.EndsWith(TEXT(".zip")))
							{
								(*AssetObj)->TryGetStringField(TEXT("browser_download_url"), ZipUrl);
								break;
							}
						}
					}
				}

				// Fallback to zipball_url if no zip asset
				if (ZipUrl.IsEmpty())
				{
					JsonObj->TryGetStringField(TEXT("zipball_url"), ZipUrl);
				}

				if (!ZipUrl.IsEmpty())
				{
					ShowUpdateNotification(RemoteVersion, ZipUrl);
				}
				else
				{
					UE_LOG(LogMonolith, Warning, TEXT("New version %s available but no download URL found"), *RemoteVersion);
				}
			}
			else
			{
				UE_LOG(LogMonolith, Log, TEXT("Monolith is up to date"));
			}
		}
	);

	Request->ProcessRequest();
}

FString UMonolithUpdateSubsystem::ParseVersionFromTag(const FString& Tag)
{
	FString Version = Tag;
	Version.TrimStartAndEndInline();
	if (Version.StartsWith(TEXT("v")) || Version.StartsWith(TEXT("V")))
	{
		Version.RightChopInline(1);
	}
	return Version;
}

int32 UMonolithUpdateSubsystem::CompareVersions(const FString& Current, const FString& Remote)
{
	auto ParseParts = [](const FString& Ver, int32& Major, int32& Minor, int32& Patch)
	{
		TArray<FString> Parts;
		Ver.ParseIntoArray(Parts, TEXT("."));
		Major = Parts.IsValidIndex(0) ? FCString::Atoi(*Parts[0]) : 0;
		Minor = Parts.IsValidIndex(1) ? FCString::Atoi(*Parts[1]) : 0;
		Patch = Parts.IsValidIndex(2) ? FCString::Atoi(*Parts[2]) : 0;
	};

	int32 CMajor, CMinor, CPatch;
	int32 RMajor, RMinor, RPatch;
	ParseParts(Current, CMajor, CMinor, CPatch);
	ParseParts(Remote, RMajor, RMinor, RPatch);

	if (RMajor != CMajor) return RMajor - CMajor;
	if (RMinor != CMinor) return RMinor - CMinor;
	return RPatch - CPatch;
}

void UMonolithUpdateSubsystem::ShowUpdateNotification(const FString& NewVersion, const FString& ZipUrl)
{
	FText NotifText = FText::Format(
		NSLOCTEXT("Monolith", "UpdateAvailable", "Monolith {0} is available (current: {1})"),
		FText::FromString(NewVersion),
		FText::FromString(VersionInfo.Current)
	);

	FNotificationInfo Info(NotifText);
	Info.bFireAndForget = false;
	Info.ExpireDuration = 30.0f;
	Info.bUseThrobber = false;
	Info.bUseLargeFont = false;

	// "Update" hyperlink
	FString CapturedUrl = ZipUrl;
	FString CapturedVersion = NewVersion;
	Info.HyperlinkText = NSLOCTEXT("Monolith", "UpdateHyperlink", "Install Update");
	Info.Hyperlink = FSimpleDelegate::CreateLambda(
		[this, CapturedUrl, CapturedVersion]()
		{
			DownloadUpdate(CapturedUrl, CapturedVersion);
		}
	);

	FSlateNotificationManager::Get().AddNotification(Info);

	UE_LOG(LogMonolith, Log, TEXT("Update notification shown for version %s"), *NewVersion);
}

void UMonolithUpdateSubsystem::DownloadUpdate(const FString& ZipUrl, const FString& Version)
{
	if (bUpdateInProgress)
	{
		UE_LOG(LogMonolith, Warning, TEXT("Update already in progress"));
		return;
	}

	bUpdateInProgress = true;
	UE_LOG(LogMonolith, Log, TEXT("Downloading Monolith %s from %s"), *Version, *ZipUrl);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(ZipUrl);
	Request->SetVerb(TEXT("GET"));
	Request->SetHeader(TEXT("User-Agent"), TEXT("Monolith-UE-Plugin"));

	FString CapturedVersion = Version;
	Request->OnProcessRequestComplete().BindLambda(
		[this, CapturedVersion](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bConnectedSuccessfully)
		{
			if (!bConnectedSuccessfully || !HttpResponse.IsValid() || HttpResponse->GetResponseCode() != 200)
			{
				UE_LOG(LogMonolith, Error, TEXT("Failed to download update (HTTP %d)"),
					HttpResponse.IsValid() ? HttpResponse->GetResponseCode() : 0);
				bUpdateInProgress = false;
				return;
			}

			OnDownloadComplete(CapturedVersion, true, HttpResponse->GetContent());
		}
	);

	Request->ProcessRequest();
}

void UMonolithUpdateSubsystem::OnDownloadComplete(const FString& Version, bool bSuccess, const TArray<uint8>& Data)
{
	if (!bSuccess || Data.Num() == 0)
	{
		UE_LOG(LogMonolith, Error, TEXT("Update download failed or empty"));
		bUpdateInProgress = false;
		return;
	}

	// Save zip to temp
	FString TempZipPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Monolith"), TEXT("update.zip"));
	FString TempDir = FPaths::GetPath(TempZipPath);
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*TempDir))
	{
		PlatformFile.CreateDirectoryTree(*TempDir);
	}

	if (!FFileHelper::SaveArrayToFile(Data, *TempZipPath))
	{
		UE_LOG(LogMonolith, Error, TEXT("Failed to save update zip to %s"), *TempZipPath);
		bUpdateInProgress = false;
		return;
	}

	UE_LOG(LogMonolith, Log, TEXT("Update zip saved (%d bytes), extracting to staging..."), Data.Num());

	// Extract to staging directory
	FString StagingDir = GetStagingPath();
	if (PlatformFile.DirectoryExists(*StagingDir))
	{
		PlatformFile.DeleteDirectoryRecursively(*StagingDir);
	}

	if (!ExtractZipToDirectory(TempZipPath, StagingDir))
	{
		UE_LOG(LogMonolith, Error, TEXT("Failed to extract update zip"));
		bUpdateInProgress = false;
		return;
	}

	// Clean up temp zip
	PlatformFile.DeleteFile(*TempZipPath);

	// Update version.json
	VersionInfo.Pending = Version;
	VersionInfo.bStaging = true;
	VersionInfo.SaveToDisk();

	bUpdateInProgress = false;

	UE_LOG(LogMonolith, Log, TEXT("Monolith %s staged. Restart the editor to apply."), *Version);

	// Notify user
	FNotificationInfo Info(FText::Format(
		NSLOCTEXT("Monolith", "UpdateStaged", "Monolith {0} staged. Restart the editor to apply."),
		FText::FromString(Version)
	));
	Info.ExpireDuration = 10.0f;
	Info.bUseThrobber = false;
	FSlateNotificationManager::Get().AddNotification(Info);
}

bool UMonolithUpdateSubsystem::ExtractZipToDirectory(const FString& ZipPath, const FString& DestDir)
{
	FString ConvertedZip = FPaths::ConvertRelativePathToFull(ZipPath);
	FString ConvertedDest = FPaths::ConvertRelativePathToFull(DestDir);

	int32 ReturnCode = -1;

#if PLATFORM_WINDOWS
	ConvertedZip.ReplaceInline(TEXT("/"), TEXT("\\"));
	ConvertedDest.ReplaceInline(TEXT("/"), TEXT("\\"));

	FString Args = FString::Printf(
		TEXT("-NoProfile -NonInteractive -Command \"Expand-Archive -Path '%s' -DestinationPath '%s' -Force\""),
		*ConvertedZip, *ConvertedDest
	);

	FPlatformProcess::ExecProcess(
		TEXT("powershell.exe"),
		*Args,
		&ReturnCode,
		nullptr,
		nullptr
	);

	if (ReturnCode != 0)
	{
		UE_LOG(LogMonolith, Error, TEXT("PowerShell Expand-Archive failed (exit code %d)"), ReturnCode);
		return false;
	}
#elif PLATFORM_MAC || PLATFORM_LINUX
	// Ensure destination directory exists
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*ConvertedDest))
	{
		PlatformFile.CreateDirectoryTree(*ConvertedDest);
	}

	FString Args = FString::Printf(
		TEXT("-xzf \"%s\" -C \"%s\""),
		*ConvertedZip, *ConvertedDest
	);

	FPlatformProcess::ExecProcess(
		TEXT("/usr/bin/tar"),
		*Args,
		&ReturnCode,
		nullptr,
		nullptr
	);

	if (ReturnCode != 0)
	{
		// Fallback: try unzip for plain .zip files (tar may fail on non-gzipped zips)
		FString UnzipArgs = FString::Printf(
			TEXT("-o \"%s\" -d \"%s\""),
			*ConvertedZip, *ConvertedDest
		);

		FPlatformProcess::ExecProcess(
			TEXT("/usr/bin/unzip"),
			*UnzipArgs,
			&ReturnCode,
			nullptr,
			nullptr
		);

		if (ReturnCode != 0)
		{
			UE_LOG(LogMonolith, Error, TEXT("Failed to extract update archive (tar and unzip both failed, exit code %d)"), ReturnCode);
			return false;
		}
	}
#else
	UE_LOG(LogMonolith, Error, TEXT("Update extraction not supported on this platform"));
	return false;
#endif

	return true;
}

void UMonolithUpdateSubsystem::ApplyStagedUpdate()
{
	FString StagingDir = GetStagingPath();
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	if (!PlatformFile.DirectoryExists(*StagingDir))
	{
		UE_LOG(LogMonolith, Warning, TEXT("Staging directory not found, clearing staging flag"));
		VersionInfo.bStaging = false;
		VersionInfo.Pending.Empty();
		VersionInfo.SaveToDisk();
		return;
	}

	UE_LOG(LogMonolith, Log, TEXT("Applying staged Monolith update %s..."), *VersionInfo.Pending);

	FString PluginDir = GetPluginPath();
	FString BackupDir = PluginDir + TEXT("_Backup");

	// Remove old backup if it exists
	if (PlatformFile.DirectoryExists(*BackupDir))
	{
		PlatformFile.DeleteDirectoryRecursively(*BackupDir);
	}

	// The staging dir may contain a nested folder from the zip.
	// Find the actual content root (look for Monolith.uplugin).
	FString ContentRoot = StagingDir;
	TArray<FString> FoundFiles;
	PlatformFile.FindFilesRecursively(FoundFiles, *StagingDir, TEXT(".uplugin"));
	if (FoundFiles.Num() > 0)
	{
		ContentRoot = FPaths::GetPath(FoundFiles[0]);
	}

	// We can't swap our own plugin directory while the module is loaded.
	// Log instructions for the user — the actual swap happens via a helper script or manually.
	UE_LOG(LogMonolith, Warning,
		TEXT("Monolith staged update ready at: %s"),
		*ContentRoot
	);
	UE_LOG(LogMonolith, Warning,
		TEXT("To complete the update: close the editor, replace Plugins/Monolith/ with the staged content, then reopen."));

	// Update version info
	if (!VersionInfo.Pending.IsEmpty())
	{
		VersionInfo.Current = VersionInfo.Pending;
	}
	VersionInfo.Pending.Empty();
	VersionInfo.bStaging = false;
	VersionInfo.SaveToDisk();
}

FString UMonolithUpdateSubsystem::GetStagingPath()
{
	return FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("Monolith_Staging"));
}

FString UMonolithUpdateSubsystem::GetPluginPath()
{
	return FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("Monolith"));
}
