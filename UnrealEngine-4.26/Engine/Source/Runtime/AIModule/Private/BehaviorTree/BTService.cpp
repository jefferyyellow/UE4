// Copyright Epic Games, Inc. All Rights Reserved.

#include "BehaviorTree/BTService.h"

UBTService::UBTService(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	bNotifyTick = true;
	bNotifyOnSearch = true;
	bTickIntervals = true;
	bCallTickOnSearchStart = false;
	bRestartTimerOnEachActivation = false;

	Interval = 0.5f;
	RandomDeviation = 0.1f;
}

// tick节点，更新下一个tick间隔
void UBTService::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	ScheduleNextTick(OwnerComp, NodeMemory);
}

void UBTService::OnSearchStart(FBehaviorTreeSearchData& SearchData)
{
	// empty in base class
}

void UBTService::NotifyParentActivation(FBehaviorTreeSearchData& SearchData)
{
	if (bNotifyOnSearch || bNotifyTick)
	{
		// 得到实例
		UBTNode* NodeOb = bCreateNodeInstance ? GetNodeInstance(SearchData) : this;
		if (NodeOb)
		{
			UBTService* ServiceNodeOb = (UBTService*)NodeOb;
			uint8* NodeMemory = GetNodeMemory<uint8>(SearchData);

			if (bNotifyTick)
			{
				// 
				const float RemainingTime = bRestartTimerOnEachActivation ? 0.0f : GetNextTickRemainingTime(NodeMemory);
				if (RemainingTime <= 0.0f)
				{
					ServiceNodeOb->ScheduleNextTick(SearchData.OwnerComp, NodeMemory);
				}
			}

			if (bNotifyOnSearch)
			{
				ServiceNodeOb->OnSearchStart(SearchData);
			}

			if (bCallTickOnSearchStart)
			{
				ServiceNodeOb->TickNode(SearchData.OwnerComp, NodeMemory, 0.0f);
			}
		}
	}
}

// 得到tick间隔的描述
FString UBTService::GetStaticTickIntervalDescription() const
{
	FString IntervalDesc = (RandomDeviation > 0.0f) ?
		FString::Printf(TEXT("%.2fs..%.2fs"), FMath::Max(0.0f, Interval - RandomDeviation), (Interval + RandomDeviation)) :
		FString::Printf(TEXT("%.2fs"), Interval);

	return FString::Printf(TEXT("tick every %s"), *IntervalDesc);
}

// 服务节点的描述
FString UBTService::GetStaticServiceDescription() const
{
	return GetStaticTickIntervalDescription();
}

// 得到静态的描述
FString UBTService::GetStaticDescription() const
{
	return FString::Printf(TEXT("%s: %s"), *UBehaviorTreeTypes::GetShortTypeName(this), *GetStaticServiceDescription());
}

#if WITH_EDITOR

FName UBTService::GetNodeIconName() const
{
	return FName("BTEditor.Graph.BTNode.Service.Icon");
}

#endif // WITH_EDITOR
// 设置下一个tick时间
void UBTService::ScheduleNextTick(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	// 随机下一次的时间
	const float NextTickTime = FMath::FRandRange(FMath::Max(0.0f, Interval - RandomDeviation), (Interval + RandomDeviation));
	SetNextTickTime(NodeMemory, NextTickTime);
}
