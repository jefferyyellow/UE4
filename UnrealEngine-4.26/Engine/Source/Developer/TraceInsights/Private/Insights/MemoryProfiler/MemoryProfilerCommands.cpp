// Copyright Epic Games, Inc. All Rights Reserved.

#include "MemoryProfilerCommands.h"

#include "DesktopPlatformModule.h"
#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

// Insights
#include "Insights/InsightsManager.h"
#include "Insights/MemoryProfiler/MemoryProfilerManager.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "FMemoryProfilerCommands"

////////////////////////////////////////////////////////////////////////////////////////////////////
// FMemoryProfilerMenuBuilder
////////////////////////////////////////////////////////////////////////////////////////////////////

void FMemoryProfilerMenuBuilder::AddMenuEntry(FMenuBuilder& MenuBuilder, const TSharedPtr< FUICommandInfo >& UICommandInfo, const FUIAction& UIAction)
{
	MenuBuilder.AddMenuEntry
	(
		UICommandInfo->GetLabel(),
		UICommandInfo->GetDescription(),
		UICommandInfo->GetIcon(),
		UIAction,
		NAME_None,
		UICommandInfo->GetUserInterfaceType()
	);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FMemoryProfilerCommands
////////////////////////////////////////////////////////////////////////////////////////////////////

FMemoryProfilerCommands::FMemoryProfilerCommands()
	: TCommands<FMemoryProfilerCommands>(
		TEXT("MemoryProfilerCommand"), // Context name for fast lookup
		NSLOCTEXT("Contexts", "MemoryProfilerCommand", "Memory Insights"), // Localized context name for displaying
		NAME_None, // Parent
		FEditorStyle::GetStyleSetName() // Icon Style Set
	)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

// UI_COMMAND takes long for the compiler to optimize
PRAGMA_DISABLE_OPTIMIZATION
void FMemoryProfilerCommands::RegisterCommands()
{
	UI_COMMAND(ToggleTimingViewVisibility, "Timing", "Toggles the visibility of the main Timing view", EUserInterfaceActionType::ToggleButton, FInputChord(EModifierKey::Control, EKeys::T));
	UI_COMMAND(ToggleMemTagTreeViewVisibility, "LLM Tags", "Toggles the visibility of the LLM Tags tree view", EUserInterfaceActionType::ToggleButton, FInputChord(EModifierKey::Control, EKeys::M));
}
PRAGMA_ENABLE_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////
// Toggle Commands
////////////////////////////////////////////////////////////////////////////////////////////////////

#define IMPLEMENT_TOGGLE_COMMAND(CmdName, IsEnabled, SetIsEnabled) \
	\
	void FMemoryProfilerActionManager::Map_##CmdName##_Global()\
	{\
		This->CommandList->MapAction(This->GetCommands().CmdName, CmdName##_Custom());\
	}\
	\
	const FUIAction FMemoryProfilerActionManager::CmdName##_Custom() \
	{\
		FUIAction UIAction;\
		UIAction.ExecuteAction = FExecuteAction::CreateRaw(this, &FMemoryProfilerActionManager::CmdName##_Execute);\
		UIAction.CanExecuteAction = FCanExecuteAction::CreateRaw(this, &FMemoryProfilerActionManager::CmdName##_CanExecute);\
		UIAction.GetActionCheckState = FGetActionCheckState::CreateRaw(this, &FMemoryProfilerActionManager::CmdName##_GetCheckState);\
		return UIAction;\
	}\
	\
	void FMemoryProfilerActionManager::CmdName##_Execute()\
	{\
		const bool b##IsEnabled = !This->IsEnabled();\
		This->SetIsEnabled(b##IsEnabled);\
	}\
	\
	bool FMemoryProfilerActionManager::CmdName##_CanExecute() const\
	{\
		return FInsightsManager::Get()->GetSession().IsValid();\
	}\
	\
	ECheckBoxState FMemoryProfilerActionManager::CmdName##_GetCheckState() const\
	{\
		const bool b##IsEnabled = This->IsEnabled();\
		return b##IsEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;\
	}

IMPLEMENT_TOGGLE_COMMAND(ToggleTimingViewVisibility, IsTimingViewVisible, ShowHideTimingView)
IMPLEMENT_TOGGLE_COMMAND(ToggleMemTagTreeViewVisibility, IsMemTagTreeViewVisible, ShowHideMemTagTreeView)

#undef IMPLEMENT_TOGGLE_COMMAND

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
