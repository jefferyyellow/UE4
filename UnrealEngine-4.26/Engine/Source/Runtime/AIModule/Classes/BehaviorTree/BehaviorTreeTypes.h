// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "UObject/Class.h"
#include "InputCoreTypes.h"
#include "Templates/SubclassOf.h"
#include "BehaviorTreeTypes.generated.h"

class FBlackboardDecoratorDetails;
class UBehaviorTree;
class UBehaviorTreeComponent;
class UBlackboardComponent;
class UBlackboardData;
class UBlackboardKeyType;
class UBTAuxiliaryNode;
class UBTCompositeNode;
class UBTNode;
class UBTTaskNode;
struct FBehaviorTreeSearchData;

// Visual logging helper
#define BT_VLOG(Context, Verbosity, Format, ...) UE_VLOG(Context->OwnerComp.IsValid() ? Context->OwnerComp->GetOwner() : NULL, LogBehaviorTree, Verbosity, Format, ##__VA_ARGS__)
#define BT_SEARCHLOG(SearchData, Verbosity, Format, ...) UE_VLOG(SearchData.OwnerComp.GetOwner(), LogBehaviorTree, Verbosity, Format, ##__VA_ARGS__)

// Behavior tree debugger in editor
#define USE_BEHAVIORTREE_DEBUGGER	(1 && WITH_EDITORONLY_DATA)

DECLARE_STATS_GROUP(TEXT("Behavior Tree"), STATGROUP_AIBehaviorTree, STATCAT_Advanced);

DECLARE_CYCLE_STAT_EXTERN(TEXT("BT Tick"),STAT_AI_BehaviorTree_Tick,STATGROUP_AIBehaviorTree, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("BT Load Time"),STAT_AI_BehaviorTree_LoadTime,STATGROUP_AIBehaviorTree, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("BT Search Time"),STAT_AI_BehaviorTree_SearchTime,STATGROUP_AIBehaviorTree, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("BT Execution Time"),STAT_AI_BehaviorTree_ExecutionTime,STATGROUP_AIBehaviorTree, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("BT Auxiliary Update Time"),STAT_AI_BehaviorTree_AuxUpdateTime,STATGROUP_AIBehaviorTree, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("BT Cleanup Time"), STAT_AI_BehaviorTree_Cleanup, STATGROUP_AIBehaviorTree, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("BT Stop Tree Time"), STAT_AI_BehaviorTree_StopTree, STATGROUP_AIBehaviorTree, );
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Num Templates"),STAT_AI_BehaviorTree_NumTemplates,STATGROUP_AIBehaviorTree, );
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Num Instances"),STAT_AI_BehaviorTree_NumInstances,STATGROUP_AIBehaviorTree, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("Instance memory"),STAT_AI_BehaviorTree_InstanceMemory,STATGROUP_AIBehaviorTree, AIMODULE_API);

namespace FBlackboard
{
	const FName KeySelf = TEXT("SelfActor");

	typedef uint8 FKey;

	const FKey InvalidKey = FKey(-1);
}

enum class EBlackboardNotificationResult : uint8
{
	RemoveObserver,
	ContinueObserving
};

// delegate defines
DECLARE_DELEGATE_TwoParams(FOnBlackboardChange, const UBlackboardComponent&, FBlackboard::FKey /*key ID*/);
DECLARE_DELEGATE_RetVal_TwoParams(EBlackboardNotificationResult, FOnBlackboardChangeNotification, const UBlackboardComponent&, FBlackboard::FKey /*key ID*/);

namespace BTSpecialChild
{
	const int32 NotInitialized = -1;	// special value for child indices: needs to be initialized
	const int32 ReturnToParent = -2;	// special value for child indices: return to parent node
	
	const uint8 OwnedByComposite = MAX_uint8;	// special value for aux node's child index: owned by composite node instead of a task
}

// 节点执行结果
UENUM(BlueprintType)
namespace EBTNodeResult
{
	// keep in sync with DescribeNodeResult()
	enum Type
	{
		Succeeded,		// finished as success
		Failed,			// finished as failure
		Aborted,		// finished aborting = failure
		InProgress,		// not finished yet
	};
}

// 节点执行模式
namespace EBTExecutionMode
{
	enum Type
	{
		SingleRun,
		Looped,
	};
}

// 节点停止模式
namespace EBTStopMode
{
	enum Type
	{
		Safe,
		Forced,
	};
}

