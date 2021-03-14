// Copyright Epic Games, Inc. All Rights Reserved.


#include "SPathPicker.h"
#include "Framework/Commands/UIAction.h"
#include "Textures/SlateIcon.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "EditorStyleSet.h"
#include "ContentBrowserUtils.h"
#include "SPathView.h"
#include "IContentBrowserDataModule.h"
#include "ContentBrowserDataSubsystem.h"

#define LOCTEXT_NAMESPACE "ContentBrowser"


void SPathPicker::Construct( const FArguments& InArgs )
{
	for (auto DelegateIt = InArgs._PathPickerConfig.SetPathsDelegates.CreateConstIterator(); DelegateIt; ++DelegateIt)
	{
		if ((*DelegateIt) != NULL)
		{
			(**DelegateIt) = FSetPathPickerPathsDelegate::CreateSP(this, &SPathPicker::SetPaths);
		}
	}

	OnPathSelected = InArgs._PathPickerConfig.OnPathSelected;
	OnGetFolderContextMenu = InArgs._PathPickerConfig.OnGetFolderContextMenu;
	OnGetPathContextMenuExtender = InArgs._PathPickerConfig.OnGetPathContextMenuExtender;

	ChildSlot
	[
		SAssignNew(PathViewPtr, SPathView)
		.InitialCategoryFilter(EContentBrowserItemCategoryFilter::IncludeAssets) // TODO: Allow this to be wholesale overridden via the picker config
		.OnItemSelectionChanged(this, &SPathPicker::OnItemSelectionChanged) // TODO: Allow this to be wholesale overridden via the picker config
		.OnGetItemContextMenu(this, &SPathPicker::GetItemContextMenu) // TODO: Allow this to be wholesale overridden via the picker config
		.FocusSearchBoxWhenOpened(InArgs._PathPickerConfig.bFocusSearchBoxWhenOpened)
		.AllowContextMenu(InArgs._PathPickerConfig.bAllowContextMenu)
		.AllowClassesFolder(InArgs._PathPickerConfig.bAllowClassesFolder)
		.AllowReadOnlyFolders(InArgs._PathPickerConfig.bAllowReadOnlyFolders)
		.SelectionMode(ESelectionMode::Single)
	];

	const FString& DefaultPath = InArgs._PathPickerConfig.DefaultPath;
	if ( !DefaultPath.IsEmpty() )
	{
		if (InArgs._PathPickerConfig.bAddDefaultPath)
		{
			const FName DefaultPathFName = *DefaultPath;
			if (!PathViewPtr->FindItemRecursive(DefaultPathFName))
			{
				const FString DefaultPathLeafName = FPaths::GetPathLeaf(DefaultPath);
				PathViewPtr->AddFolderItem(FContentBrowserItemData(nullptr, EContentBrowserItemFlags::Type_Folder, DefaultPathFName, *DefaultPathLeafName, FText(), nullptr), /*bUserNamed*/false);
			}
		}

		TArray<FString> SelectedPaths;
		SelectedPaths.Add(DefaultPath);
		PathViewPtr->SetSelectedPaths(SelectedPaths);
	}
}

void SPathPicker::OnItemSelectionChanged(const FContentBrowserItem& SelectedItem, ESelectInfo::Type SelectInfo)
{
	FName SelectedPackagePath;
	if (SelectedItem.IsFolder() && SelectedItem.Legacy_TryGetPackagePath(SelectedPackagePath))
	{
		OnPathSelected.ExecuteIfBound(SelectedPackagePath.ToString());
	}
}

TSharedPtr<SWidget> SPathPicker::GetItemContextMenu(TArrayView<const FContentBrowserItem> SelectedItems)
{
	TArray<FString> SelectedPackagePaths;
	for (const FContentBrowserItem& SelectedItem : SelectedItems)
	{
		FName PackagePath;
		if (SelectedItem.Legacy_TryGetPackagePath(PackagePath))
		{
			SelectedPackagePaths.Add(PackagePath.ToString());
		}
	}

	if (SelectedPackagePaths.Num() == 0)
	{
		return nullptr;
	}

	FOnCreateNewFolder OnCreateNewFolder = FOnCreateNewFolder::CreateSP(PathViewPtr.Get(), &SPathView::NewFolderItemRequested);

	if (OnGetFolderContextMenu.IsBound())
	{
		return OnGetFolderContextMenu.Execute(SelectedPackagePaths, OnGetPathContextMenuExtender, OnCreateNewFolder);
	}

	return GetFolderContextMenu(SelectedPackagePaths, OnGetPathContextMenuExtender, OnCreateNewFolder);
}

