// Copyright Epic Games, Inc. All Rights Reserved.

#include "MemoryTracker.h"

// Insights
#include "Insights/InsightsManager.h"
#include "Insights/MemoryProfiler/ViewModels/MemoryTag.h"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////
// FMemoryTracker
////////////////////////////////////////////////////////////////////////////////////////////////////

FMemoryTracker::FMemoryTracker(FMemoryTrackerId InTrackerId, const FString InTrackerName)
	: Id(InTrackerId)
	, Name(InTrackerName)
{
	//TODO: static_assert(FMemoryTracker::InvalidTrackerId == static_cast<FMemoryTracker>(Trace::FMemoryTrackerDesc::InvalidTrackerId), "Memory TrackerId type mismatch!");
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FMemoryTracker::~FMemoryTracker()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMemoryTracker::Update()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights
