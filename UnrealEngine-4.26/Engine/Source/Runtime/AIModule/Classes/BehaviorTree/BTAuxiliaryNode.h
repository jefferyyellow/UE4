// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "BehaviorTree/BTNode.h"
#include "BTAuxiliaryNode.generated.h"

struct FBTAuxiliaryMemory : public FBTInstancedNodeMemory
{
	float NextTickRemainingTime;
	float AccumulatedDeltaTime;
};

/** 
 * Auxiliary nodes are supporting nodes, that receive notification about execution flow and can be ticked
 *
 * Because some of them can be instanced for specific AI, following virtual functions are not marked as const:
 *  - OnBecomeRelevant
 *  - OnCeaseRelevant
 *  - TickNode
 *
 * If your node is not being instanced (default behavior), DO NOT change any properties of object within those functions!
 * Template nodes are shared across all behavior tree components using the same tree asset and must store
 * their runtime properties in provided NodeMemory block (allocation size determined by GetInstanceMemorySize() )
 *
 */
// 辅助节点是支持节点，能接收执行流程的通知并且可以被ticked

UCLASS(Abstract)
class AIMODULE_API UBTAuxiliaryNode : public UBTNode
{
	GENERATED_UCLASS_BODY()

	/** wrapper for node instancing: OnBecomeRelevant */
	// 节点实例化的包装函数：OnBecomeRelevant
	void WrappedOnBecomeRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const;

	/** wrapper for node instancing: OnCeaseRelevant */
	// 节点实例化的包装函数：OnCeaseRelevant
	void WrappedOnCeaseRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const;

	/** wrapper for node instancing: TickNode
	  * @param OwnerComp	The behavior tree owner of this node
	  * @param NodeMemory	The instance memory of the current node
	  * @param DeltaSeconds		DeltaTime since last call
	  * @param NextNeededDeltaTime		In out parameter, if this node needs a smaller DeltaTime it is his responsibility to change it
	  * @returns	True if it actually done some processing or false if it was skipped because of not ticking or in between time interval */
	// 节点实例化的包装函数：TickNode
	bool WrappedTickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds, float& NextNeededDeltaTime) const;
	// 描述运行时的值
	virtual void DescribeRuntimeValues(const UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTDescriptionVerbosity::Type Verbosity, TArray<FString>& Values) const override;
	// 得到专用内存大小
	virtual uint16 GetSpecialMemorySize() const override;

	/** fill in data about tree structure */
	// 树结构的数据填充
	void InitializeParentLink(uint8 InChildIndex);

	/** @return parent task node */
	// 得到父任务节点
	const UBTNode* GetMyNode() const;

	/** @return index of child in parent's array or MAX_uint8 */
	// 在父节点数组中子节点的索引
	uint8 GetChildIndex() const;

	/** Get The next needed deltatime for this node
	  * @param OwnerComp	The behavior tree owner of this node
	  * @param NodeMemory	The instance memory of the current node
	  * @return The next needed DeltaTime */
	// 获取此节点下一次tick的剩余时间
	float GetNextNeededDeltaTime(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const;

protected:

	/** if set, OnBecomeRelevant will be used */
	// 是否调用OnBecomeRelevant
	uint8 bNotifyBecomeRelevant:1;

	/** if set, OnCeaseRelevant will be used */
	// 是否调用OnCeaseRelevant
	uint8 bNotifyCeaseRelevant:1;

	/** if set, OnTick will be used */
	// 是否调用OnTick
	uint8 bNotifyTick : 1;

	/** if set, conditional tick will use remaining time form node's memory */
	// 如果设置，条件tick将使用节点内存中剩余的时间
	// 这个设置以后，不是每次都tick,而是通过FBTAuxiliaryMemory中的NextTickRemainingTime来控制，
	// 减少到零了tick一次
	uint8 bTickIntervals : 1;

	/** child index in parent node */
	// 在父节点中的子节点索引
	uint8 ChildIndex;

	/** called when auxiliary node becomes active
	 * this function should be considered as const (don't modify state of object) if node is not instanced! */
	// 当辅助节点变成活跃时调用
	virtual void OnBecomeRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory);

	/** called when auxiliary node becomes inactive
	 * this function should be considered as const (don't modify state of object) if node is not instanced! */
	// 当辅助节点变成不活跃时调用
	virtual void OnCeaseRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory);

	/** tick function
	 * this function should be considered as const (don't modify state of object) if node is not instanced! */
	// tick函数
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds);

	/** sets next tick time */
	// 设置下一次tick时间
	void SetNextTickTime(uint8* NodeMemory, float RemainingTime) const;

	/** gets remaining time for next tick */
	// 得到下一次tick还剩下多少时间
	float GetNextTickRemainingTime(uint8* NodeMemory) const;
};

FORCEINLINE uint8 UBTAuxiliaryNode::GetChildIndex() const
{
	return ChildIndex;
}
