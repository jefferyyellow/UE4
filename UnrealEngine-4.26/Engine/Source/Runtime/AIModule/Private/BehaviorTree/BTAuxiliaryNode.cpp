// Copyright Epic Games, Inc. All Rights Reserved.

#include "BehaviorTree/BTAuxiliaryNode.h"
#include "BehaviorTree/BTCompositeNode.h"

UBTAuxiliaryNode::UBTAuxiliaryNode(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	bNotifyBecomeRelevant = false;
	bNotifyCeaseRelevant = false;
	bNotifyTick = false;
	bTickIntervals = false;
	ChildIndex = BTSpecialChild::OwnedByComposite;
}

// 节点实例化的包装函数:OnBecomeRelevant
void UBTAuxiliaryNode::WrappedOnBecomeRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	if (bNotifyBecomeRelevant)
	{
		// 得到实例化的内存
		const UBTNode* NodeOb = HasInstance() ? GetNodeInstance(OwnerComp, NodeMemory) : this;
		if (NodeOb)
		{
			((UBTAuxiliaryNode*)NodeOb)->OnBecomeRelevant(OwnerComp, NodeMemory);
		}
	}
}

// 节点实例化的包装函数：OnCeaseRelevant
void UBTAuxiliaryNode::WrappedOnCeaseRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	if (bNotifyCeaseRelevant)
	{
		// 得到实例化的内存
		const UBTNode* NodeOb = HasInstance() ? GetNodeInstance(OwnerComp, NodeMemory) : this;
		if (NodeOb)
		{
			((UBTAuxiliaryNode*)NodeOb)->OnCeaseRelevant(OwnerComp, NodeMemory);
		}
	}
}

// 节点实例化的包装函数：TickNode
bool UBTAuxiliaryNode::WrappedTickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds, float& NextNeededDeltaTime) const
{
	if (bNotifyTick || HasInstance())
	{
		const UBTAuxiliaryNode* NodeOb = HasInstance() ? static_cast<UBTAuxiliaryNode*>(GetNodeInstance(OwnerComp, NodeMemory)) : this;
		
		ensure(NodeOb);
		// Tick通知
		if (NodeOb != nullptr && NodeOb->bNotifyTick)
		{
			float UseDeltaTime = DeltaSeconds;
			// 如果设置，条件tick将使用节点内存中剩余的时间
			if (NodeOb->bTickIntervals)
			{
				// 从节点内存中的时间来进行控制是否调用tick
				FBTAuxiliaryMemory* AuxMemory = GetSpecialNodeMemory<FBTAuxiliaryMemory>(NodeMemory);
				AuxMemory->NextTickRemainingTime -= DeltaSeconds;
				AuxMemory->AccumulatedDeltaTime += DeltaSeconds;

				// 减少的0就tick一次，并且将AccumulatedDeltaTime置零
				const bool bTick = AuxMemory->NextTickRemainingTime <= 0.0f;
				if (bTick)
				{
				    UseDeltaTime = AuxMemory->AccumulatedDeltaTime;
				    AuxMemory->AccumulatedDeltaTime = 0.0f;
    
				    const_cast<UBTAuxiliaryNode*>(NodeOb)->TickNode(OwnerComp, NodeMemory, UseDeltaTime);
				}

				if (AuxMemory->NextTickRemainingTime < NextNeededDeltaTime)
				{
					NextNeededDeltaTime = AuxMemory->NextTickRemainingTime;
				}
				return bTick;
			}
			else
			{
				const_cast<UBTAuxiliaryNode*>(NodeOb)->TickNode(OwnerComp, NodeMemory, UseDeltaTime);
				NextNeededDeltaTime = 0.0f;
				return true;
			}

		}
	}
	return false;
}

// 设置下一次tick时间
void UBTAuxiliaryNode::SetNextTickTime(uint8* NodeMemory, float RemainingTime) const
{
	if (bTickIntervals)
	{
		FBTAuxiliaryMemory* AuxMemory = GetSpecialNodeMemory<FBTAuxiliaryMemory>(NodeMemory);
		AuxMemory->NextTickRemainingTime = RemainingTime;
	}
}

// 得到下一次tick还剩下多少时间
float UBTAuxiliaryNode::GetNextTickRemainingTime(uint8* NodeMemory) const
{
	if (bTickIntervals)
	{
		FBTAuxiliaryMemory* AuxMemory = GetSpecialNodeMemory<FBTAuxiliaryMemory>(NodeMemory);
		return FMath::Max(0.0f, AuxMemory->NextTickRemainingTime);
	}

	return 0.0f;
}

// 描述运行时的值
void UBTAuxiliaryNode::DescribeRuntimeValues(const UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTDescriptionVerbosity::Type Verbosity, TArray<FString>& Values) const
{
	Super::DescribeRuntimeValues(OwnerComp, NodeMemory, Verbosity, Values);

	if (Verbosity == EBTDescriptionVerbosity::Detailed && bTickIntervals)
	{
		FBTAuxiliaryMemory* AuxMemory = GetSpecialNodeMemory<FBTAuxiliaryMemory>(NodeMemory);
		Values.Add(FString::Printf(TEXT("next tick: %ss"), *FString::SanitizeFloat(AuxMemory->NextTickRemainingTime)));
	}
}

void UBTAuxiliaryNode::OnBecomeRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	// empty in base class
}

void UBTAuxiliaryNode::OnCeaseRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	// empty in base class
}

void UBTAuxiliaryNode::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	// empty in base class
}

// 得到专用内存大小
uint16 UBTAuxiliaryNode::GetSpecialMemorySize() const
{
	// FBTAuxiliaryMemory新增的字段都是bTickIntervals需要的，要不然只需要调用父类的GetSpecialMemorySize就够了
	return bTickIntervals ? sizeof(FBTAuxiliaryMemory) : Super::GetSpecialMemorySize();
}

// 在父节点中的子节点索引
void UBTAuxiliaryNode::InitializeParentLink(uint8 MyChildIndex)
{
	ChildIndex = MyChildIndex;
}

// 得到父任务节点
const UBTNode* UBTAuxiliaryNode::GetMyNode() const
{
	return (ChildIndex == BTSpecialChild::OwnedByComposite) ? GetParentNode() : (GetParentNode() ? GetParentNode()->GetChildNode(ChildIndex) : nullptr);
}

// 获取此节点下一次tick的剩余时间
float UBTAuxiliaryNode::GetNextNeededDeltaTime(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	if (bNotifyTick || HasInstance())
	{
		const UBTAuxiliaryNode* NodeOb = HasInstance() ? static_cast<UBTAuxiliaryNode*>(GetNodeInstance(OwnerComp, NodeMemory)) : this;

		if (NodeOb != nullptr && NodeOb->bNotifyTick)
		{
			if (bTickIntervals)
			{
				FBTAuxiliaryMemory* AuxMemory = GetSpecialNodeMemory<FBTAuxiliaryMemory>(NodeMemory);
				return AuxMemory->NextTickRemainingTime;
			}
			else
			{
				return 0.0f;
			}
		}
	}
	return FLT_MAX;
}

//----------------------------------------------------------------------//
// DEPRECATED
//----------------------------------------------------------------------//
