// Copyright Epic Games, Inc. All Rights Reserved.

#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "VisualLogger/VisualLogger.h"

UBTDecorator::UBTDecorator(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	FlowAbortMode = EBTFlowAbortMode::None;
	bAllowAbortNone = true;
	bAllowAbortLowerPri = true;
	bAllowAbortChildNodes = true;
	bNotifyActivation = false;
	bNotifyDeactivation = false;
	bNotifyProcessed = false;

	bShowInverseConditionDesc = true;
	bInverseCondition = false;
}

bool UBTDecorator::CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	return true;
}

// 设置是否反转条件
void UBTDecorator::SetIsInversed(bool bShouldBeInversed)
{
	bInverseCondition = bShouldBeInversed;
}

void UBTDecorator::OnNodeActivation(FBehaviorTreeSearchData& SearchData)
{
}

void UBTDecorator::OnNodeDeactivation(FBehaviorTreeSearchData& SearchData, EBTNodeResult::Type NodeResult)
{
}

void UBTDecorator::OnNodeProcessed(FBehaviorTreeSearchData& SearchData, EBTNodeResult::Type& NodeResult)
{
}

// 实例化节点包装函数：CalculateRawConditionValue
bool UBTDecorator::WrappedCanExecute(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	// 得到节点内存地址
	const UBTDecorator* NodeOb = bCreateNodeInstance ? (const UBTDecorator*)GetNodeInstance(OwnerComp, NodeMemory) : this;
	return NodeOb ? (IsInversed() != NodeOb->CalculateRawConditionValue(OwnerComp, NodeMemory)) : false;
}

// 实例化节点包装函数：OnNodeActivation
void UBTDecorator::WrappedOnNodeActivation(FBehaviorTreeSearchData& SearchData) const
{
	if (bNotifyActivation)
	{
		const UBTNode* NodeOb = bCreateNodeInstance ? GetNodeInstance(SearchData) : this;
		if (NodeOb)
		{
			((UBTDecorator*)NodeOb)->OnNodeActivation(SearchData);
		}		
	}
};

// 实例化节点包装函数：OnNodeDeactivation
void UBTDecorator::WrappedOnNodeDeactivation(FBehaviorTreeSearchData& SearchData, EBTNodeResult::Type NodeResult) const
{
	if (bNotifyDeactivation)
	{
		const UBTNode* NodeOb = bCreateNodeInstance ? GetNodeInstance(SearchData) : this;
		if (NodeOb)
		{
			((UBTDecorator*)NodeOb)->OnNodeDeactivation(SearchData, NodeResult);
		}		
	}
}

// 实例化节点包装函数：OnNodeProcessed
void UBTDecorator::WrappedOnNodeProcessed(FBehaviorTreeSearchData& SearchData, EBTNodeResult::Type& NodeResult) const
{
	if (bNotifyProcessed)
	{
		const UBTNode* NodeOb = bCreateNodeInstance ? GetNodeInstance(SearchData) : this;
		if (NodeOb)
		{
			((UBTDecorator*)NodeOb)->OnNodeProcessed(SearchData, NodeResult);
		}		
	}
}

// 条件打断
void UBTDecorator::ConditionalFlowAbort(UBehaviorTreeComponent& OwnerComp, EBTDecoratorAbortRequest RequestMode) const
{
	if (FlowAbortMode == EBTFlowAbortMode::None)
	{
		return;
	}

	// 找到节点索引
	const int32 InstanceIdx = OwnerComp.FindInstanceContainingNode(GetParentNode());
	if (InstanceIdx == INDEX_NONE)
	{
		return;
	}
	// 找到节点内存
	uint8* NodeMemory = OwnerComp.GetNodeMemory((UBTNode*)this, InstanceIdx);

	// 正在运行子树
	const bool bIsExecutingBranch = OwnerComp.IsExecutingBranch(this, GetChildIndex());
	// 条件通过
	const bool bPass = WrappedCanExecute(OwnerComp, NodeMemory);
	// 暂停中
	const bool bAbortPending = OwnerComp.IsAbortPending();
	// 只要条件仍然通过，每次都请求执行更新
	const bool bAlwaysRequestWhenPassing = (RequestMode == EBTDecoratorAbortRequest::ConditionPassing);

	const bool bLogRestart = (bIsExecutingBranch != bPass) || (bIsExecutingBranch && bPass && (bAlwaysRequestWhenPassing || bAbortPending));
	UE_VLOG(OwnerComp.GetOwner(), LogBehaviorTree, Verbose, TEXT("%s, ConditionalFlowAbort(%s) pass:%d executingBranch:%d abortPending:%d => %s"),
		*UBehaviorTreeTypes::DescribeNodeHelper(this),
		bAlwaysRequestWhenPassing ? TEXT("always when passing") : TEXT("on change"),
		bPass ? 1 : 0,
		bIsExecutingBranch ? 1 : 0,
		bAbortPending ? 1 : 0,
		bLogRestart ? TEXT("restart") : TEXT("skip"));
	// 1.如果运行子树时，条件变化以后，条件已经不能通过了
	// 2.如果不在运行子树，条件变化以后，条件通过了，可以运行子树了
	if (bIsExecutingBranch != bPass)
	{
		OwnerComp.RequestExecution(this);
	}
	// 不在运行子树，条件也通过不了，并且有父节点
	else if (!bIsExecutingBranch && !bPass && GetParentNode() && GetParentNode()->Children.IsValidIndex(GetChildIndex()))
	{
		// this condition here is to remove all active observers _BELOW_ this node
		// because if this condition failed we no longer want to react to child-conditions
		// value changes anyway since their nodes execution will be blocked by this condition 
		// during tree search
		// 该条件在这里用于删除所有激活的观察器_BELOW_该节点，因为如果该条件失败我们不再想去反馈子条件，
		// 不管以何种方式改变值，因为他们的节点执行将会被在树搜索期间被该条件阻塞
		const UBTCompositeNode* BranchRoot = GetParentNode()->Children[GetChildIndex()].ChildComposite;
		OwnerComp.RequestUnregisterAuxNodesInBranch(BranchRoot);
	}
	else if (bIsExecutingBranch && bPass && (bAlwaysRequestWhenPassing || bAbortPending))
	{
		// force result Aborted to restart from this decorator
		// 强制结果中止从该装饰器重新启动
		OwnerComp.RequestExecution(GetParentNode(), InstanceIdx, this, GetChildIndex(), EBTNodeResult::Aborted);
	}
}

