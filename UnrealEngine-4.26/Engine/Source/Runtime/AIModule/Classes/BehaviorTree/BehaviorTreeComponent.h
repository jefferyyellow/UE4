// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "EngineDefines.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "GameplayTagContainer.h"
#include "AITypes.h"
#include "BrainComponent.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "BehaviorTreeComponent.generated.h"

class FBehaviorTreeDebugger;
class UBehaviorTree;
class UBTAuxiliaryNode;
class UBTCompositeNode;
class UBTDecorator;
class UBTNode;
class UBTTask_RunBehavior;
class UBTTask_RunBehaviorDynamic;
class UBTTaskNode;
struct FScopedBehaviorTreeLock;

struct FBTNodeExecutionInfo
{
	/** index of first task allowed to be executed */
	// 允许执行的第一个任务的索引
	FBTNodeIndex SearchStart;

	/** index of last task allowed to be executed */
	// 允许执行的最后一个任务的索引
	FBTNodeIndex SearchEnd;

	/** node to be executed */
	// 执行的节点
	UBTCompositeNode* ExecuteNode;

	/** subtree index */
	// 子树的索引
	uint16 ExecuteInstanceIdx;

	/** result used for resuming execution */
	// 用于恢复执行的结果 
	TEnumAsByte<EBTNodeResult::Type> ContinueWithResult;

	/** if set, tree will try to execute next child of composite instead of forcing branch containing SearchStart */
	// 如果设置，则tree将尝试执行Composite的下一个子级，而不是强制包含SearchStart的分支
	uint8 bTryNextChild : 1;

	/** if set, request was not instigated by finishing task/initialization but is a restart (e.g. decorator) */
	// 如果已设置，则请求不会通过完成任务/初始化来激发，而是重新启动（例如，装饰器） 
	uint8 bIsRestart : 1;

	FBTNodeExecutionInfo() : ExecuteNode(NULL), bTryNextChild(false), bIsRestart(false) { }
};

// 代办的执行信息
struct FBTPendingExecutionInfo
{
	/** next task to execute */
	// 执行的下一个任务
	UBTTaskNode* NextTask;

	/** if set, tree ran out of nodes */
	// 如果设置，则树用完了节点
	uint32 bOutOfNodes : 1;

	/** if set, request can't be executed */
	// 如果设置，请求不被执行
	uint32 bLocked : 1;

	FBTPendingExecutionInfo() : NextTask(NULL), bOutOfNodes(false), bLocked(false) {}
	bool IsSet() const { return (NextTask || bOutOfNodes) && !bLocked; }
	bool IsLocked() const { return bLocked; }

	void Lock() { bLocked = true; }
	void Unlock() { bLocked = false; }
};

struct FBTPendingAuxNodesUnregisterInfo
{
	/** list of node index ranges pending aux nodes unregistration */
	// 未注册辅助节点的节点索引范围列表
	TArray<FBTNodeIndexRange> Ranges;
};

struct FBTTreeStartInfo
{
	// 行为树
	UBehaviorTree* Asset;
	// 执行模式
	EBTExecutionMode::Type ExecuteMode;
	// 是否待初始化
	uint8 bPendingInitialize : 1;

	FBTTreeStartInfo() : Asset(nullptr), ExecuteMode(EBTExecutionMode::Looped), bPendingInitialize(0) {}
	bool IsSet() const { return Asset != nullptr; }
	bool HasPendingInitialize() const { return bPendingInitialize && IsSet(); }
};

UCLASS(ClassGroup = AI, meta = (BlueprintSpawnableComponent))
class AIMODULE_API UBehaviorTreeComponent : public UBrainComponent
{
	GENERATED_UCLASS_BODY()

	// UActorComponent overrides
	// 注册组件的tick函数
	virtual void RegisterComponentTickFunctions(bool bRegister) override;
	// 设置组件的tick可用
	virtual void SetComponentTickEnabled(bool bEnabled) override;

	// Begin UBrainComponent overrides
	// UBrainComponent组件的虚拟接口的overrides
	// 开始逻辑
	virtual void StartLogic() override;
	// 重启逻辑
	virtual void RestartLogic() override;
	// 结束逻辑
	virtual void StopLogic(const FString& Reason) override;
	// 暂停逻辑
	virtual void PauseLogic(const FString& Reason) override;
	// 恢复逻辑
	virtual EAILogicResuming::Type ResumeLogic(const FString& Reason) override;