TSharedPtr<SWidget> SPathPicker::GetFolderContextMenu(const TArray<FString> & SelectedPaths, FContentBrowserMenuExtender_SelectedPaths InMenuExtender, FOnCreateNewFolder InOnCreateNewFolder)
{
	TSharedPtr<FExtender> Extender;
	if (InMenuExtender.IsBound())
	{
		Extender = InMenuExtender.Execute(SelectedPaths);
	}

	const bool bInShouldCloseWindowAfterSelection = true;
	const bool bCloseSelfOnly = true;
	FMenuBuilder MenuBuilder(bInShouldCloseWindowAfterSelection, nullptr, Extender, bCloseSelfOnly);

	// We can only create folders when we have a single path selected
	UContentBrowserDataSubsystem* ContentBrowserData = IContentBrowserDataModule::Get().GetSubsystem();
	const bool bCanCreateNewFolder = SelectedPaths.Num() == 1 && ContentBrowserData->CanCreateFolder(*SelectedPaths[0], nullptr);

	FText NewFolderToolTip;
	if(SelectedPaths.Num() == 1)
	{
		if(bCanCreateNewFolder)
		{
			NewFolderToolTip = FText::Format(LOCTEXT("NewFolderTooltip_CreateIn", "Create a new folder in {0}."), FText::FromString(SelectedPaths[0]));
		}
		else
		{
			NewFolderToolTip = FText::Format(LOCTEXT("NewFolderTooltip_InvalidPath", "Cannot create new folders in {0}."), FText::FromString(SelectedPaths[0]));
		}
	}
	else
	{
		NewFolderToolTip = LOCTEXT("NewFolderTooltip_InvalidNumberOfPaths", "Can only create folders when there is a single path selected.");
	}

	// New Folder
	MenuBuilder.AddMenuEntry(
		LOCTEXT("NewFolder", "New Folder"),
		NewFolderToolTip,
		FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.NewFolderIcon"),
		FUIAction(
			FExecuteAction::CreateSP(this, &SPathPicker::CreateNewFolder, SelectedPaths.Num() > 0 ? SelectedPaths[0] : FString(), InOnCreateNewFolder),
			FCanExecuteAction::CreateLambda( [bCanCreateNewFolder] { return bCanCreateNewFolder; } )
			),
		"FolderContext"
		);

	return MenuBuilder.MakeWidget();
}

void SPathPicker::CreateNewFolder(FString FolderPath, FOnCreateNewFolder InOnCreateNewFolder)
{
	const FText DefaultFolderBaseName = LOCTEXT("DefaultFolderName", "NewFolder");
	UContentBrowserDataSubsystem* ContentBrowserData = IContentBrowserDataModule::Get().GetSubsystem();

	// Create a valid base name for this folder
	FString DefaultFolderName = DefaultFolderBaseName.ToString();
	int32 NewFolderPostfix = 0;
	FName CombinedPathName;
	for (;;)
	{
		FString CombinedPathNameStr = FolderPath / DefaultFolderName;
		if (NewFolderPostfix > 0)
		{
			CombinedPathNameStr.AppendInt(NewFolderPostfix);
		}
		++NewFolderPostfix;

		CombinedPathName = *CombinedPathNameStr;

		const FContentBrowserItem ExistingFolder = ContentBrowserData->GetItemAtPath(CombinedPathName, EContentBrowserItemTypeFilter::IncludeFolders);
		if (!ExistingFolder.IsValid())
		{
			break;
		}
	}

	const FContentBrowserItemTemporaryContext NewFolderItem = ContentBrowserData->CreateFolder(CombinedPathName);
	if (NewFolderItem.IsValid())
	{
		InOnCreateNewFolder.ExecuteIfBound(NewFolderItem);
	}
}

void SPathPicker::SetPaths(const TArray<FString>& NewPaths)
{
	PathViewPtr->SetSelectedPaths(NewPaths);
}

TArray<FString> SPathPicker::GetPaths() const
{
	return PathViewPtr->GetSelectedPaths();
}

const TSharedPtr<SPathView>& SPathPicker::GetPathView() const
{
	return PathViewPtr;
}

#undef LOCTEXT_NAMESPACE
