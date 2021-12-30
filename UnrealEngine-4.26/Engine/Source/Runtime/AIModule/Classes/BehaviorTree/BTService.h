// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "BehaviorTree/BTAuxiliaryNode.h"
#include "BTService.generated.h"

/** 
 *	Behavior Tree service nodes is designed to perform "background" tasks that update AI's knowledge.
 *
 *  Services are being executed when underlying branch of behavior tree becomes active,
 *  but unlike tasks they don't return any results and can't directly affect execution flow.
 *
 *  Usually they perform periodical checks (see TickNode) and often store results in blackboard.
 *  If any decorator node below requires results of check beforehand, use OnSearchStart function.
 *   Keep in mind that any checks performed there have to be instantaneous!
 *	
 *  Other typical use case is creating a marker when specific branch is being executed
 *  (see OnBecomeRelevant, OnCeaseRelevant), by setting a flag in blackboard.
 *
 *  Because some of them can be instanced for specific AI, following virtual functions are not marked as const:
 *   - OnBecomeRelevant (from UBTAuxiliaryNode)
 *   - OnCeaseRelevant (from UBTAuxiliaryNode)
 *   - TickNode (from UBTAuxiliaryNode)
 *   - OnSearchStart
 *
 *  If your node is not being instanced (default behavior), DO NOT change any properties of object within those functions!
 *  Template nodes are shared across all behavior tree components using the same tree asset and must store
 *  their runtime properties in provided NodeMemory block (allocation size determined by GetInstanceMemorySize() )
 */
 
UCLASS(Abstract)
class AIMODULE_API UBTService : public UBTAuxiliaryNode
{
	GENERATED_UCLASS_BODY()
	// 得到静态的描述
	virtual FString GetStaticDescription() const override;

	void NotifyParentActivation(FBehaviorTreeSearchData& SearchData);

protected:
	// Gets the description of our tick interval
	// 得到tick间隔的描述
	FString GetStaticTickIntervalDescription() const;

	// Gets the description for our service
	// 服务节点的描述
	virtual FString GetStaticServiceDescription() const;

	/** defines time span between subsequent ticks of the service */
	// 定义服务的后续滴答之间的时间间隔
	UPROPERTY(Category=Service, EditAnywhere, meta=(ClampMin="0.001"))
	float Interval;

	/** adds random range to service's Interval */
	// 在服务的时间间隔上加上随机
	UPROPERTY(Category=Service, EditAnywhere, meta=(ClampMin="0.0"))
	float RandomDeviation;

	/** call Tick event when task search enters this node (SearchStart will be called as well) */
	// 当任务搜索进入该节点时调用Tick事件（SearchStart也会被调用）
	UPROPERTY(Category = Service, EditAnywhere, AdvancedDisplay)
	uint32 bCallTickOnSearchStart : 1;

	/** if set, next tick time will be always reset to service's interval when node is activated */
	// 如果设置，当节点被激活时，下一个滴答时间将始终重置为服务的间隔
	UPROPERTY(Category = Service, EditAnywhere, AdvancedDisplay)
	uint32 bRestartTimerOnEachActivation : 1;

	/** if set, service will be notified about search entering underlying branch */
	// 如果设置，服务将收到有关搜索进入底层分支的通知
	uint32 bNotifyOnSearch : 1;

	/** update next tick interval
	 * this function should be considered as const (don't modify state of object) if node is not instanced! */
	// tick节点，更新下一个tick间隔
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;

	/** called when search enters underlying branch
	 * this function should be considered as const (don't modify state of object) if node is not instanced! */
	// 当搜索进入底层分支时调用
	virtual void OnSearchStart(FBehaviorTreeSearchData& SearchData);

#if WITH_EDITOR
	virtual FName GetNodeIconName() const override;
#endif // WITH_EDITOR

	/** set next tick time */
	// 设置下一个tick时间
	virtual void ScheduleNextTick(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory);
};