	/** indicates instance has been initialized to work with specific BT asset */
	// 表示实例已被初始化以使用特定的BT资产（树已经开始执行）
	bool TreeHasBeenStarted() const;

public:
	/** DO NOT USE. This constructor is for internal usage only for hot-reload purposes. */
	// 禁止使用。该构造函数只是用于热加载目的的内部使用
	UBehaviorTreeComponent(FVTableHelper& Helper);
	
	// 是否执行中（已经开始执行并且没有暂停）
	virtual bool IsRunning() const override;
	// 是否已经暂停
	virtual bool IsPaused() const override;
	// 清理
	virtual void Cleanup() override;
	// 处理消息
	virtual void HandleMessage(const FAIMessage& Message) override;
	// End UBrainComponent overrides

	// Begin UActorComponent overrides
	virtual void UninitializeComponent() override;
	// End UActorComponent overrides

	/** starts execution from root */
	// 从根开始执行
	void StartTree(UBehaviorTree& Asset, EBTExecutionMode::Type ExecuteMode = EBTExecutionMode::Looped);

	/** stops execution */
	// 结束执行
	void StopTree(EBTStopMode::Type StopMode = EBTStopMode::Safe);

	/** restarts execution from root */
	// 从根重新开始执行
	void RestartTree();

	/** request execution change */
	// 要求执行变更
	void RequestExecution(UBTCompositeNode* RequestedOn, int32 InstanceIdx, 
		const UBTNode* RequestedBy, int32 RequestedByChildIndex,
		EBTNodeResult::Type ContinueWithResult, bool bStoreForDebugger = true);

	/** request execution change: helpers for decorator nodes */
	// 要求执行变更：装饰节点
	void RequestExecution(const UBTDecorator* RequestedBy);

	/** request execution change: helpers for task nodes */
	// 要求执行变更：任务节点
	void RequestExecution(EBTNodeResult::Type ContinueWithResult);

	/** request unregistration of aux nodes in the specified branch */
	// 请求在指定分支中注销aux节点
	void RequestUnregisterAuxNodesInBranch(const UBTCompositeNode* Node);

	/** finish latent execution or abort */
	// 完成潜在的执行或中止 
	void OnTaskFinished(const UBTTaskNode* TaskNode, EBTNodeResult::Type TaskResult);

	/** setup message observer for given task */
	void RegisterMessageObserver(const UBTTaskNode* TaskNode, FName MessageType);
	void RegisterMessageObserver(const UBTTaskNode* TaskNode, FName MessageType, FAIRequestID MessageID);
	
	/** remove message observers registered with task */
	// 删除任务注册的消息观察者 
	void UnregisterMessageObserversFrom(const UBTTaskNode* TaskNode);
	void UnregisterMessageObserversFrom(const FBTNodeIndex& TaskIdx);

	/** add active parallel task */
	void RegisterParallelTask(const UBTTaskNode* TaskNode);

	/** remove parallel task */
	void UnregisterParallelTask(const UBTTaskNode* TaskNode, uint16 InstanceIdx);

	/** unregister all aux nodes less important than given index */
	void UnregisterAuxNodesUpTo(const FBTNodeIndex& Index);

	/** unregister all aux nodes between given execution index range: FromIndex < AuxIndex < ToIndex */
	void UnregisterAuxNodesInRange(const FBTNodeIndex& FromIndex, const FBTNodeIndex& ToIndex);

	/** unregister all aux nodes in branch of tree */
	UE_DEPRECATED(4.26, "This function is deprecated. Please use RequestUnregisterAuxNodesInBranch instead.")
	void UnregisterAuxNodesInBranch(const UBTCompositeNode* Node, bool bApplyImmediately = true);

	/** BEGIN UActorComponent overrides */
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	/** END UActorComponent overrides */

	/** Schedule when will be the next tick, 0.0f means next frame, FLT_MAX means never */
	// 安排下一帧的时间，0.0f表示下一帧，FLT_MAX表示从来不
	void ScheduleNextTick(float NextDeltaTime);

	/** process execution flow */
	// 处理执行流
	void ProcessExecutionRequest();

	/** schedule execution flow update in next tick */
	// 在下一个tick安排执行流更新
	void ScheduleExecutionUpdate();

	/** tries to find behavior tree instance in context */
	// 在上下文环境中找到行为树实例
	int32 FindInstanceContainingNode(const UBTNode* Node) const;

	/** tries to find template node for given instanced node */
	// 对于给定的实例节点查找对应的模板节点
	UBTNode* FindTemplateNode(const UBTNode* Node) const;

	/** @return current tree */
	UBehaviorTree* GetCurrentTree() const;

	/** @return tree from top of instance stack */
	// 实例堆栈顶部的树
	UBehaviorTree* GetRootTree() const;