// 内存初始化
namespace EBTMemoryInit
{
	enum Type
	{
		Initialize,		// first time initialization
		RestoreSubtree,	// loading saved data on reentering subtree
	};
}

// 内存清除
namespace EBTMemoryClear
{
	enum Type
	{
		Destroy,		// final clear
		StoreSubtree,	// saving data on leaving subtree
	};
}

UENUM()
namespace EBTFlowAbortMode
{
	// keep in sync with DescribeFlowAbortMode()

	enum Type
	{
		None				UMETA(DisplayName="Nothing"),
		LowerPriority		UMETA(DisplayName="Lower Priority"),
		Self				UMETA(DisplayName="Self"),
		Both				UMETA(DisplayName="Both"),
	};
}

namespace EBTActiveNode
{
	// keep in sync with DescribeActiveNode()
	enum Type
	{
		Composite,
		ActiveTask,
		AbortingTask,
		InactiveTask,
	};
}

namespace EBTTaskStatus
{
	// keep in sync with DescribeTaskStatus()
	enum Type
	{
		Active,
		Aborting,
		Inactive,
	};
}

// 节点更新模式
namespace EBTNodeUpdateMode
{
	// keep in sync with DescribeNodeUpdateMode()
	enum Type
	{
		Unknown,
		Add,				// add node
		Remove,				// remove node
	};
}

/** wrapper struct for holding a parallel task node and its status */
// 包装结构，用于保存并行任务节点及其状态 
struct FBehaviorTreeParallelTask
{
	/** worker object */
	// 任务节点
	const UBTTaskNode* TaskNode;

	/** additional mode data used for context switching */
	// 用于上下文切换的其他模式数据
	EBTTaskStatus::Type Status;

	FBehaviorTreeParallelTask() : TaskNode(NULL) {}
	FBehaviorTreeParallelTask(const UBTTaskNode* InTaskNode, EBTTaskStatus::Type InStatus) : TaskNode(InTaskNode), Status(InStatus) {}

	bool operator==(const FBehaviorTreeParallelTask& Other) const { return TaskNode == Other.TaskNode; }
	bool operator==(const UBTTaskNode* OtherTask) const { return TaskNode == OtherTask; }
};

namespace EBTExecutionSnap
{
	enum Type
	{
		Regular,
		OutOfNodes,
	};
}

// 说明详细度
namespace EBTDescriptionVerbosity
{
	enum Type
	{
		Basic,
		Detailed,
	};
}
// 节点相对优先级
enum class EBTNodeRelativePriority : uint8
{
	Lower,
	Same,
	Higher
};

/** debugger data about subtree instance */
// 子树实例的调试数据
struct FBehaviorTreeDebuggerInstance
{
	struct FNodeFlowData
	{
		uint16 ExecutionIndex;
		uint16 bPassed : 1;
		uint16 bTrigger : 1;
		uint16 bDiscardedTrigger : 1;

		FNodeFlowData() : ExecutionIndex(INDEX_NONE), bPassed(0), bTrigger(0), bDiscardedTrigger(0) {}
	};

	FBehaviorTreeDebuggerInstance() : TreeAsset(NULL), RootNode(NULL) {}

	/** behavior tree asset */
	// 行为树资源
	UBehaviorTree* TreeAsset;

	/** root node in template */
	// 模板根节点
	UBTCompositeNode* RootNode;

	/** execution indices of active nodes */
	// 活动节点的执行索引
	TArray<uint16> ActivePath;

	/** execution indices of active nodes */
	// 活动节点的执行索引 
	TArray<uint16> AdditionalActiveNodes;

	/** search flow from previous state */
	// 来自先前状态的搜索流
	TArray<FNodeFlowData> PathFromPrevious;

	/** runtime descriptions for each execution index */
	// 每个执行索引的运行时描述
	TArray<FString> RuntimeDesc;

	FORCEINLINE bool IsValid() const { return ActivePath.Num() != 0; }
};

/** debugger data about current execution step */
// 当前执行步骤的调试数据
struct FBehaviorTreeExecutionStep
{
	FBehaviorTreeExecutionStep() : TimeStamp(0.f), ExecutionStepId(InvalidExecutionId) {}

	/** subtree instance stack */
	// 子树实例堆栈
	TArray<FBehaviorTreeDebuggerInstance> InstanceStack;

