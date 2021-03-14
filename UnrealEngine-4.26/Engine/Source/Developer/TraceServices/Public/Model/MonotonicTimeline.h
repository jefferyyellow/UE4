// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Common/PagedArray.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/Containers/Timelines.h"

namespace Trace
{

struct FMonotonicTimelineDefaultSettings
{
	enum
	{
		MaxDepth = 1024,
		ScopeEntriesPageSize = 65536,
		EventsPageSize = 65536,
		DetailLevelsCount = 6,
	};

	constexpr static double DetailLevelResolution(int32 Index)
	{
		const double DetailLevels[DetailLevelsCount] = { 0.0, 0.0001, 0.001, 0.008, 0.04, 0.2 };
		return DetailLevels[Index];
	}
};

template<typename InEventType, typename SettingsType = FMonotonicTimelineDefaultSettings>
class TMonotonicTimeline
	: public ITimeline<InEventType>
{
public:
	using EventType = InEventType;

	TMonotonicTimeline(ILinearAllocator& InAllocator)
		: Allocator(InAllocator)
	{
		
		for (int32 DetailLevelIndex = 0; DetailLevelIndex < SettingsType::DetailLevelsCount; ++DetailLevelIndex)
		{
			double Resolution = SettingsType::DetailLevelResolution(DetailLevelIndex);
			DetailLevels.Emplace(Allocator, Resolution);
		}
	}

	virtual ~TMonotonicTimeline() = default;
	
	void AppendBeginEvent(double StartTime, const EventType& Event)
	{
		int32 CurrentDepth = DetailLevels[0].InsertionState.CurrentDepth;

		AddScopeEntry(DetailLevels[0], StartTime, true);
		AddEvent(DetailLevels[0], Event);
		FDetailLevelDepthState& Lod0DepthState = DetailLevels[0].InsertionState.DepthStates[CurrentDepth];
		Lod0DepthState.EnterTime = StartTime;
		Lod0DepthState.DominatingEvent = Event;
		//Lod0DepthState.DebugDominatingEventType = Owner.EventTypes[TypeId];

		for (int32 DetailLevelIndex = 1; DetailLevelIndex < SettingsType::DetailLevelsCount; ++DetailLevelIndex)
		{
			FDetailLevel& DetailLevel = DetailLevels[DetailLevelIndex];
			FDetailLevelDepthState& CurrentDepthState = DetailLevel.InsertionState.DepthStates[CurrentDepth];

			if (CurrentDepthState.PendingScopeEnterIndex < 0 || StartTime >= CurrentDepthState.EnterTime + DetailLevel.Resolution)
			{
				if (CurrentDepthState.PendingEventIndex >= 0)
				{
					for (int32 Depth = DetailLevel.InsertionState.PendingDepth; Depth >= CurrentDepth; --Depth)
					{
						FDetailLevelDepthState& DepthState = DetailLevel.InsertionState.DepthStates[Depth];
						check(DepthState.PendingScopeEnterIndex >= 0);
						AddScopeEntry(DetailLevel, DepthState.ExitTime, false);

						DepthState.PendingScopeEnterIndex = -1;
						DepthState.PendingEventIndex = -1;
					}
				}
				DetailLevel.InsertionState.PendingDepth = CurrentDepth;

				uint64 EnterScopeIndex = DetailLevel.ScopeEntries.Num();
				uint64 EventIndex = DetailLevel.Events.Num();

				AddScopeEntry(DetailLevel, StartTime, true);
				AddEvent(DetailLevel, Event);

				CurrentDepthState.DominatingEventStartTime = StartTime;
				CurrentDepthState.DominatingEventEndTime = StartTime;
				CurrentDepthState.DominatingEventDuration = 0.0;
				CurrentDepthState.PendingScopeEnterIndex = EnterScopeIndex;
				CurrentDepthState.PendingEventIndex = EventIndex;
				CurrentDepthState.EnterTime = StartTime;
				CurrentDepthState.DominatingEvent = Event;
				//CurrentDepthState.DebugDominatingEventType = Owner.EventTypes[TypeId];
			}
			else if (CurrentDepth > DetailLevel.InsertionState.PendingDepth)
			{
				DetailLevel.InsertionState.PendingDepth = CurrentDepth;
			}
			SetEvent(DetailLevel, CurrentDepthState.PendingEventIndex, Event);
		}
		++ModCount;
	}

	void AppendEndEvent(double EndTime)
	{
		AddScopeEntry(DetailLevels[0], EndTime, false);

		int32 CurrentDepth = DetailLevels[0].InsertionState.CurrentDepth;
		for (int32 DetailLevelIndex = 1; DetailLevelIndex < SettingsType::DetailLevelsCount; ++DetailLevelIndex)
		{
			FDetailLevel& DetailLevel = DetailLevels[DetailLevelIndex];

			DetailLevel.InsertionState.DepthStates[CurrentDepth].ExitTime = EndTime;

			UpdateDominatingEvent(DetailLevel, CurrentDepth, EndTime);

			FDetailLevelDepthState& CurrentDepthState = DetailLevel.InsertionState.DepthStates[CurrentDepth];
			check(CurrentDepthState.PendingScopeEnterIndex >= 0);
			if (EndTime >= CurrentDepthState.EnterTime + DetailLevel.Resolution)
			{
				for (int32 Depth = DetailLevel.InsertionState.PendingDepth; Depth >= CurrentDepth; --Depth)
				{
					FDetailLevelDepthState& DepthState = DetailLevel.InsertionState.DepthStates[Depth];
					check(DepthState.PendingScopeEnterIndex >= 0);
					AddScopeEntry(DetailLevel, DepthState.ExitTime, false);

					DepthState.PendingScopeEnterIndex = -1;
					DepthState.PendingEventIndex = -1;
				}
				DetailLevel.InsertionState.PendingDepth = CurrentDepth - 1;
			}
		}
		++ModCount;
	}

	virtual uint64 GetModCount() const override
	{
		return ModCount;
	}

	virtual uint64 GetEventCount() const override
	{
		return DetailLevels[0].Events.Num();
	}

	virtual const EventType& GetEvent(uint64 InIndex) const override 
	{ 
		return DetailLevels[0].Events[InIndex]; 
	}

	virtual double GetStartTime() const override
	{
		return DetailLevels[0].ScopeEntries.Num() > 0 ? FMath::Abs(DetailLevels[0].ScopeEntries[0].Time) : 0.0;
	}

	virtual double GetEndTime() const override
	{
		uint64 NumScopeEntries = DetailLevels[0].ScopeEntries.Num();
		return NumScopeEntries > 0 ? FMath::Abs(DetailLevels[0].ScopeEntries[NumScopeEntries - 1].Time) : 0.0;
	}

	virtual void EnumerateEventsDownSampled(double IntervalStart, double IntervalEnd, double Resolution, typename ITimeline<EventType>::EventCallback Callback) const override
	{
		int32 DetailLevelIndex = SettingsType::DetailLevelsCount - 1;
		for (; DetailLevelIndex > 0; --DetailLevelIndex)
		{
			if (DetailLevels[DetailLevelIndex].Resolution <= Resolution)
			{
				break;
			}
		}

		const FDetailLevel& DetailLevel = DetailLevels[DetailLevelIndex];
		if (DetailLevel.ScopeEntries.Num() == 0)
		{
			return;
		}

		uint64 FirstScopePageIndex = Algo::UpperBoundBy(DetailLevel.ScopeEntries, IntervalStart, [](const FEventScopeEntryPage& Page)
		{
			return Page.BeginTime;
		});
		if (FirstScopePageIndex > 0)
		{
			--FirstScopePageIndex;
		}
		auto ScopeEntryIterator = DetailLevel.ScopeEntries.GetIteratorFromPage(FirstScopePageIndex);
		const FEventScopeEntryPage* ScopePage = ScopeEntryIterator.GetCurrentPage();
		if (ScopePage->BeginTime > IntervalEnd)
		{
			return;
		}
		if (ScopePage->EndTime < IntervalStart)
		{
			return;
		}
		auto EventsIterator = DetailLevel.Events.GetIteratorFromItem(ScopePage->BeginEventIndex);
		struct FEnumerationStackEntry
		{
			double StartTime;
			EventType Event;
		};
		FEnumerationStackEntry EventStack[SettingsType::MaxDepth];
		int32 CurrentStackDepth = ScopePage->InitialStackCount;
		for (int32 InitialStackIndex = 0; InitialStackIndex < CurrentStackDepth; ++InitialStackIndex)
		{
			FEnumerationStackEntry& EnumerationStackEntry = EventStack[InitialStackIndex];
			const FEventStackEntry& EventStackEntry = ScopePage->InitialStack[InitialStackIndex];
			EnumerationStackEntry.StartTime = GetScopeEntryTime(DetailLevel, EventStackEntry.EnterScopeIndex);
			EnumerationStackEntry.Event = GetEvent(DetailLevel, EventStackEntry.EventIndex);
		}

		const FEventScopeEntry* ScopeEntry = ScopeEntryIterator.GetCurrentItem();
		const EventType* Event = EventsIterator.GetCurrentItem();
		while (ScopeEntry && FMath::Abs(ScopeEntry->Time) < IntervalStart)
		{
			if (ScopeEntry->Time < 0.0)
			{
				check(CurrentStackDepth < SettingsType::MaxDepth);
				FEnumerationStackEntry& StackEntry = EventStack[CurrentStackDepth++];
				StackEntry.Event = *Event;
				StackEntry.StartTime = -ScopeEntry->Time;
				Event = EventsIterator.NextItem();
			}
			else
			{
				check(CurrentStackDepth > 0);
				--CurrentStackDepth;
			}
			ScopeEntry = ScopeEntryIterator.NextItem();
		}
		if (CurrentStackDepth == 1 && EventStack[0].StartTime > IntervalEnd)
		{
			return;
		}
		for (int32 StackIndex = 0; StackIndex < CurrentStackDepth; ++StackIndex)
		{
			FEnumerationStackEntry& StackEntry = EventStack[StackIndex];
			if (Callback(true, StackEntry.StartTime, StackEntry.Event) == EEventEnumerate::Stop)
			{
				return;
			}
		}
		while (ScopeEntry && FMath::Abs(ScopeEntry->Time) <= IntervalEnd)
		{
			if (ScopeEntry->Time < 0.0)
			{
				check(CurrentStackDepth < SettingsType::MaxDepth);
				FEnumerationStackEntry& StackEntry = EventStack[CurrentStackDepth++];
				StackEntry.Event = *Event;
				if (Callback(true, -ScopeEntry->Time, StackEntry.Event) == EEventEnumerate::Stop)
				{
					return;
				}
				Event = EventsIterator.NextItem();
			}
			else
			{
				check(CurrentStackDepth > 0);
				FEnumerationStackEntry& StackEntry = EventStack[--CurrentStackDepth];
				if (Callback(false, ScopeEntry->Time, StackEntry.Event) == EEventEnumerate::Stop)
				{
					return;
				}
			}
			ScopeEntry = ScopeEntryIterator.NextItem();
		}

		bool bSearchEndTimeUsingPages = false;
		uint64 LastPageIndex = ScopeEntryIterator.GetCurrentPageIndex();
		uint32 ExitDepth = 0;
		while (CurrentStackDepth > 0 && ScopeEntry)
		{
			if (ScopeEntryIterator.GetCurrentPageIndex() != LastPageIndex)
			{
				bSearchEndTimeUsingPages = true;
				break;
			}
			if (ScopeEntry->Time < 0.0)
			{
				++ExitDepth;
			}
			else
			{
				if (ExitDepth == 0)
				{
					FEnumerationStackEntry& StackEntry = EventStack[--CurrentStackDepth];
					if (Callback(false, ScopeEntry->Time, StackEntry.Event) == EEventEnumerate::Stop)
					{
						return;
					}
				}
				else
				{
					--ExitDepth;
				}
			}

			LastPageIndex = ScopeEntryIterator.GetCurrentPageIndex();
			ScopeEntry = ScopeEntryIterator.NextItem();
		}

		if (bSearchEndTimeUsingPages)
		{
			do
			{
				const FEventScopeEntryPage* CurrentScopePage = ScopeEntryIterator.GetCurrentPage();
				check(CurrentStackDepth <= CurrentScopePage->InitialStackCount);

				while (CurrentStackDepth > 0 && CurrentScopePage->InitialStack[CurrentStackDepth - 1].EndTime > 0)
				{
					--CurrentStackDepth;
					EventType CurrentEvent = GetEvent(DetailLevel, CurrentScopePage->InitialStack[CurrentStackDepth].EventIndex);
					if (Callback(false, CurrentScopePage->InitialStack[CurrentStackDepth].EndTime, CurrentEvent) == EEventEnumerate::Stop)
					{
						return;
					}
				}
			} while (ScopeEntryIterator.NextPage());
		}

		while (CurrentStackDepth > 0)
		{
			FEnumerationStackEntry& StackEntry = EventStack[--CurrentStackDepth];
			if (Callback(false, DetailLevel.InsertionState.LastTime, StackEntry.Event) == EEventEnumerate::Stop)
			{
				return;
			}
		}
	}

	virtual void EnumerateEventsDownSampled(double IntervalStart, double IntervalEnd, double Resolution, typename ITimeline<EventType>::EventRangeCallback Callback) const override
	{
		struct FStackEntry
		{
			uint64 LocalEventIndex;
		};
		FStackEntry EventStack[SettingsType::MaxDepth];
		uint32 CurrentDepth = 0;

		struct FOutputEvent
		{
			double StartTime;
			double EndTime;
			uint32 Depth;
			EventType Event;
		};
		TArray<FOutputEvent> OutputEvents;

		EnumerateEventsDownSampled(IntervalStart, IntervalEnd, Resolution, [&EventStack, &OutputEvents, &CurrentDepth, Callback](bool IsEnter, double Time, const EventType& Event)
		{
			if (IsEnter)
			{
				FStackEntry& StackEntry = EventStack[CurrentDepth];
				StackEntry.LocalEventIndex = OutputEvents.Num();
				FOutputEvent& OutputEvent = OutputEvents.AddDefaulted_GetRef();
				OutputEvent.StartTime = Time;
				OutputEvent.EndTime = Time;
				OutputEvent.Depth = CurrentDepth;
				OutputEvent.Event = Event;
				++CurrentDepth;
			}
			else
			{
				{
					FStackEntry& StackEntry = EventStack[--CurrentDepth];
					FOutputEvent* OutputEvent = OutputEvents.GetData() + StackEntry.LocalEventIndex;
					OutputEvent->EndTime = Time;
				}
				if (CurrentDepth == 0)
				{
					for (FOutputEvent& OutputEvent : OutputEvents)
					{
						EEventEnumerate Ret = Callback(OutputEvent.StartTime, OutputEvent.EndTime, OutputEvent.Depth, OutputEvent.Event);
						if (Ret != EEventEnumerate::Continue)
						{
							return Ret;
						}
					}
					OutputEvents.Empty(OutputEvents.Num());
				}
			}

			return EEventEnumerate::Continue;
		});
	}

	virtual void EnumerateEvents(double IntervalStart, double IntervalEnd, typename ITimeline<EventType>::EventCallback Callback) const override
	{
		EnumerateEventsDownSampled(IntervalStart, IntervalEnd, 0.0, Callback);
	}

	virtual void EnumerateEvents(double IntervalStart, double IntervalEnd, typename ITimeline<EventType>::EventRangeCallback Callback) const override
	{
		EnumerateEventsDownSampled(IntervalStart, IntervalEnd, 0.0, Callback);
	}

	virtual bool GetEventInfo(double InTime, double DeltaTime, int32 Depth, typename ITimeline<InEventType>::FTimelineEventInfo& EventInfo) const override
	{
		if (Depth >= SettingsType::MaxDepth || Depth < 0)
		{
			return false;
		}

		const FDetailLevel& DetailLevel = DetailLevels[0];

		if (DetailLevel.ScopeEntries.Num() == 0)
		{
			return false;
		}

		if (DetailLevel.InsertionState.LastTime < InTime - DeltaTime)
		{
			return false;
		}

		uint64 FirstScopePageIndex = Algo::UpperBoundBy(DetailLevel.ScopeEntries, InTime, [](const FEventScopeEntryPage& Page)
			{
				return Page.BeginTime;
			});

		if (FirstScopePageIndex > 0)
		{
			--FirstScopePageIndex;
		}

		auto ScopeEntryIterator = DetailLevel.ScopeEntries.GetIteratorFromPage(FirstScopePageIndex);

		FEventStackEntry OutScopeEntry; 
		bool bIsFound = FindEventUsingPageInitialStack(ScopeEntryIterator, InTime, DeltaTime, Depth, DetailLevel, OutScopeEntry);
		if (bIsFound)
		{
			EventInfo.StartTime = GetScopeEntryTime(DetailLevel, OutScopeEntry.EnterScopeIndex);
			EventInfo.ExclTime = OutScopeEntry.ExclTime;
			EventInfo.EndTime = OutScopeEntry.EndTime;
			EventInfo.Event = GetEvent(DetailLevel, OutScopeEntry.EventIndex);

			if (EventInfo.EndTime < 0)
			{
				//The end of the event has not been reached by analysis
				EventInfo.EndTime = DetailLevel.InsertionState.LastTime;
			}

			return true;
		}

		const FEventScopeEntryPage* ScopePage = ScopeEntryIterator.GetCurrentPage();
		double IntervalStart = FMath::Max(InTime - DeltaTime, 0.0);

		while (ScopePage->BeginTime > IntervalStart && FirstScopePageIndex > 0)
		{
			--FirstScopePageIndex;
			ScopeEntryIterator = DetailLevel.ScopeEntries.GetIteratorFromPage(FirstScopePageIndex);
			ScopePage = ScopeEntryIterator.GetCurrentPage();
		}

		auto EventsIterator = DetailLevel.Events.GetIteratorFromItem(ScopePage->BeginEventIndex);
		FEventInfoStackEntry EventStack[SettingsType::MaxDepth];

		int32 CurrentStackDepth = ScopePage->InitialStackCount;
		for (int32 InitialStackIndex = 0; InitialStackIndex < CurrentStackDepth; ++InitialStackIndex)
		{
			EventStack[InitialStackIndex] = FEventInfoStackEntry(ScopePage->InitialStack[InitialStackIndex], DetailLevel);
		}

		const FEventScopeEntry* ScopeEntry = ScopeEntryIterator.GetCurrentItem();
		const EventType* Event = EventsIterator.GetCurrentItem();
		double LastTime = 0.0;
		auto LastTimeIterator = ScopeEntryIterator;
		if (LastTimeIterator.PrevItem())
		{
			LastTime = abs(LastTimeIterator.GetCurrentItem()->Time);
		}

		//Iterate from the start of the page to the start time of our interval
		while (ScopeEntry && FMath::Abs(ScopeEntry->Time) <= IntervalStart)
		{
			if (ScopeEntry->Time < 0.0)
			{
				check(CurrentStackDepth < SettingsType::MaxDepth);
				FEventInfoStackEntry& StackEntry = EventStack[CurrentStackDepth++];
				StackEntry.Event = *Event;
				StackEntry.StartTime = -ScopeEntry->Time;
				StackEntry.ExclTime = 0.0;
				if (CurrentStackDepth > 1)
				{
					FEventInfoStackEntry& ParentStackEntry = EventStack[CurrentStackDepth - 2];
					ParentStackEntry.ExclTime += StackEntry.StartTime - LastTime;
				}
				Event = EventsIterator.NextItem();
				LastTime = -ScopeEntry->Time;
			}
			else
			{
				check(CurrentStackDepth > 0);
				--CurrentStackDepth;

				if (CurrentStackDepth == Depth)
				{
					FEventInfoStackEntry& StackEntry = EventStack[CurrentStackDepth];
					StackEntry.ExclTime += ScopeEntry->Time - LastTime;
				}
				LastTime = ScopeEntry->Time;
			}

			ScopeEntry = ScopeEntryIterator.NextItem();
		}

		//Check if we have an event between InTime - InDeltaTime and InTime + InDeltaTime
		FFindBestMatchEventInParams InParams;
		InParams.IterationState.ScopeIterator = ScopeEntryIterator;
		InParams.IterationState.EventsIterator = EventsIterator;
		InParams.IterationState.EventStack = EventStack;
		InParams.IterationState.StackDepth = CurrentStackDepth;
		InParams.IterationState.LastIterationTime = LastTime;

		InParams.TargetExactTime = InTime;
		InParams.TargetEndTime = InTime + DeltaTime;
		InParams.TargetDepth = Depth;

		FFindBestMatchEventOutParams OutParams;

		bool bMatchFound = FindBestMatchEvent(InParams, OutParams);
		if (!bMatchFound)
		{
			return false;
		}

		bool bMatchEventStartsInsidePage = true;
		if (ScopePage->InitialStackCount > Depth)
		{
			double StartTime = GetScopeEntryTime(DetailLevel, ScopePage->InitialStack[Depth].EnterScopeIndex);
			if (StartTime == OutParams.EventInfo.StartTime)
			{
				bMatchEventStartsInsidePage = false;
			}
		}

		//If we have found both start time and end time for our event, we can return the result
		if (OutParams.bHasEndTime && bMatchEventStartsInsidePage)
		{
			EventInfo.StartTime = OutParams.EventInfo.StartTime;
			EventInfo.EndTime = OutParams.EndTime;
			EventInfo.ExclTime = OutParams.EventInfo.ExclTime;
			EventInfo.Event = OutParams.EventInfo.Event;

			if (EventInfo.EndTime < 0)
			{
				//The end of the event has not been reached by analysis
				EventInfo.EndTime = DetailLevel.InsertionState.LastTime;
			}

			return true;
		}

		//We continue searching for the end scope event
		ScopeEntryIterator = OutParams.IterationState.ScopeIterator;
		EventsIterator = OutParams.IterationState.EventsIterator;
		CurrentStackDepth = OutParams.IterationState.StackDepth;
		LastTime = OutParams.IterationState.LastIterationTime;

		FEventInfoStackEntry TargetEntry = OutParams.EventInfo;

		auto ScopeEntryIteratorAtEvent = ScopeEntryIterator;

		//We find the page where the target event ends
		ScopeEntryIterator = DetailLevel.ScopeEntries.GetIteratorFromPage(FirstScopePageIndex);
		auto EventLastPageIterator = ScopeEntryIterator;
		
		while (ScopeEntryIterator.NextPage() != nullptr)
		{
			const FEventScopeEntryPage* CurrentScopePage = ScopeEntryIterator.GetCurrentPage();

			if (CurrentScopePage->InitialStackCount <= Depth)
			{
				break;
			}
			double StartTime = GetScopeEntryTime(DetailLevel, CurrentScopePage->InitialStack[Depth].EnterScopeIndex);

			if (StartTime != TargetEntry.StartTime)
			{
				break;
			}

			EventLastPageIterator = ScopeEntryIterator;
		}

		if (EventLastPageIterator.GetCurrentPageIndex() != FirstScopePageIndex || !bMatchEventStartsInsidePage)
		{
			//If the end scope event is on a different page than the start scope one we can get the event info from the InitialStack of the last page
			ScopeEntryIterator = EventLastPageIterator;
			ScopePage = ScopeEntryIterator.GetCurrentPage();

			check(ScopePage->InitialStackCount > Depth);
			FEventStackEntry& TargetStackEntry = ScopePage->InitialStack[Depth];
			EventInfo.StartTime = GetScopeEntryTime(DetailLevel, TargetStackEntry.EnterScopeIndex);
			EventInfo.EndTime = TargetStackEntry.EndTime;
			EventInfo.ExclTime = TargetStackEntry.ExclTime;
			EventInfo.Event = GetEvent(DetailLevel, TargetStackEntry.EventIndex);

			if (EventInfo.EndTime < 0)
			{
				//The end of the event has not been reached by analysis
				EventInfo.EndTime = DetailLevel.InsertionState.LastTime;
			}
			
			return true;
		}
		else
		{
			//If the end scope event is in the same page, we continue iterating from the point where FindBestMatchEvent stopped
			ScopeEntryIterator = ScopeEntryIteratorAtEvent;
			ScopeEntry = ScopeEntryIterator.GetCurrentItem();
		}

		while (ScopeEntry && 
			   Depth < CurrentStackDepth)
		{
			if (ScopeEntry->Time < 0.0)
			{
				check(CurrentStackDepth < SettingsType::MaxDepth);
				FEventInfoStackEntry& StackEntry = EventStack[CurrentStackDepth++];
				StackEntry.Event = *Event;
				StackEntry.StartTime = -ScopeEntry->Time;
				if (CurrentStackDepth > 1)
				{
					FEventInfoStackEntry& ParentStackEntry = EventStack[CurrentStackDepth - 2];
					ParentStackEntry.ExclTime += StackEntry.StartTime - LastTime;
				}
				Event = EventsIterator.NextItem();
				LastTime = -ScopeEntry->Time;
			}
			else
			{
				check(CurrentStackDepth > 0);
				--CurrentStackDepth;
				if (CurrentStackDepth == Depth)
				{
					FEventInfoStackEntry& StackEntry = EventStack[CurrentStackDepth];
					StackEntry.ExclTime += ScopeEntry->Time - LastTime;
				}

				LastTime = ScopeEntry->Time;
			}
			ScopeEntry = ScopeEntryIterator.NextItem();
		}

		TargetEntry = EventStack[Depth];

		EventInfo.StartTime = TargetEntry.StartTime;
		EventInfo.EndTime = LastTime;
		EventInfo.ExclTime = TargetEntry.ExclTime;
		EventInfo.Event = TargetEntry.Event;

		return true;
	}

private:
	struct FEventScopeEntry
	{
		double Time;
		//uint32 DebugDepth;
	};

	struct FEventStackEntry
	{
		uint64 EnterScopeIndex;
		uint64 EventIndex;
		double ExclTime = 0.0;
		double EndTime = -1.0; //By convention a negative EndTime means the event does not end in the current page
	};

	struct FEventScopeEntryPage
	{
		FEventScopeEntry* Items = nullptr;
		uint64 Count = 0;
		double BeginTime = 0.0;
		double EndTime = 0.0;
		uint64 BeginEventIndex = 0;
		uint64 EndEventIndex = 0;
		FEventStackEntry* InitialStack = nullptr;
		uint16 InitialStackCount = 0;
	};

	struct FDetailLevelDepthState
	{
		int64 PendingScopeEnterIndex = -1;
		int64 PendingEventIndex = -1;

		EventType DominatingEvent;
		double DominatingEventStartTime = 0.0;
		double DominatingEventEndTime = 0.0;
		double DominatingEventDuration = 0.0;

		double EnterTime = 0.0;
		double ExitTime = 0.0;

		//const FTimelineEventType* DebugDominatingEventType;
	};

	struct FDetailLevelInsertionState
	{
		double LastTime = -1.0;
		uint16 CurrentDepth = 0;
		int32 PendingDepth = -1;
		FDetailLevelDepthState DepthStates[SettingsType::MaxDepth];
		FEventStackEntry EventStack[SettingsType::MaxDepth];
		uint64 CurrentScopeEntryPageIndex = (uint64) -1;
	};

	struct FDetailLevel
	{
		FDetailLevel(ILinearAllocator& Allocator, double InResolution)
			: Resolution(InResolution)
			, ScopeEntries(Allocator, SettingsType::ScopeEntriesPageSize)
			, Events(Allocator, SettingsType::EventsPageSize)
		{

		}

		double Resolution;
		TPagedArray<FEventScopeEntry, FEventScopeEntryPage> ScopeEntries;
		TPagedArray<EventType> Events;

		FDetailLevelInsertionState InsertionState;
	};

	struct FEventInfoStackEntry
	{
		FEventInfoStackEntry() = default;
		FEventInfoStackEntry(const FEventStackEntry& EventStackEntry, const FDetailLevel& DetailLevel)
		{
			StartTime = GetScopeEntryTime(DetailLevel, EventStackEntry.EnterScopeIndex);
			Event = GetEvent(DetailLevel, EventStackEntry.EventIndex);
			ExclTime = EventStackEntry.ExclTime;
		}

		EventType Event;
		double StartTime = 0.0;
		double ExclTime = 0.0;
	};

	struct FIterationState
	{
		typename TPagedArray<FEventScopeEntry, FEventScopeEntryPage>::TIterator ScopeIterator;
		typename TPagedArray<EventType>::TIterator EventsIterator;
		FEventInfoStackEntry* EventStack;
		int32 StackDepth;
		double LastIterationTime;
	};

	struct FFindBestMatchEventInParams
	{
		FIterationState IterationState;

		double TargetExactTime;
		double TargetEndTime;
		int32 TargetDepth;
	};

	struct FFindBestMatchEventOutParams
	{
		FIterationState IterationState;

		FEventInfoStackEntry EventInfo;
		bool bHasEndTime;
		double EndTime;
	};

	void UpdateDominatingEvent(FDetailLevel& DetailLevel, int32 Depth, double CurrentTime)
	{
		FDetailLevelDepthState& Lod0DepthState = DetailLevels[0].InsertionState.DepthStates[Depth];
		double Lod0EventDuration = CurrentTime - Lod0DepthState.EnterTime;
		FDetailLevelDepthState& CurrentDepthState = DetailLevel.InsertionState.DepthStates[Depth];
		if (Lod0EventDuration > CurrentDepthState.DominatingEventDuration)
		{
			check(CurrentDepthState.PendingScopeEnterIndex >= 0);
			check(CurrentDepthState.PendingEventIndex >= 0);

			CurrentDepthState.DominatingEvent = Lod0DepthState.DominatingEvent;
			CurrentDepthState.DominatingEventStartTime = Lod0DepthState.EnterTime;
			CurrentDepthState.DominatingEventEndTime = CurrentTime;
			CurrentDepthState.DominatingEventDuration = Lod0EventDuration;

			SetEvent(DetailLevel, CurrentDepthState.PendingEventIndex, CurrentDepthState.DominatingEvent);

			//CurrentDepthState.DebugDominatingEventType = Owner.EventTypes[CurrentDepthState.DominatingEventType];
		}
	}

	void AddScopeEntry(FDetailLevel& DetailLevel, double Time, bool IsEnter)
	{
		check(Time >= DetailLevel.InsertionState.LastTime);

		uint64 EventIndex = DetailLevel.Events.Num();
		uint64 ScopeIndex = DetailLevel.ScopeEntries.Num();

		FEventScopeEntry& ScopeEntry = DetailLevel.ScopeEntries.PushBack();
		ScopeEntry.Time = IsEnter ? -Time : Time;
		FEventScopeEntryPage* LastPage = DetailLevel.ScopeEntries.GetLastPage();
		uint64 LastPageIndex = DetailLevel.ScopeEntries.NumPages() - 1;
		if (LastPageIndex != DetailLevel.InsertionState.CurrentScopeEntryPageIndex)
		{
			// At the very first call, CurrentScopeEntryPage will be -1
			if (DetailLevel.InsertionState.CurrentScopeEntryPageIndex != (uint64)-1)
			{
				FEventScopeEntryPage* CurrentScopeEntryPage = DetailLevel.ScopeEntries.GetPage(DetailLevel.InsertionState.CurrentScopeEntryPageIndex);
				int32 PreviousPageInitialStackCount = CurrentScopeEntryPage->InitialStackCount;
				int32 CurrentDepth = DetailLevel.InsertionState.CurrentDepth;
				// Update the open scopes that were also open at the beginning of the last page so the values 
				// represent stats up to and including the current page
				int ii = 0;
				for (; ii < PreviousPageInitialStackCount && ii < CurrentDepth; ++ii)
				{
					FEventStackEntry& InsertionStateStackEntry = DetailLevel.InsertionState.EventStack[ii];
					FEventStackEntry& PreviousPageStackEntry = CurrentScopeEntryPage->InitialStack[ii];

					if (InsertionStateStackEntry.EnterScopeIndex == PreviousPageStackEntry.EnterScopeIndex
						&& InsertionStateStackEntry.EventIndex == PreviousPageStackEntry.EventIndex)
					{
						PreviousPageStackEntry.ExclTime = InsertionStateStackEntry.ExclTime;
					}
					else
					{
						break;
					}
				}
			}

			DetailLevel.InsertionState.CurrentScopeEntryPageIndex = LastPageIndex;
			LastPage->BeginTime = Time;
			LastPage->BeginEventIndex = DetailLevel.Events.Num();
			LastPage->EndEventIndex = LastPage->BeginEventIndex;
			LastPage->InitialStackCount = DetailLevel.InsertionState.CurrentDepth;
			if (LastPage->InitialStackCount)
			{
				LastPage->InitialStack = reinterpret_cast<FEventStackEntry*>(Allocator.Allocate(LastPage->InitialStackCount * sizeof(FEventStackEntry)));
				memcpy(LastPage->InitialStack, DetailLevel.InsertionState.EventStack, LastPage->InitialStackCount * sizeof(FEventStackEntry));
			}
		}
		LastPage->EndTime = Time;

		if (IsEnter)
		{
			if (DetailLevel.InsertionState.CurrentDepth < SettingsType::MaxDepth)
			{
				FEventStackEntry& StackEntry = DetailLevel.InsertionState.EventStack[DetailLevel.InsertionState.CurrentDepth++];
				StackEntry.EventIndex = EventIndex;
				StackEntry.EnterScopeIndex = ScopeIndex;
				StackEntry.ExclTime = 0.0;
				StackEntry.EndTime = -1.0;

				if (DetailLevel.InsertionState.CurrentDepth > 1)
				{
					FEventStackEntry& ParentStackEntry = DetailLevel.InsertionState.EventStack[DetailLevel.InsertionState.CurrentDepth - 2];
					ParentStackEntry.ExclTime += Time - DetailLevel.InsertionState.LastTime;
				}
			}
		}
		else
		{
			check(DetailLevel.InsertionState.CurrentDepth > 0);
			--DetailLevel.InsertionState.CurrentDepth;

			FEventStackEntry& StackEntry = DetailLevel.InsertionState.EventStack[DetailLevel.InsertionState.CurrentDepth];
			StackEntry.ExclTime += Time - DetailLevel.InsertionState.LastTime;

			FEventScopeEntryPage* CurrentScopeEntryPage = DetailLevel.ScopeEntries.GetPage(DetailLevel.InsertionState.CurrentScopeEntryPageIndex);
			if (DetailLevel.InsertionState.CurrentDepth < CurrentScopeEntryPage->InitialStackCount)
			{
				FEventStackEntry& PreviousPageStackEntry = CurrentScopeEntryPage->InitialStack[DetailLevel.InsertionState.CurrentDepth];

				if (StackEntry.EnterScopeIndex == PreviousPageStackEntry.EnterScopeIndex
					&& StackEntry.EventIndex == PreviousPageStackEntry.EventIndex)
				{
					PreviousPageStackEntry.ExclTime = StackEntry.ExclTime;
					PreviousPageStackEntry.EndTime = Time;
				}
			}
		}

		DetailLevel.InsertionState.LastTime = Time;

		//ScopeEntry.DebugDepth = DetailLevel.InsertionState.CurrentDepth;
	}

	void AddEvent(FDetailLevel& DetailLevel, const EventType& Event)
	{
		FEventScopeEntryPage* CurrentScopeEntryPage = DetailLevel.ScopeEntries.GetPage(DetailLevel.InsertionState.CurrentScopeEntryPageIndex);
		++CurrentScopeEntryPage->EndEventIndex;
		DetailLevel.Events.PushBack() = Event;

		//Event.DebugType = Owner.EventTypes[TypeIndex];
	}

	static double GetScopeEntryTime(const FDetailLevel& DetailLevel, uint64 Index)
	{
		const FEventScopeEntry& ScopeEntry = DetailLevel.ScopeEntries[Index];
		return ScopeEntry.Time < 0 ? -ScopeEntry.Time : ScopeEntry.Time;
	}

	static void SetEvent(FDetailLevel& DetailLevel, uint64 Index, const EventType& Event)
	{
		DetailLevel.Events[Index] = Event;
	}

	static EventType GetEvent(const FDetailLevel& DetailLevel, uint64 Index)
	{
		return DetailLevel.Events[Index];
	}

	bool FindEventUsingPageInitialStack(typename TPagedArray<FEventScopeEntry, FEventScopeEntryPage>::TIterator ScopeEntryIterator, 
										double Time, 
										double DeltaTime, 
										int32 Depth, 
										const FDetailLevel& DetailLevel,
										FEventStackEntry& OutPageStackEntry) const
	{
		const FEventScopeEntryPage* CurrentScopePage = ScopeEntryIterator.GetCurrentPage();
		bool bIsInCurrentPageInitStack = false;

		if (CurrentScopePage->InitialStackCount > Depth)
		{
			FEventStackEntry& CurrentPageStackEntry = CurrentScopePage->InitialStack[Depth];
			if (CurrentPageStackEntry.EndTime < 0 || CurrentPageStackEntry.EndTime > Time)
			{
				bIsInCurrentPageInitStack = true;
			}
		}

		const FEventScopeEntryPage* NextScopePage = nullptr;
		if (!bIsInCurrentPageInitStack && ScopeEntryIterator.NextPage() != nullptr)
		{
			NextScopePage = ScopeEntryIterator.GetCurrentPage();
			if (NextScopePage->InitialStackCount > Depth)
			{
				FEventStackEntry& NextPageStackEntry = NextScopePage->InitialStack[Depth];
				double StartTime = GetScopeEntryTime(DetailLevel, NextPageStackEntry.EnterScopeIndex);
				if (StartTime < Time)
				{
					CurrentScopePage = NextScopePage;
					bIsInCurrentPageInitStack = true;
				}
			}
		}

		if (!bIsInCurrentPageInitStack)
		{
			return false;
		}

		while (ScopeEntryIterator.NextPage() != nullptr && CurrentScopePage->InitialStack[Depth].EndTime < 0)
		{
			CurrentScopePage = ScopeEntryIterator.GetCurrentPage();
		}

		OutPageStackEntry = CurrentScopePage->InitialStack[Depth];

		return true;
	}

	bool FindBestMatchEvent(const FFindBestMatchEventInParams &InParams, FFindBestMatchEventOutParams &OutParams) const
	{
		FIterationState IterationState = InParams.IterationState;
		const FEventScopeEntry* ScopeEntry = IterationState.ScopeIterator.GetCurrentItem();
		const EventType* Event = IterationState.EventsIterator.GetCurrentItem();

		FEventInfoStackEntry BestMatchEntry;
		double BestMatchEndTime = 0.0;
		bool bHasEndEvent = false;

		//In the first step, we iterate up to TargetExactTime, storing the last event with our target depth that ended during iteration
		//......]...]....]...TargetExactTime
		while (ScopeEntry && FMath::Abs(ScopeEntry->Time) <= InParams.TargetExactTime)
		{
			if (ScopeEntry->Time < 0.0)
			{
				check(IterationState.StackDepth < SettingsType::MaxDepth);
				FEventInfoStackEntry& StackEntry = IterationState.EventStack[IterationState.StackDepth++];
				StackEntry.Event = *Event;
				StackEntry.StartTime = -ScopeEntry->Time;
				StackEntry.ExclTime = 0.0;
				if (IterationState.StackDepth > 1)
				{
					FEventInfoStackEntry& ParentStackEntry = IterationState.EventStack[IterationState.StackDepth - 2];
					ParentStackEntry.ExclTime += StackEntry.StartTime - IterationState.LastIterationTime;
				}
				Event = IterationState.EventsIterator.NextItem();
				IterationState.LastIterationTime = -ScopeEntry->Time;
			}
			else
			{
				check(IterationState.StackDepth > 0);
				--IterationState.StackDepth;

				if (IterationState.StackDepth == InParams.TargetDepth)
				{
					FEventInfoStackEntry& StackEntry = IterationState.EventStack[IterationState.StackDepth];
					StackEntry.ExclTime += ScopeEntry->Time - IterationState.LastIterationTime;
					BestMatchEndTime = ScopeEntry->Time;
					BestMatchEntry = StackEntry;
					bHasEndEvent = true;
				}
				IterationState.LastIterationTime = ScopeEntry->Time;
			}

			ScopeEntry = IterationState.ScopeIterator.NextItem();
		}

		//If the iteration stack depth is as deep as our target depth, that we have a perfect match,
		//an event that is ongoing at TargetExactTime, so we just return it
		//....[..TargetExactTime.....]
		//The strict '>' comparison is needed because CurrentStackDepth is "RealDepth + 1"
		if (IterationState.StackDepth > InParams.TargetDepth)
		{
			OutParams.EventInfo = IterationState.EventStack[InParams.TargetDepth];
			OutParams.bHasEndTime = false;
			OutParams.IterationState = IterationState;

			return true;
		}

		//We continue iterating until TargetEndTime or until we find the start of an event with the target depth
		//TargetExactTime.....[
		bool bHasStartEvent = false;
		while (ScopeEntry && FMath::Abs(ScopeEntry->Time) <= InParams.TargetEndTime)
		{
			if (ScopeEntry->Time < 0.0)
			{
				check(IterationState.StackDepth < SettingsType::MaxDepth);
				FEventInfoStackEntry& StackEntry = IterationState.EventStack[IterationState.StackDepth++];
				StackEntry.Event = *Event;
				StackEntry.StartTime = -ScopeEntry->Time;
				StackEntry.ExclTime = 0.0;
				if (IterationState.StackDepth > 1)
				{
					FEventInfoStackEntry& ParentStackEntry = IterationState.EventStack[IterationState.StackDepth - 2];
					ParentStackEntry.ExclTime += StackEntry.StartTime - IterationState.LastIterationTime;
				}

				Event = IterationState.EventsIterator.NextItem();
				IterationState.LastIterationTime = -ScopeEntry->Time;

				if (IterationState.StackDepth == InParams.TargetDepth + 1)
				{
					bHasStartEvent = true;
					ScopeEntry = IterationState.ScopeIterator.NextItem();
					break;
				}
			}
			else
			{
				check(IterationState.StackDepth > 0);
				--IterationState.StackDepth;

				if (IterationState.StackDepth == InParams.TargetDepth)
				{
					FEventInfoStackEntry& StackEntry = IterationState.EventStack[IterationState.StackDepth];
					StackEntry.ExclTime += ScopeEntry->Time - IterationState.LastIterationTime;
				}
				IterationState.LastIterationTime = ScopeEntry->Time;
			}

			ScopeEntry = IterationState.ScopeIterator.NextItem();
		}

		if (bHasStartEvent == false && bHasEndEvent == false)
		{
			return false;
		}

		//We choose the event that is closer to TargetExactTime
		if (bHasStartEvent)
		{
			double EndTimeDelta = InParams.TargetExactTime - BestMatchEndTime;
			double StartTimeDelta = IterationState.LastIterationTime - InParams.TargetExactTime;
			if (!bHasEndEvent || StartTimeDelta < EndTimeDelta)
			{
				//The event that just started is the best match
				bHasEndEvent = false;
				BestMatchEntry = IterationState.EventStack[InParams.TargetDepth];
			}
		}

		OutParams.EventInfo = BestMatchEntry;
		OutParams.bHasEndTime = bHasEndEvent;
		if (bHasEndEvent)
		{
			OutParams.EndTime = BestMatchEndTime;
		}

		OutParams.IterationState = IterationState;
		return true;
	}

	ILinearAllocator& Allocator;
	TArray<FDetailLevel> DetailLevels;
	uint64 ModCount = 0;
};

}