	/** @return active node */
	// 返回激活的节点
	const UBTNode* GetActiveNode() const;
	
	/** get index of active instance on stack */
	uint16 GetActiveInstanceIdx() const;

	/** @return node memory */
	// 返回节点内存
	uint8* GetNodeMemory(UBTNode* Node, int32 InstanceIdx) const;

	/** @return true if ExecutionRequest is switching to higher priority node */
	bool IsRestartPending() const;

	/** @return true if waiting for abort to finish */
	bool IsAbortPending() const;

	/** @return true if active node is one of child nodes of given one */
	// 如果激活节点是给定节点的一个子节点就返回true
	bool IsExecutingBranch(const UBTNode* Node, int32 ChildIndex = -1) const;

	/** @return true if aux node is currently active */
	// 如果辅助节点是当前的活动节点，就返回true
	bool IsAuxNodeActive(const UBTAuxiliaryNode* AuxNode) const;
	bool IsAuxNodeActive(const UBTAuxiliaryNode* AuxNodeTemplate, int32 InstanceIdx) const;

	/** Returns true if InstanceStack contains any BT runtime instances */
	bool IsInstanceStackEmpty() const { return (InstanceStack.Num() == 0); }

	/** @return status of speficied task */
	// 返回指定任务节点的状态
	EBTTaskStatus::Type GetTaskStatus(const UBTTaskNode* TaskNode) const;

	virtual FString GetDebugInfoString() const override;
	virtual FString DescribeActiveTasks() const;
	virtual FString DescribeActiveTrees() const;

	/** @return the cooldown tag end time, 0.0f if CooldownTag is not found */
	UFUNCTION(BlueprintCallable, Category = "AI|Logic")
	float GetTagCooldownEndTime(FGameplayTag CooldownTag) const;

	/** add to the cooldown tag's duration */
	UFUNCTION(BlueprintCallable, Category = "AI|Logic")
	void AddCooldownTagDuration(FGameplayTag CooldownTag, float CooldownDuration, bool bAddToExistingDuration);

	/** assign subtree to RunBehaviorDynamic task specified by tag */
	UFUNCTION(BlueprintCallable, Category="AI|Logic")
	virtual void SetDynamicSubtree(FGameplayTag InjectTag, UBehaviorTree* BehaviorAsset);

// Code for timing BT Search for FramePro
#if !UE_BUILD_SHIPPING
	static void EndFrame();
#endif

#if ENABLE_VISUAL_LOG
	virtual void DescribeSelfToVisLog(struct FVisualLogEntry* Snapshot) const override;
#endif

#if CSV_PROFILER
	/** Set a custom CSV tick stat name, must point to a static string */
	void SetCSVTickStatName(const char* InCSVTickStatName) { CSVTickStatName = InCSVTickStatName; }
#endif

protected:
	/** stack of behavior tree instances */
	// 行为树实例堆栈
	TArray<FBehaviorTreeInstance> InstanceStack;

	/** list of known subtree instances */
	// 已知子树实例的列表
	TArray<FBehaviorTreeInstanceId> KnownInstances;

	/** instanced nodes */
	// 实例化节点
	UPROPERTY(transient)
	TArray<UBTNode*> NodeInstances;

	/** search data being currently used */
	FBehaviorTreeSearchData SearchData;

	/** execution request, search will be performed when current task finish execution/aborting */
	// 执行请求，当当前任务完成执行/中止时将执行搜索
	FBTNodeExecutionInfo ExecutionRequest;

	/** result of ExecutionRequest, will be applied when current task finish aborting */
	// ExecutionRequest的结果，当前任务完成终止时会应用
	FBTPendingExecutionInfo PendingExecution;

	/** list of all pending aux nodes unregistration requests */
	// 列出所有未办的aux节点取消注册请求
	FBTPendingAuxNodesUnregisterInfo PendingUnregisterAuxNodesRequests;

	/** stored data for starting new tree, waits until previously running finishes aborting */
	FBTTreeStartInfo TreeStartInfo;

	/** message observers mapped by instance & execution index */
	TMultiMap<FBTNodeIndex,FAIMessageObserverHandle> TaskMessageObservers;

	/** behavior cooldowns mapped by tag to last time it was set */
	TMap<FGameplayTag, float> CooldownTagsMap;

#if USE_BEHAVIORTREE_DEBUGGER
	/** search flow for debugger */
	mutable TArray<TArray<FBehaviorTreeDebuggerInstance::FNodeFlowData> > CurrentSearchFlow;
	mutable TArray<TArray<FBehaviorTreeDebuggerInstance::FNodeFlowData> > CurrentRestarts;
	mutable TMap<FName, FString> SearchStartBlackboard;
	mutable TArray<FBehaviorTreeDebuggerInstance> RemovedInstances;