	/** blackboard snapshot: value descriptions */
	// 黑板快照：值描述
	TMap<FName, FString> BlackboardValues;

	/** Game world's time stamp of this step */
	// 此步骤的游戏世界时间戳
	float TimeStamp;

	static constexpr int32 InvalidExecutionId = -1;

	/** Id of execution step */
	// 执行步骤的Id
	int32 ExecutionStepId;
};

/** identifier of subtree instance */
// 子树实例的标识符
struct FBehaviorTreeInstanceId
{
	/** behavior tree asset */
	// 行为树资源
	UBehaviorTree* TreeAsset;

	/** root node in template for cleanup purposes */
	// 用于清理目的的模板中的根节点
	UBTCompositeNode* RootNode;

	/** execution index path from root */
	// 根执行索引路径
	TArray<uint16> Path;

	/** persistent instance memory */
	// 持久实例内存
	TArray<uint8> InstanceMemory;

	/** index of first node instance (BehaviorTreeComponent.NodeInstances) */
	// 第一个节点实例的索引
	int32 FirstNodeInstance;

	FBehaviorTreeInstanceId() : TreeAsset(0), RootNode(0), FirstNodeInstance(-1) {}

	bool operator==(const FBehaviorTreeInstanceId& Other) const
	{
		return (TreeAsset == Other.TreeAsset) && (Path == Other.Path);
	}
};

struct FBehaviorTreeSearchData;
DECLARE_DELEGATE_TwoParams(FBTInstanceDeactivation, UBehaviorTreeComponent&, EBTNodeResult::Type);

/** data required for instance of single subtree */
// 单个子树实例所需的数据
struct FBehaviorTreeInstance
{
	/** root node in template */
	// 模板的根节点
	UBTCompositeNode* RootNode;

	/** active node in template */
	// 模板的激活节点
	UBTNode* ActiveNode;

	/** active auxiliary nodes */
	// 活动的辅助节点
	TArray<UBTAuxiliaryNode*> ActiveAuxNodes;

	/** active parallel tasks */
	// 活动并行任务
	TArray<FBehaviorTreeParallelTask> ParallelTasks;

	/** memory: instance */
	// 内存：实例
	TArray<uint8> InstanceMemory;

	/** index of identifier (BehaviorTreeComponent.KnownInstances) */
	// 识符索引
	uint8 InstanceIdIndex;

	/** active node type */
	// 活动节点类型 
	TEnumAsByte<EBTActiveNode::Type> ActiveNodeType;

	/** delegate sending a notify when tree instance is removed from active stack */
	// 从活动堆栈中删除树实例时,委托会发送通知
	FBTInstanceDeactivation DeactivationNotify;

	AIMODULE_API FBehaviorTreeInstance();
	AIMODULE_API FBehaviorTreeInstance(const FBehaviorTreeInstance& Other);
	AIMODULE_API FBehaviorTreeInstance(int32 MemorySize);
	AIMODULE_API ~FBehaviorTreeInstance();

#if STATS
	void IncMemoryStats() const;
	void DecMemoryStats() const;
	uint32 GetAllocatedSize() const;
#else
	FORCEINLINE uint32 GetAllocatedSize() const { return 0; }
	FORCEINLINE void IncMemoryStats() const {}
	FORCEINLINE void DecMemoryStats() const {}
#endif // STATS

	/** initialize memory and create node instances */
	// 初始化内存并创建节点实例
	void Initialize(UBehaviorTreeComponent& OwnerComp, UBTCompositeNode& Node, int32& InstancedIndex, EBTMemoryInit::Type InitType);

	/** cleanup node instances */
	// 清理节点实例
	void Cleanup(UBehaviorTreeComponent& OwnerComp, EBTMemoryClear::Type CleanupType);

	/** check if instance has active node with given execution index */
	// 检查实例是否具有给定执行索引的活动节点
	bool HasActiveNode(uint16 TestExecutionIndex) const;

	/** deactivate all active aux nodes and remove their requests from SearchData */
	// 停用所有活动的辅助节点并从SearchData中删除其请求
	void DeactivateNodes(FBehaviorTreeSearchData& SearchData, uint16 InstanceIndex);

	/** get list of all active auxiliary nodes */
	// 获取所有活动辅助节点的列表
	TArrayView<UBTAuxiliaryNode* const> GetActiveAuxNodes() const { return ActiveAuxNodes; }