// 得到静态描述
FString UBTDecorator::GetStaticDescription() const
{
	FString FlowAbortDesc;
	if (FlowAbortMode != EBTFlowAbortMode::None)
	{
		FlowAbortDesc = FString::Printf(TEXT("aborts %s"), *UBehaviorTreeTypes::DescribeFlowAbortMode(FlowAbortMode).ToLower());
	}

	FString InversedDesc;
	if (bShowInverseConditionDesc && IsInversed())
	{
		InversedDesc = TEXT("inversed");
	}

	FString AdditionalDesc;
	if (FlowAbortDesc.Len() || InversedDesc.Len())
	{
		AdditionalDesc = FString::Printf(TEXT("( %s%s%s )\n"), *FlowAbortDesc, 
			(FlowAbortDesc.Len() > 0) && (InversedDesc.Len() > 0) ? TEXT(", ") : TEXT(""),
			*InversedDesc);
	}

	return FString::Printf(TEXT("%s%s"), *AdditionalDesc, *UBehaviorTreeTypes::GetShortTypeName(this));
}

bool UBTDecorator::IsFlowAbortModeValid() const
{
#if WITH_EDITOR
	// 
	if (GetParentNode() == NULL ||
		(GetParentNode()->CanAbortLowerPriority() == false && GetParentNode()->CanAbortSelf() == false))
	{
		return (FlowAbortMode == EBTFlowAbortMode::None);
	}
	// 不是打断低优先级
	if (GetParentNode()->CanAbortLowerPriority() == false)
	{
		return (FlowAbortMode == EBTFlowAbortMode::None || FlowAbortMode == EBTFlowAbortMode::Self);
	}
	// 不能打断自己
	if (GetParentNode()->CanAbortSelf() == false)
	{
		return (FlowAbortMode == EBTFlowAbortMode::None || FlowAbortMode == EBTFlowAbortMode::LowerPriority);
	}
#endif

	return true;
}

void UBTDecorator::UpdateFlowAbortMode()
{
#if WITH_EDITOR
	if (GetParentNode() == NULL)
	{
		FlowAbortMode = EBTFlowAbortMode::None;
		return;
	}

	// 父节点不是打断低优先级
	if (GetParentNode()->CanAbortLowerPriority() == false)
	{
		// 当前是Both的话，将低优先级去掉
		if (FlowAbortMode == EBTFlowAbortMode::Both)
		{
			FlowAbortMode = GetParentNode()->CanAbortSelf() ? EBTFlowAbortMode::Self : EBTFlowAbortMode::None;
		}
		else if (FlowAbortMode == EBTFlowAbortMode::LowerPriority)
		{
			FlowAbortMode = EBTFlowAbortMode::None;
		}
	}

	// 父节点不能打断自己
	if (GetParentNode()->CanAbortSelf() == false)
	{
		// 当前是Both的话，将打断自己去掉
		if (FlowAbortMode == EBTFlowAbortMode::Both)
		{
			FlowAbortMode = GetParentNode()->CanAbortLowerPriority() ? EBTFlowAbortMode::LowerPriority : EBTFlowAbortMode::None;
		}
		else if (FlowAbortMode == EBTFlowAbortMode::Self)
		{
			FlowAbortMode = EBTFlowAbortMode::None;
		}
	}
#endif
}

//----------------------------------------------------------------------//
// DEPRECATED
//----------------------------------------------------------------------//
void UBTDecorator::InitializeDecorator(uint8 InChildIndex)
{
	InitializeParentLink(InChildIndex);
}
