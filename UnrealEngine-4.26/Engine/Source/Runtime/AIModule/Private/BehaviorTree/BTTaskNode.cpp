// Copyright Epic Games, Inc. All Rights Reserved.

#include "BehaviorTree/BTTaskNode.h"
#include "AIController.h"
#include "VisualLogger/VisualLogger.h"
#include "GameplayTasksComponent.h"

UBTTaskNode::UBTTaskNode(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	bNotifyTick = false;
	bNotifyTaskFinished = false;
	bIgnoreRestartSelf = false;
}

EBTNodeResult::Type UBTTaskNode::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	return EBTNodeResult::Succeeded;
}

EBTNodeResult::Type UBTTaskNode::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	return EBTNodeResult::Aborted;
}
// 实例化节点包装函数：ExecuteTask
EBTNodeResult::Type UBTTaskNode::WrappedExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	// 如果是实例化节点，需要找到对应的实例化内存
	const UBTNode* NodeOb = bCreateNodeInstance ? GetNodeInstance(OwnerComp, NodeMemory) : this;
	return NodeOb ? ((UBTTaskNode*)NodeOb)->ExecuteTask(OwnerComp, NodeMemory) : EBTNodeResult::Failed;
}

// 实例化节点包装函数：AbortTask
EBTNodeResult::Type UBTTaskNode::WrappedAbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	// 如果是实例化节点，需要找到对应的实例化内存
	UBTNode* NodeOb = const_cast<UBTNode*>(bCreateNodeInstance ? GetNodeInstance(OwnerComp, NodeMemory) : this);
	UBTTaskNode* TaskNodeOb = static_cast<UBTTaskNode*>(NodeOb);
	EBTNodeResult::Type Result = TaskNodeOb ? TaskNodeOb->AbortTask(OwnerComp, NodeMemory) : EBTNodeResult::Aborted;

	return Result;
}

// 实例化节点的包装函数：TickTask
bool UBTTaskNode::WrappedTickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds, float& NextNeededDeltaTime) const
{
	if (bNotifyTick)
	{
		// 如果是实例化节点，需要找到对应的实例化内存
		const UBTNode* NodeOb = bCreateNodeInstance ? GetNodeInstance(OwnerComp, NodeMemory) : this;
		if (NodeOb)
		{
			((UBTTaskNode*)NodeOb)->TickTask(OwnerComp, NodeMemory, DeltaSeconds);
			NextNeededDeltaTime = 0.0f;
			return true;
		}
	}
	return false;
}

// 实例化节点的包装函数：OnTaskFinished
void UBTTaskNode::WrappedOnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult) const
{
	// 如果是实例化节点，需要找到对应的实例化内存
	UBTNode* NodeOb = const_cast<UBTNode*>(bCreateNodeInstance ? GetNodeInstance(OwnerComp, NodeMemory) : this);

	if (NodeOb)
	{
		UBTTaskNode* TaskNodeOb = static_cast<UBTTaskNode*>(NodeOb);
		// 需要通知任务结束
		if (TaskNodeOb->bNotifyTaskFinished)
		{
			TaskNodeOb->OnTaskFinished(OwnerComp, NodeMemory, TaskResult);
		}
		// 处理GameTasks
		if (TaskNodeOb->bOwnsGameplayTasks && OwnerComp.GetAIOwner())
		{
			UGameplayTasksComponent* GTComp = OwnerComp.GetAIOwner()->GetGameplayTasksComponent();
			if (GTComp)
			{
				GTComp->EndAllResourceConsumingTasksOwnedBy(*TaskNodeOb);
			}
		}
	}
}