	/** add specified node to the active nodes list */
	// 将指定的节点添加到活动节点列表中
	void AddToActiveAuxNodes(UBTAuxiliaryNode* AuxNode);

	/** remove specified node from the active nodes list */
	// 从活动节点列表中删除指定的节点
	void RemoveFromActiveAuxNodes(UBTAuxiliaryNode* AuxNode);

	/** remove all auxiliary nodes from active nodes list */
	// 从活动节点列表删除所有的辅助节点
	void ResetActiveAuxNodes();

	/** iterate on auxiliary nodes and call ExecFunc on each of them. Nodes can not be added or removed during the iteration */
	// 遍历辅助节点并在它们上面调用ExecFunc。迭代期间无法增加或者删除节点
	void ExecuteOnEachAuxNode(TFunctionRef<void(const UBTAuxiliaryNode&)> ExecFunc);

	/** get list of all active parallel tasks */
	// 获取所有活动并行任务的列表
	TArrayView<const FBehaviorTreeParallelTask> GetParallelTasks() const { return ParallelTasks; }

	/** add new parallel task */
	// 增加一个并行任务
	void AddToParallelTasks(FBehaviorTreeParallelTask&& ParallelTask);

	/** remove parallel task at given index */
	// 在给定的索引上删除一个并行任务
	void RemoveParallelTaskAt(int32 TaskIndex);

	/** mark parallel task at given index as pending abort */
	// 将给定索引处的并行任务标记为挂起中止
	void MarkParallelTaskAsAbortingAt(int32 TaskIndex);

	/** indicates if the provided index is a valid parallel task index */
	// 指示提供的索引是否是有效的并行任务索引
	bool IsValidParallelTaskIndex(const int32 Index) const { return ParallelTasks.IsValidIndex(Index); }

	/** iterate on parallel tasks and call ExecFunc on each of them. Supports removing the iterated task while processed */
	// 遍历并行任务并且在每个任务上面调用ExecFunc。
	void ExecuteOnEachParallelTask(TFunctionRef<void(const FBehaviorTreeParallelTask&, const int32)> ExecFunc);

	/** set instance memory */
	// 设置实例内存
	void SetInstanceMemory(const TArray<uint8>& Memory);

	/** get instance memory */
	// 得到实例内存
	TArrayView<const uint8> GetInstanceMemory() const { return InstanceMemory; }

protected:

	/** worker for updating all nodes */
	void CleanupNodes(UBehaviorTreeComponent& OwnerComp, UBTCompositeNode& Node, EBTMemoryClear::Type CleanupType);

private:
#if DO_ENSURE
	/** debug flag to detect modifications to the array of nodes while iterating through it */
	// 调试标志，以在遍历节点数组时检测对节点数组的修改
	bool bIteratingNodes = false;

	/**
	 * debug flag to detect forbidden modifications to the array of parallel tasks while iterating through it
	 * the only allowed modification is to unregister the task on which the exec function is executed
	 * @see ExecuteOnEachParallelTask
	 */
	// 调试标志，用于检测对并行任务数组的禁止修改，同时对其进行迭代，唯一允许的修改是注销执行exec函数的任务 
	int32 ParallelTaskIndex = INDEX_NONE;
#endif // DO_ENSURE
};

// 节点索引
struct FBTNodeIndex
{
	/** index of instance of stack */
	// 堆栈实例的索引
	uint16 InstanceIndex;

	/** execution index within instance */
	// 实例中的执行索引
	uint16 ExecutionIndex;

	FBTNodeIndex() : InstanceIndex(MAX_uint16), ExecutionIndex(MAX_uint16) {}
	FBTNodeIndex(uint16 InInstanceIndex, uint16 InExecutionIndex) : InstanceIndex(InInstanceIndex), ExecutionIndex(InExecutionIndex) {}

	bool TakesPriorityOver(const FBTNodeIndex& Other) const;
	bool IsSet() const { return InstanceIndex < MAX_uint16; }

	FORCEINLINE bool operator==(const FBTNodeIndex& Other) const { return Other.ExecutionIndex == ExecutionIndex && Other.InstanceIndex == InstanceIndex; }
	FORCEINLINE bool operator!=(const FBTNodeIndex& Other) const { return !operator==(Other); }
	FORCEINLINE friend uint32 GetTypeHash(const FBTNodeIndex& Other) { return Other.ExecutionIndex ^ Other.InstanceIndex; }