	/** debugger's recorded data */
	mutable TArray<FBehaviorTreeExecutionStep> DebuggerSteps;

	/** set when at least one debugger window is opened */
	static int32 ActiveDebuggerCounter;
#endif

	// Code for timing BT Search for FramePro
#if !UE_BUILD_SHIPPING
	static bool bAddedEndFrameCallback;
	static double FrameSearchTime;
	static int32 NumSearchTimeCalls;
#endif

	/** index of last active instance on stack */
	// 堆栈上最后一个激活实例的索引
	uint16 ActiveInstanceIdx;

	/** if set, StopTree calls will be deferred */
	// 如果设置，StopTree得调用将延迟
	uint8 StopTreeLock;

	/** if set, StopTree will be called at the end of tick */
	// 如果设置，StopTree将会在tick得结尾调用
	uint8 bDeferredStopTree : 1;

	/** loops tree execution */
	// 循环的树执行
	uint8 bLoopExecution : 1;

	/** set when execution is waiting for tasks to abort (current or parallel's main) */
	// 当执行是等待任务中止时设置（当前或者并行）
	uint8 bWaitingForAbortingTasks : 1;

	/** set when execution update is scheduled for next tick */
	// 设置何时计划下一次执行更新
	uint8 bRequestedFlowUpdate : 1;

	/** set when tree stop was called */
	// 当树的stop函数被调用时设置
	uint8 bRequestedStop : 1;

	/** if set, tree execution is allowed */
	// 运行中，如果设置，树执行已经允许
	uint8 bIsRunning : 1;

	/** if set, execution requests will be postponed */
	// 暂停，如果设置，执行请求将会延迟
	uint8 bIsPaused : 1;

	/** push behavior tree instance on execution stack
	 *	@NOTE: should never be called out-side of BT execution, meaning only BT tasks can push another BT instance! */
	// 在执行堆栈上推送行为树实例
	// 永远不要在BT执行之外被调用，这意味着只有BT任务才能推送另一个BT实例！
	bool PushInstance(UBehaviorTree& TreeAsset);

	/** add unique Id of newly created subtree to KnownInstances list and return its index */
	// 将新创建的子树的唯一ID添加到KnownInstances列表中并返回其索引 
	uint8 UpdateInstanceId(UBehaviorTree* TreeAsset, const UBTNode* OriginNode, int32 OriginInstanceIdx);

	/** remove instanced nodes, known subtree instances and safely clears their persistent memory */
	// 删除实例节点，已知的子树实例和安全地清空持久化内存
	void RemoveAllInstances();

	/** copy memory block from running instances to persistent memory */
	// 从运行实例中将内存拷贝到持久化内存中
	void CopyInstanceMemoryToPersistent();

	/** copy memory block from persistent memory to running instances (rollback) */
	// 将持久化内存内存块拷贝到运行实例中
	void CopyInstanceMemoryFromPersistent();

	/** find next task to execute */
	UBTTaskNode* FindNextTask(UBTCompositeNode* ParentNode, uint16 ParentInstanceIdx, EBTNodeResult::Type LastResult);

	/** called when tree runs out of nodes to execute */
	// 当树执行完所有的节点后调用
	void OnTreeFinished();

	/** apply pending node updates from SearchData */
	void ApplySearchData(UBTNode* NewActiveNode);

	/** apply pending node updates required for discarded search */
	// 通过放弃搜索的方式来应用等待的节点更新要求
	void ApplyDiscardedSearch();

	/** apply updates from specific list */
	// 应用特定列表中的更新 
	void ApplySearchUpdates(const TArray<FBehaviorTreeSearchUpdate>& UpdateList, int32 NewNodeExecutionIndex, bool bPostUpdate = false);

	/** abort currently executed task */
	void AbortCurrentTask();

	/** execute new task */
	void ExecuteTask(UBTTaskNode* TaskNode);

	/** deactivate all nodes up to requested one */
	// 停用所有的节点一直到requested那个
	bool DeactivateUpTo(UBTCompositeNode* Node, uint16 NodeInstanceIdx, EBTNodeResult::Type& NodeResult, int32& OutLastDeactivatedChildIndex);

	/** update state of aborting tasks */
	// 更新中止任务的状态
	void UpdateAbortingTasks();

	/** apply pending execution from last task search */
	// 处理上一个搜索任务中待执行的请求
	void ProcessPendingExecution();

	/** apply pending tree initialization */
	// 应用代办的树初始化 
	void ProcessPendingInitialize();

