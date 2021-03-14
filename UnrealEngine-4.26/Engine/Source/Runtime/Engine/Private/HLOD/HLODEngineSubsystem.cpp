// Copyright Epic Games, Inc. All Rights Reserved.

#include "HLOD/HLODEngineSubsystem.h"

#if WITH_EDITOR

#include "EngineUtils.h"
#include "Engine/EngineTypes.h"
#include "Engine/LODActor.h"
#include "Engine/HLODProxy.h"
#include "Editor.h"
#include "UnrealEngine.h"
#include "HierarchicalLOD.h"
#include "Modules/ModuleManager.h"
#include "IHierarchicalLODUtilities.h"
#include "HierarchicalLODUtilitiesModule.h"
#include "GameFramework/WorldSettings.h"

void UHLODEngineSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	bDisableHLODCleanupOnLoad = false;

	Super::Initialize(Collection);
	RegisterRecreateLODActorsDelegates();
}

void UHLODEngineSubsystem::Deinitialize()
{
	UnregisterRecreateLODActorsDelegates();
	Super::Deinitialize();
}

void UHLODEngineSubsystem::DisableHLODCleanupOnLoad(bool bInDisableHLODCleanup)
{
	bDisableHLODCleanupOnLoad = bInDisableHLODCleanup;
}

void UHLODEngineSubsystem::OnSaveLODActorsToHLODPackagesChanged()
{
	UnregisterRecreateLODActorsDelegates();
	RegisterRecreateLODActorsDelegates();
}

void UHLODEngineSubsystem::UnregisterRecreateLODActorsDelegates()
{
	FWorldDelegates::OnPostWorldInitialization.Remove(OnPostWorldInitializationDelegateHandle);
	FWorldDelegates::LevelAddedToWorld.Remove(OnLevelAddedToWorldDelegateHandle);
}

void UHLODEngineSubsystem::RegisterRecreateLODActorsDelegates()
{
	if (GetDefault<UHierarchicalLODSettings>()->bSaveLODActorsToHLODPackages)
	{
		OnPostWorldInitializationDelegateHandle = FWorldDelegates::OnPostWorldInitialization.AddUObject(this, &UHLODEngineSubsystem::RecreateLODActorsForWorld);
		OnLevelAddedToWorldDelegateHandle = FWorldDelegates::LevelAddedToWorld.AddUObject(this, &UHLODEngineSubsystem::RecreateLODActorsForLevel);
		OnPreSaveWorlDelegateHandle = FEditorDelegates::PreSaveWorld.AddUObject(this, &UHLODEngineSubsystem::OnPreSaveWorld);
	}	
}

void UHLODEngineSubsystem::RecreateLODActorsForWorld(UWorld* InWorld, const UWorld::InitializationValues InInitializationValues)
{
	// For each level in this world
	for (ULevel* Level : InWorld->GetLevels())
	{
		RecreateLODActorsForLevel(Level, InWorld);
	}
}

void UHLODEngineSubsystem::RecreateLODActorsForLevel(ULevel* InLevel, UWorld* InWorld)
{
	bool bShouldRecreateActors = InWorld && !InWorld->bIsTearingDown && !InWorld->IsPreviewWorld();
	if (!bShouldRecreateActors)
	{
		return;
	}

	FHierarchicalLODUtilitiesModule& Module = FModuleManager::LoadModuleChecked<FHierarchicalLODUtilitiesModule>("HierarchicalLODUtilities");
	IHierarchicalLODUtilities* Utilities = Module.GetUtilities();

	// First, destroy LODActors that were previously constructed from HLODDesc... If needed, they will be recreated below.
	if (!bDisableHLODCleanupOnLoad)
	{
		CleanupHLODs(InLevel);
	}

	// Look for HLODProxy packages associated with this level
	int32 NumLODLevels = InLevel->GetWorldSettings()->bEnableHierarchicalLODSystem ? InLevel->GetWorldSettings()->GetHierarchicalLODSetup().Num() : 0;
	for (int32 LODIndex = 0; LODIndex < NumLODLevels; ++LODIndex)
	{
		// Obtain HLOD package for the current HLOD level
		UHLODProxy* HLODProxy = Utilities->RetrieveLevelHLODProxy(InLevel, LODIndex);
		if (HLODProxy)
		{
			// Spawn LODActors from the HLODDesc, if any is found
			HLODProxy->SpawnLODActors(InLevel);
		}
	}
}