	FORCEINLINE FString Describe() const { return FString::Printf(TEXT("[%d:%d]"), InstanceIndex, ExecutionIndex); }
};

// 节点索引范围
struct FBTNodeIndexRange
{
	/** first node index */
	// 第一个节点索引
	FBTNodeIndex FromIndex;

	/** last node index */
	// 最后一个节点索引
	FBTNodeIndex ToIndex;

	FBTNodeIndexRange(const FBTNodeIndex& From, const FBTNodeIndex& To) : FromIndex(From), ToIndex(To) {}

	bool IsSet() const { return FromIndex.IsSet() && ToIndex.IsSet(); }

	bool operator==(const FBTNodeIndexRange& Other) const { return Other.FromIndex == FromIndex && Other.ToIndex == ToIndex; }
	bool operator!=(const FBTNodeIndexRange& Other) const { return !operator==(Other); }

	bool Contains(const FBTNodeIndex& Index) const
	{ 
		return Index.InstanceIndex == FromIndex.InstanceIndex && FromIndex.ExecutionIndex <= Index.ExecutionIndex && Index.ExecutionIndex <= ToIndex.ExecutionIndex;
	}

	FString Describe() const { return FString::Printf(TEXT("[%s...%s]"), *FromIndex.Describe(), *ToIndex.Describe()); }
};

/** node update data */
// 节点更新数据
struct FBehaviorTreeSearchUpdate
{
	UBTAuxiliaryNode* AuxNode;
	UBTTaskNode* TaskNode;

	uint16 InstanceIndex;

	TEnumAsByte<EBTNodeUpdateMode::Type> Mode;

	/** if set, this entry will be applied AFTER other are processed */
	// 如果设置，则此条目将在其他处理之后应用 
	uint8 bPostUpdate : 1;

	FBehaviorTreeSearchUpdate() : AuxNode(0), TaskNode(0), InstanceIndex(0), Mode(EBTNodeUpdateMode::Unknown), bPostUpdate(false) {}
	FBehaviorTreeSearchUpdate(const UBTAuxiliaryNode* InAuxNode, uint16 InInstanceIndex, EBTNodeUpdateMode::Type InMode) :
		AuxNode((UBTAuxiliaryNode*)InAuxNode), TaskNode(0), InstanceIndex(InInstanceIndex), Mode(InMode), bPostUpdate(false)
	{}
	FBehaviorTreeSearchUpdate(const UBTTaskNode* InTaskNode, uint16 InInstanceIndex, EBTNodeUpdateMode::Type InMode) :
		AuxNode(0), TaskNode((UBTTaskNode*)InTaskNode), InstanceIndex(InInstanceIndex), Mode(InMode), bPostUpdate(false)
	{}
};

/** instance notify data */
// 实例通知数据
struct FBehaviorTreeSearchUpdateNotify
{
	uint16 InstanceIndex;
	TEnumAsByte<EBTNodeResult::Type> NodeResult;

	FBehaviorTreeSearchUpdateNotify() : InstanceIndex(0), NodeResult(EBTNodeResult::Succeeded) {}
	FBehaviorTreeSearchUpdateNotify(uint16 InInstanceIndex, EBTNodeResult::Type InNodeResult) : InstanceIndex(InInstanceIndex), NodeResult(InNodeResult) {}
};

/** node search data */
// 节点搜索数据
struct FBehaviorTreeSearchData
{
	/** BT component */
	// 行为树组件
	UBehaviorTreeComponent& OwnerComp;

	/** requested updates of additional nodes (preconditions, services, parallels)
	 *  buffered during search to prevent instant add & remove pairs */
	// 请求更新在搜索过程中为了防止及时添加和删除而缓冲的额外节点（前提条件，服务，并行）
	TArray<FBehaviorTreeSearchUpdate> PendingUpdates;

	/** notifies for tree instances */
	// 树实例的通知
	TArray<FBehaviorTreeSearchUpdateNotify> PendingNotifies;

	/** node under which the search was performed */
	// 执行搜索的节点 
	FBTNodeIndex SearchRootNode;

	/** first node allowed in search */
	// 允许搜索的第一个节点
	FBTNodeIndex SearchStart;

	/** last node allowed in search */
	// 允许搜索的最后一个节点
	FBTNodeIndex SearchEnd;

	/** search unique number */
	// 搜索的唯一号码
	int32 SearchId;