	/**
	 * apply pending unregister aux nodes requests
	 * @return true if some request were processed, false otherwise
	 */
	bool ProcessPendingUnregister();

	/** restore state of tree to state before search */
	// 恢复树的状态到搜索以前
	void RollbackSearchChanges();

	/** make a snapshot for debugger */
	void StoreDebuggerExecutionStep(EBTExecutionSnap::Type SnapType);

	/** make a snapshot for debugger from given subtree instance */
	void StoreDebuggerInstance(FBehaviorTreeDebuggerInstance& InstanceInfo, uint16 InstanceIdx, EBTExecutionSnap::Type SnapType) const;
	void StoreDebuggerRemovedInstance(uint16 InstanceIdx) const;

	/** store search step for debugger */
	void StoreDebuggerSearchStep(const UBTNode* Node, uint16 InstanceIdx, EBTNodeResult::Type NodeResult) const;
	void StoreDebuggerSearchStep(const UBTNode* Node, uint16 InstanceIdx, bool bPassed) const;

	/** store restarting node for debugger */
	void StoreDebuggerRestart(const UBTNode* Node, uint16 InstanceIdx, bool bAllowed);

	/** describe blackboard's key values */
	void StoreDebuggerBlackboard(TMap<FName, FString>& BlackboardValueDesc) const;

	/** gather nodes runtime descriptions */
	void StoreDebuggerRuntimeValues(TArray<FString>& RuntimeDescriptions, UBTNode* RootNode, uint16 InstanceIdx) const;

	/** update runtime description of given task node in latest debugger's snapshot */
	void UpdateDebuggerAfterExecution(const UBTTaskNode* TaskNode, uint16 InstanceIdx) const;

	/** check if debugger is currently running and can gather data */
	static bool IsDebuggerActive();

	/** Return NodeA's relative priority in regards to NodeB */
	// 返回NodeA对于NodeB的相对优先级
	EBTNodeRelativePriority CalculateRelativePriority(const UBTNode* NodeA, const UBTNode* NodeB) const;

	friend UBTNode;
	friend UBTCompositeNode;
	friend UBTTaskNode;
	friend UBTTask_RunBehavior;
	friend UBTTask_RunBehaviorDynamic;
	friend FBehaviorTreeDebugger;
	friend FBehaviorTreeInstance;
	friend FScopedBehaviorTreeLock;

protected:
	/** data asset defining the tree */
	// 定义的树的资产
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = AI)
	UBehaviorTree* DefaultBehaviorTreeAsset;

	/** Used to tell tickmanager that we want interval ticking */
	// 用于告诉tickmanager，我们需要间隔ticking
	bool bTickedOnce = false;
	/** Predicted next DeltaTime*/
	// 预测下一次的变化时间
	float NextTickDeltaTime = 0.0f;
	/** Accumulated DeltaTime if ticked more than predicted next delta time */
	float AccumulatedTickDeltaTime = 0.0f;
	/** GameTime of the last DeltaTime request, used for debugging to output warnings about ticking */
	// 上一次DeltaTime请求的GameTime
	float LastRequestedDeltaTimeGameTime = 0;

#if CSV_PROFILER
	/** CSV tick stat name. Can be changed but must point to a static string */
	const char* CSVTickStatName = "BehaviorTreeTick";
#endif
};

//////////////////////////////////////////////////////////////////////////
// Inlines

FORCEINLINE UBehaviorTree* UBehaviorTreeComponent::GetCurrentTree() const
{
	return InstanceStack.Num() ? KnownInstances[InstanceStack[ActiveInstanceIdx].InstanceIdIndex].TreeAsset : NULL;
}

// 实例堆栈顶部的树
FORCEINLINE UBehaviorTree* UBehaviorTreeComponent::GetRootTree() const
{
	return InstanceStack.Num() ? KnownInstances[InstanceStack[0].InstanceIdIndex].TreeAsset : NULL;
}

// 返回激活的节点
FORCEINLINE const UBTNode* UBehaviorTreeComponent::GetActiveNode() const
{
	return InstanceStack.Num() ? InstanceStack[ActiveInstanceIdx].ActiveNode : NULL;
}

FORCEINLINE uint16 UBehaviorTreeComponent::GetActiveInstanceIdx() const
{
	return ActiveInstanceIdx;
}

FORCEINLINE bool UBehaviorTreeComponent::IsRestartPending() const
{
	return ExecutionRequest.ExecuteNode && !ExecutionRequest.bTryNextChild;
}

FORCEINLINE bool UBehaviorTreeComponent::IsAbortPending() const
{
	return bWaitingForAbortingTasks || PendingExecution.IsSet();
}
