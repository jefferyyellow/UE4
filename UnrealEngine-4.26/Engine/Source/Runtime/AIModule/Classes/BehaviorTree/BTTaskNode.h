// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "BehaviorTree/BTNode.h"
#include "BTTaskNode.generated.h"

class UBTService;

/** 
 * Task are leaf nodes of behavior tree, which perform actual actions
 *
 * Because some of them can be instanced for specific AI, following virtual functions are not marked as const:
 *  - ExecuteTask
 *  - AbortTask
 *  - TickTask
 *  - OnMessage
 *
 * If your node is not being instanced (default behavior), DO NOT change any properties of object within those functions!
 * Template nodes are shared across all behavior tree components using the same tree asset and must store
 * their runtime properties in provided NodeMemory block (allocation size determined by GetInstanceMemorySize() )
 *
 */

UCLASS(Abstract)
class AIMODULE_API UBTTaskNode : public UBTNode
{
	GENERATED_UCLASS_BODY()

	/** starts this task, should return Succeeded, Failed or InProgress
	 *  (use FinishLatentTask() when returning InProgress)
	 * this function should be considered as const (don't modify state of object) if node is not instanced! */
	// 开始任务，应该返回Succeeded，Failed或者InProgress
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory);

protected:
	/** aborts this task, should return Aborted or InProgress
	 *  (use FinishLatentAbort() when returning InProgress)
	 * this function should be considered as const (don't modify state of object) if node is not instanced! */
	// 中止任务，应该返回Aborted或者InProgress
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory);

public:
#if WITH_EDITOR
	virtual FName GetNodeIconName() const override;
#endif // WITH_EDITOR
	// 游戏任务停用
	virtual void OnGameplayTaskDeactivated(UGameplayTask& Task) override;

	/** message observer's hook */
	// 消息观察者的钩子
	void ReceivedMessage(UBrainComponent* BrainComp, const FAIMessage& Message);

	/** wrapper for node instancing: ExecuteTask */
	// 实例化节点包装函数：ExecuteTask
	EBTNodeResult::Type WrappedExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const;

	/** wrapper for node instancing: AbortTask */
	// 实例化节点包装函数：AbortTask
	EBTNodeResult::Type WrappedAbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const;

	/** wrapper for node instancing: TickTask
	  * @param OwnerComp	The behavior tree owner of this node
	  * @param NodeMemory	The instance memory of the current node
	  * @param DeltaSeconds		DeltaTime since last call
	  * @param NextNeededDeltaTime		In out parameter, if this node needs a smaller DeltaTime it is his responsibility to change it
	  * @returns	True if it actually done some processing or false if it was skipped because of not ticking or in between time interval */
	// 实例化节点的包装函数：TickTask
	bool WrappedTickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds, float& NextNeededDeltaTime) const;

	/** wrapper for node instancing: OnTaskFinished */
	// 实例化节点的包装函数：OnTaskFinished
	void WrappedOnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult) const;

	/** helper function: finish latent executing */
	// 辅助函数：完成潜在的执行
	void FinishLatentTask(UBehaviorTreeComponent& OwnerComp, EBTNodeResult::Type TaskResult) const;

	/** helper function: finishes latent aborting */
	// 辅助函数：完成潜在的中止
	void FinishLatentAbort(UBehaviorTreeComponent& OwnerComp) const;

	/** @return true if task search should be discarded when this task is selected to execute but is already running */
	// true表示：当该任务被选择执行但是已经在允许中了，任务搜索就应该被丢弃
	bool ShouldIgnoreRestartSelf() const;

	/** service nodes */
	// 服务节点列表
	UPROPERTY()
	TArray<UBTService*> Services;

protected:

	/** if set, task search will be discarded when this task is selected to execute but is already running */
	// 启用后，当选择执行的任务已在运行时，任务搜索将被放弃
	UPROPERTY(EditAnywhere, Category=Task)
	uint32 bIgnoreRestartSelf : 1;

	/** if set, TickTask will be called */
	// 如果设置，TickTask将会被调用
	uint32 bNotifyTick : 1;

	/** if set, OnTaskFinished will be called */
	// 如果设置，OnTaskFinished将会被调用
	uint32 bNotifyTaskFinished : 1;
	
	/** ticks this task 
	 * this function should be considered as const (don't modify state of object) if node is not instanced! */
	// ticks该任务
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds);

	/** message handler, default implementation will finish latent execution/abortion
	 * this function should be considered as const (don't modify state of object) if node is not instanced! */
	// 消息分发器，默认实现将完成潜在的执行/中止
	virtual void OnMessage(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, FName Message, int32 RequestID, bool bSuccess);

	/** called when task execution is finished
	 * this function should be considered as const (don't modify state of object) if node is not instanced! */
	// 任务执行完成后调用
	virtual void OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult);

	/** register message observer */
	// 注册消息观察者
	void WaitForMessage(UBehaviorTreeComponent& OwnerComp, FName MessageType) const;
	void WaitForMessage(UBehaviorTreeComponent& OwnerComp, FName MessageType, int32 RequestID) const;
	
	/** unregister message observers */
	// 注销消息观察者
	void StopWaitingForMessages(UBehaviorTreeComponent& OwnerComp) const;
};

FORCEINLINE bool UBTTaskNode::ShouldIgnoreRestartSelf() const
{
	return bIgnoreRestartSelf;
}