	/** active instance index to rollback to */
	// 回滚到激活的实例索引
	int32 RollbackInstanceIdx;

	/** start index of the deactivated branch */
	// 停用分支的起始索引
	FBTNodeIndex DeactivatedBranchStart;

	/** end index of the deactivated branch */
	// 停用分支的结束索引
	FBTNodeIndex DeactivatedBranchEnd;

	/** saved start index of the deactivated branch for rollback */
	// 保存用于回滚的停用分支的起始索引
	FBTNodeIndex RollbackDeactivatedBranchStart;

	/** saved end index of the deactivated branch for rollback */
	// 保存用于回滚的停用分支的结束索引
	FBTNodeIndex RollbackDeactivatedBranchEnd;

	/** if set, execution request from node in the deactivated branch will be skipped */
	// 如果设置，则将跳过来自停用分支中节点的执行请求
	uint32 bFilterOutRequestFromDeactivatedBranch : 1;

	/** if set, current search will be restarted in next tick */
	// 如果设置，当前的搜索将在下一个tick中重启
	uint32 bPostponeSearch : 1;

	/** set when task search is in progress */
	// 当task还在搜索中就设置
	uint32 bSearchInProgress : 1;

	/** if set, active node state/memory won't be rolled back */
	// 如果设置，激活节点的状态/内存不会被回滚
	uint32 bPreserveActiveNodeMemoryOnRollback : 1;

	/** adds update info to PendingUpdates array, removing all previous updates for this node */
	// 将更新信息添加到PendingUpdates数组，删除此节点的所有以前的更新 
	void AddUniqueUpdate(const FBehaviorTreeSearchUpdate& UpdateInfo);

	/** assign unique Id number */
	// 分配一个唯一的ID
	void AssignSearchId();

	/** clear state of search */
	// 清空搜索状态
	void Reset();

	FBehaviorTreeSearchData(UBehaviorTreeComponent& InOwnerComp) 
		: OwnerComp(InOwnerComp), RollbackInstanceIdx(INDEX_NONE)
		, bFilterOutRequestFromDeactivatedBranch(false)
		, bPostponeSearch(false)
		, bSearchInProgress(false)
		, bPreserveActiveNodeMemoryOnRollback(false)
	{}

	FBehaviorTreeSearchData() = delete;

private:

	static int32 NextSearchId;
};

/** property block in blueprint defined nodes */
// 蓝图定义节点的属性块
struct FBehaviorTreePropertyMemory
{
	uint16 Offset;
	uint16 BlockSize;
	
	FBehaviorTreePropertyMemory() {}
	FBehaviorTreePropertyMemory(int32 Value) : Offset((uint32)Value >> 16), BlockSize((uint32)Value & 0xFFFF) {}

	int32 Pack() const { return (int32)(((uint32)Offset << 16) | BlockSize); }
};

/** helper struct for defining types of allowed blackboard entries
 *  (e.g. only entries holding points and objects derived form actor class) */
// 允许的黑板条目类型的帮助结构
// 比如：仅包含点和对象的条目是从Actor类派生 
USTRUCT(BlueprintType)
struct AIMODULE_API FBlackboardKeySelector
{
	GENERATED_USTRUCT_BODY()

	FBlackboardKeySelector() : SelectedKeyID(FBlackboard::InvalidKey), bNoneIsAllowedValue(false)
	{}

	/** array of allowed types with additional properties (e.g. uobject's base class) 
	  * EditAnywhere is required for FBlackboardSelectorDetails::CacheBlackboardData() */
	// 带有额外属性的允许类型的数组（比如：uobject的基类数组）
	// FBlackboardSelectorDetails::CacheBlackboardData()需要EditAnywhere
	UPROPERTY(transient, EditAnywhere, BlueprintReadWrite, Category = Blackboard)
	TArray<UBlackboardKeyType*> AllowedTypes;

	/** name of selected key */
	// 选择的key的名字
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = Blackboard)
	FName SelectedKeyName;

	/** class of selected key  */
	// 选择key的类
	UPROPERTY(transient, EditInstanceOnly, BlueprintReadWrite, Category = Blackboard)
	TSubclassOf<UBlackboardKeyType> SelectedKeyType;