// 接收信息
void UBTTaskNode::ReceivedMessage(UBrainComponent* BrainComp, const FAIMessage& Message)
{
	UBehaviorTreeComponent* OwnerComp = static_cast<UBehaviorTreeComponent*>(BrainComp);
	check(OwnerComp);
	
	const uint16 InstanceIdx = OwnerComp->FindInstanceContainingNode(this);
	if (OwnerComp->InstanceStack.IsValidIndex(InstanceIdx))
	{
		uint8* NodeMemory = GetNodeMemory<uint8>(OwnerComp->InstanceStack[InstanceIdx]);
		OnMessage(*OwnerComp, NodeMemory, Message.MessageName, Message.RequestID, Message.Status == FAIMessage::Success);
	}
	else
	{
		UE_VLOG(OwnerComp->GetOwner(), LogBehaviorTree, Warning, TEXT("UBTTaskNode::ReceivedMessage called while %s node no longer in active BT")
			, *GetNodeName());
	}
}

void UBTTaskNode::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	// empty in base class
}

void UBTTaskNode::OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult)
{
	// empty in base class
}

// 处理消息
void UBTTaskNode::OnMessage(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, FName Message, int32 RequestID, bool bSuccess)
{
	const EBTTaskStatus::Type Status = OwnerComp.GetTaskStatus(this);
	// 如果节点是激活状态，则完成任务
	if (Status == EBTTaskStatus::Active)
	{
		FinishLatentTask(OwnerComp, bSuccess ? EBTNodeResult::Succeeded : EBTNodeResult::Failed);
	}
	// 如果节点是中止状态，则完成中止
	else if (Status == EBTTaskStatus::Aborting)
	{
		FinishLatentAbort(OwnerComp);
	}
}

void UBTTaskNode::FinishLatentTask(UBehaviorTreeComponent& OwnerComp, EBTNodeResult::Type TaskResult) const
{
	// OnTaskFinished must receive valid template node
	UBTTaskNode* TemplateNode = (UBTTaskNode*)OwnerComp.FindTemplateNode(this);
	OwnerComp.OnTaskFinished(TemplateNode, TaskResult);
}

void UBTTaskNode::FinishLatentAbort(UBehaviorTreeComponent& OwnerComp) const
{
	// OnTaskFinished must receive valid template node
	UBTTaskNode* TemplateNode = (UBTTaskNode*)OwnerComp.FindTemplateNode(this);
	OwnerComp.OnTaskFinished(TemplateNode, EBTNodeResult::Aborted);
}

void UBTTaskNode::WaitForMessage(UBehaviorTreeComponent& OwnerComp, FName MessageType) const
{
	// messages delegates should be called on node instances (if they exists)
	OwnerComp.RegisterMessageObserver(this, MessageType);
}

void UBTTaskNode::WaitForMessage(UBehaviorTreeComponent& OwnerComp, FName MessageType, int32 RequestID) const
{
	// messages delegates should be called on node instances (if they exists)
	OwnerComp.RegisterMessageObserver(this, MessageType, RequestID);
}
	
void UBTTaskNode::StopWaitingForMessages(UBehaviorTreeComponent& OwnerComp) const
{
	// messages delegates should be called on node instances (if they exists)
	OwnerComp.UnregisterMessageObserversFrom(this);
}

#if WITH_EDITOR

FName UBTTaskNode::GetNodeIconName() const
{
	return FName("BTEditor.Graph.BTNode.Task.Icon");
}

#endif	// WITH_EDITOR

void UBTTaskNode::OnGameplayTaskDeactivated(UGameplayTask& Task)
{
	ensure(Task.GetTaskOwner() == this);

	UBehaviorTreeComponent* BTComp = GetBTComponentForTask(Task);
	if (BTComp)
	{
		// this is a super-default behavior. Specific task will surely like to 
		// handle this themselves, finishing with specific result
		// 这是一个基类的默认行为，具体任务一定会喜欢自己处理，以特定的结果结束
		const EBTTaskStatus::Type Status = BTComp->GetTaskStatus(this);
		FinishLatentTask(*BTComp, Status == EBTTaskStatus::Aborting ? EBTNodeResult::Aborted : EBTNodeResult::Succeeded);
	}
}

//----------------------------------------------------------------------//
// DEPRECATED
//----------------------------------------------------------------------//