bool UHLODEngineSubsystem::CleanupHLODs(ULevel* InLevel)
{
	bool bPerformedCleanup = false;

	for (AActor* Actor : InLevel->Actors)
	{
		if (ALODActor* LODActor = Cast<ALODActor>(Actor))
		{
			bPerformedCleanup |= CleanupHLOD(LODActor);
		}
	}

	return bPerformedCleanup;
}

bool UHLODEngineSubsystem::CleanupHLODs(UWorld* InWorld)
{
	bool bPerformedCleanup = false;

	for (TActorIterator<ALODActor> It(InWorld); It; ++It)
	{
		bPerformedCleanup |= CleanupHLOD(*It);
	}

	return bPerformedCleanup;
}

bool UHLODEngineSubsystem::CleanupHLOD(ALODActor* InLODActor)
{
	bool bShouldDestroyActor = false;

	UWorld* World = InLODActor->GetWorld();

	if (!InLODActor->GetLevel()->GetWorldSettings()->bEnableHierarchicalLODSystem || InLODActor->GetLevel()->GetWorldSettings()->GetHierarchicalLODSetup().Num() == 0)
	{
		UE_LOG(LogEngine, Warning, TEXT("Deleting LODActor %s found in map with no HLOD setup or disabled HLOD system. Resave %s to silence warning."), *InLODActor->GetName(), *InLODActor->GetOutermost()->GetPathName());
		bShouldDestroyActor = true;
	}
	else if (!InLODActor->GetProxy() || InLODActor->GetProxy()->GetMap() != TSoftObjectPtr<UWorld>(World))
	{
		UE_LOG(LogEngine, Warning, TEXT("Deleting LODActor %s with invalid HLODProxy. Resave %s to silence warning."), *InLODActor->GetName(), *InLODActor->GetOutermost()->GetPathName());
		bShouldDestroyActor = true;
	}
	else if (GetDefault<UHierarchicalLODSettings>()->bSaveLODActorsToHLODPackages && !InLODActor->HasAnyFlags(RF_Transient))
	{
		UE_LOG(LogEngine, Warning, TEXT("Deleting non-transient LODActor %s. Rebuild HLOD & resave %s to silence warning."), *InLODActor->GetName(), *InLODActor->GetOutermost()->GetPathName());
		bShouldDestroyActor = true;
	}

	if (bShouldDestroyActor)
	{
		World->EditorDestroyActor(InLODActor, true);
	}

	return bShouldDestroyActor;
}

void UHLODEngineSubsystem::OnPreSaveWorld(uint32 InSaveFlags, UWorld* InWorld)
{
	// When cooking, make sure that the LODActors are not transient
	if (InWorld && InWorld->PersistentLevel && GIsCookerLoadingPackage)
	{
		for (TActorIterator<ALODActor> It(InWorld); It; ++It)
		{
			ALODActor* LODActor = *It;
			if (LODActor->WasBuiltFromHLODDesc())
			{
				EObjectFlags TransientFlags = EObjectFlags::RF_Transient | EObjectFlags::RF_DuplicateTransient;
				if (LODActor->HasAnyFlags(TransientFlags))
				{
					LODActor->ClearFlags(TransientFlags);

					const bool bIncludeNestedObjects = true;
					ForEachObjectWithOuter(LODActor, [TransientFlags](UObject* Subobject)
					{
						Subobject->ClearFlags(TransientFlags);
					}, bIncludeNestedObjects);
				}
			}
		}
	}
}

#endif // WITH_EDITOR