protected:
	/** ID of selected key */
	// 选择键的ID
	UPROPERTY(transient, EditInstanceOnly, BlueprintReadWrite, Category = Blackboard)
	uint8 SelectedKeyID;
	// SelectedKeyId type should be FBlackboard::FKey, but typedefs are not supported by UHT
	// SelectedKeyId的类型应该和FBlackboard::FKey类型一致，但是UHT不支持typedefs
	static_assert(sizeof(uint8) == sizeof(FBlackboard::FKey), "FBlackboardKeySelector::SelectedKeyId should be of FBlackboard::FKey-compatible type.");

	// Requires BlueprintReadWrite so that blueprint creators (using MakeBlackboardKeySelector) can specify whether or not None is Allowed.
	// 需要BlueprintReadWrite，以便蓝图创建者（使用MakeBlackboardKeySelector）可以指定是否允许无。 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Blackboard, Meta = (Tooltip = ""))
	uint32 bNoneIsAllowedValue:1;

	/** find initial selection. Called when None is not a valid option for this key selector */
	// 找到最初的选择。 当None不是此键选择器的有效选项时调用
	void InitSelection(const UBlackboardData& BlackboardAsset);

public:
	/** find ID and class of selected key */
	// 查找selected key的ID和类型
	void ResolveSelectedKey(const UBlackboardData& BlackboardAsset);
		
	void AllowNoneAsValue(bool bAllow) { bNoneIsAllowedValue = bAllow; }

	FORCEINLINE FBlackboard::FKey GetSelectedKeyID() const { return SelectedKeyID; }

	/** helper functions for setting basic filters */
	// 设置基本过滤器的帮助函数
	void AddObjectFilter(UObject* Owner, FName PropertyName, TSubclassOf<UObject> AllowedClass);
	void AddClassFilter(UObject* Owner, FName PropertyName, TSubclassOf<UObject> AllowedClass);
	void AddEnumFilter(UObject* Owner, FName PropertyName, UEnum* AllowedEnum);
	void AddNativeEnumFilter(UObject* Owner, FName PropertyName, const FString& AllowedEnumName);
	void AddIntFilter(UObject* Owner, FName PropertyName);
	void AddFloatFilter(UObject* Owner, FName PropertyName);
	void AddBoolFilter(UObject* Owner, FName PropertyName);
	void AddVectorFilter(UObject* Owner, FName PropertyName);
	void AddRotatorFilter(UObject* Owner, FName PropertyName);
	void AddStringFilter(UObject* Owner, FName PropertyName);
	void AddNameFilter(UObject* Owner, FName PropertyName);

	FORCEINLINE bool IsNone() const { return bNoneIsAllowedValue && SelectedKeyID == FBlackboard::InvalidKey; }
	FORCEINLINE bool IsSet() const { return SelectedKeyID != FBlackboard::InvalidKey; }
	FORCEINLINE bool NeedsResolving() const { return SelectedKeyID == FBlackboard::InvalidKey && SelectedKeyName.IsNone() == false; }
	FORCEINLINE void InvalidateResolvedKey() { SelectedKeyID = FBlackboard::InvalidKey; }

	friend FBlackboardDecoratorDetails;

	UE_DEPRECATED(4.24, "This version of AddClassFilter is deprecated. Please provide AllowedClass as TSubclassOf<UObject>")
	void AddClassFilter(UObject* Owner, FName PropertyName, TSubclassOf<UClass> AllowedClass);
};

UCLASS(Abstract)
class AIMODULE_API UBehaviorTreeTypes : public UObject
{
	GENERATED_BODY()

	static FString BTLoggingContext;

public:

	static FString DescribeNodeHelper(const UBTNode* Node);

	static FString DescribeNodeResult(EBTNodeResult::Type NodeResult);
	static FString DescribeFlowAbortMode(EBTFlowAbortMode::Type FlowAbortMode);
	static FString DescribeActiveNode(EBTActiveNode::Type ActiveNodeType);
	static FString DescribeTaskStatus(EBTTaskStatus::Type TaskStatus);
	static FString DescribeNodeUpdateMode(EBTNodeUpdateMode::Type UpdateMode);

	/** returns short name of object's class (BTTaskNode_Wait -> Wait) */
	// 返回object's类的简单名字
	static FString GetShortTypeName(const UObject* Ob);
	
	static FString GetBTLoggingContext() { return BTLoggingContext; }
	
	// @param NewBTLoggingContext the object which name's will be added to some of the BT logging
	// 	pass nullptr to clear
	static void SetBTLoggingContext(const UBTNode* NewBTLoggingContext);
};
