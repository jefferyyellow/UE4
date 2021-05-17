// Copyright Epic Games, Inc. All Rights Reserved.

#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BehaviorTree/BehaviorTree.h"
#include "GameFramework/Actor.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Enum.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_NativeEnum.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Rotator.h"
#include "VisualLogger/VisualLogger.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Class.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_String.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BTCompositeNode.h"

//----------------------------------------------------------------------//
// FBehaviorTreeInstance
//----------------------------------------------------------------------//
void FBehaviorTreeInstance::Initialize(UBehaviorTreeComponent& OwnerComp, UBTCompositeNode& Node, int32& InstancedIndex, EBTMemoryInit::Type InitType)
{
	// 初始化子树
	for (int32 ServiceIndex = 0; ServiceIndex < Node.Services.Num(); ServiceIndex++)
	{
		Node.Services[ServiceIndex]->InitializeInSubtree(OwnerComp, Node.Services[ServiceIndex]->GetNodeMemory<uint8>(*this), InstancedIndex, InitType);
	}

	// 初始化根节点
	uint8* NodeMemory = Node.GetNodeMemory<uint8>(*this);
	Node.InitializeInSubtree(OwnerComp, NodeMemory, InstancedIndex, InitType);

	// 得到复合节点
	UBTCompositeNode* InstancedComposite = Cast<UBTCompositeNode>(Node.GetNodeInstance(OwnerComp, NodeMemory));
	if (InstancedComposite)
	{
		InstancedComposite->InitializeComposite(Node.GetLastExecutionIndex());
	}

	// 遍历所有的复合子节点
	for (int32 ChildIndex = 0; ChildIndex < Node.Children.Num(); ChildIndex++)
	{
		FBTCompositeChild& ChildInfo = Node.Children[ChildIndex];
		// 遍历子节点中的装饰节点
		for (int32 DecoratorIndex = 0; DecoratorIndex < ChildInfo.Decorators.Num(); DecoratorIndex++)
		{
			UBTDecorator* DecoratorOb = ChildInfo.Decorators[DecoratorIndex];
			uint8* DecoratorMemory = DecoratorOb->GetNodeMemory<uint8>(*this);
			// 创建子树设置内存和实例化时调用
			DecoratorOb->InitializeInSubtree(OwnerComp, DecoratorMemory, InstancedIndex, InitType);

			UBTDecorator* InstancedDecoratorOb = Cast<UBTDecorator>(DecoratorOb->GetNodeInstance(OwnerComp, DecoratorMemory));
			if (InstancedDecoratorOb)
			{
				InstancedDecoratorOb->InitializeParentLink(DecoratorOb->GetChildIndex());
			}
		}

		// 如果是复合节点，那就递归调用，初始化复合节点
		if (ChildInfo.ChildComposite)
		{
			Initialize(OwnerComp, *(ChildInfo.ChildComposite), InstancedIndex, InitType);
		}
		// 如果是任务节点
		else if (ChildInfo.ChildTask)
		{
			// 初始话任务节点下面的服务节点
			for (int32 ServiceIndex = 0; ServiceIndex < ChildInfo.ChildTask->Services.Num(); ServiceIndex++)
			{
				UBTService* ServiceOb = ChildInfo.ChildTask->Services[ServiceIndex];
				uint8* ServiceMemory = ServiceOb->GetNodeMemory<uint8>(*this);
				ServiceOb->InitializeInSubtree(OwnerComp, ServiceMemory, InstancedIndex, InitType);

				UBTService* InstancedServiceOb = Cast<UBTService>(ServiceOb->GetNodeInstance(OwnerComp, ServiceMemory));
				if (InstancedServiceOb)
				{
					InstancedServiceOb->InitializeParentLink(ServiceOb->GetChildIndex());
				}
			}
			// 初始化任务节点
			ChildInfo.ChildTask->InitializeInSubtree(OwnerComp, ChildInfo.ChildTask->GetNodeMemory<uint8>(*this), InstancedIndex, InitType);
		}
	}
}

void FBehaviorTreeInstance::Cleanup(UBehaviorTreeComponent& OwnerComp, EBTMemoryClear::Type CleanupType)
{
	FBehaviorTreeInstanceId& Info = OwnerComp.KnownInstances[InstanceIdIndex];
	// 取得第一个节点实例的索引
	if (Info.FirstNodeInstance >= 0)
	{
		const int32 MaxAllowedIdx = OwnerComp.NodeInstances.Num();
		// 如果存在下一个树实例，那就取得下一个树实例的开始索引，如果不存在，就使用节点数量作为最大索引
		const int32 LastNodeIdx = OwnerComp.KnownInstances.IsValidIndex(InstanceIdIndex + 1) ?
			FMath::Min(OwnerComp.KnownInstances[InstanceIdIndex + 1].FirstNodeInstance, MaxAllowedIdx) :
			MaxAllowedIdx;

		// 将所有需要销毁的节点调用将要消耗函数
		for (int32 Idx = Info.FirstNodeInstance; Idx < LastNodeIdx; Idx++)
		{
			OwnerComp.NodeInstances[Idx]->OnInstanceDestroyed(OwnerComp);
		}
	}

	// 销毁节点
	CleanupNodes(OwnerComp, *RootNode, CleanupType);

	// remove memory when instance is destroyed - it will need full initialize anyway
	// 如果实例销毁就删除内存，无论如何，它将需要完全初始化
	if (CleanupType == EBTMemoryClear::Destroy)
	{
		Info.InstanceMemory.Empty();
	}
	else
	{
		Info.InstanceMemory = InstanceMemory;
	}
}

// 清除所有的节点
void FBehaviorTreeInstance::CleanupNodes(UBehaviorTreeComponent& OwnerComp, UBTCompositeNode& Node, EBTMemoryClear::Type CleanupType)
{
	// 清除所有的服务节点
	for (int32 ServiceIndex = 0; ServiceIndex < Node.Services.Num(); ServiceIndex++)
	{
		Node.Services[ServiceIndex]->CleanupInSubtree(OwnerComp, Node.Services[ServiceIndex]->GetNodeMemory<uint8>(*this), CleanupType);
	}

	// 清除当前节点的子树
	Node.CleanupInSubtree(OwnerComp, Node.GetNodeMemory<uint8>(*this), CleanupType);

	// 遍历所有的子节点
	for (int32 ChildIndex = 0; ChildIndex < Node.Children.Num(); ChildIndex++)
	{
		FBTCompositeChild& ChildInfo = Node.Children[ChildIndex];

		// 清除子节点中所有的装饰节点
		for (int32 DecoratorIndex = 0; DecoratorIndex < ChildInfo.Decorators.Num(); DecoratorIndex++)
		{
			ChildInfo.Decorators[DecoratorIndex]->CleanupInSubtree(OwnerComp, ChildInfo.Decorators[DecoratorIndex]->GetNodeMemory<uint8>(*this), CleanupType);
		}

		// 如果子节点是复合节点，递归调用
		if (ChildInfo.ChildComposite)
		{
			// 清除节点
			CleanupNodes(OwnerComp, *(ChildInfo.ChildComposite), CleanupType);
		}
		// 如果子节点是任务节点
		else if (ChildInfo.ChildTask)
		{
			// 清除任务节点附带的服务节点
			for (int32 ServiceIndex = 0; ServiceIndex < ChildInfo.ChildTask->Services.Num(); ServiceIndex++)
			{
				ChildInfo.ChildTask->Services[ServiceIndex]->CleanupInSubtree(OwnerComp, ChildInfo.ChildTask->Services[ServiceIndex]->GetNodeMemory<uint8>(*this), CleanupType);
			}

			// 清除任务节点
			ChildInfo.ChildTask->CleanupInSubtree(OwnerComp, ChildInfo.ChildTask->GetNodeMemory<uint8>(*this), CleanupType);
		}
	}
}

#if STATS

void FBehaviorTreeInstance::IncMemoryStats() const
{
	INC_MEMORY_STAT_BY(STAT_AI_BehaviorTree_InstanceMemory, GetAllocatedSize());
}

void FBehaviorTreeInstance::DecMemoryStats() const
{
	DEC_MEMORY_STAT_BY(STAT_AI_BehaviorTree_InstanceMemory, GetAllocatedSize());
}

uint32 FBehaviorTreeInstance::GetAllocatedSize() const
{
	return sizeof(*this) + ActiveAuxNodes.GetAllocatedSize() + ParallelTasks.GetAllocatedSize() + InstanceMemory.GetAllocatedSize();
}

#define MEM_STAT_UPDATE_WRAPPER(cmd) \
	DecMemoryStats();\
	cmd; \
	IncMemoryStats();

#else

#define MEM_STAT_UPDATE_WRAPPER(cmd) cmd;

#endif // STATS

FBehaviorTreeInstance::FBehaviorTreeInstance()
{
	IncMemoryStats(); 
	INC_DWORD_STAT(STAT_AI_BehaviorTree_NumInstances);
}

FBehaviorTreeInstance::FBehaviorTreeInstance(const FBehaviorTreeInstance& Other)
{ 
	*this = Other; 
	IncMemoryStats();
	INC_DWORD_STAT(STAT_AI_BehaviorTree_NumInstances);
}

FBehaviorTreeInstance::FBehaviorTreeInstance(int32 MemorySize)
{
	InstanceMemory.AddZeroed(MemorySize);
	IncMemoryStats();
	INC_DWORD_STAT(STAT_AI_BehaviorTree_NumInstances);
}

FBehaviorTreeInstance::~FBehaviorTreeInstance()
{
	DecMemoryStats();
	DEC_DWORD_STAT(STAT_AI_BehaviorTree_NumInstances);
}

// 将指定的节点添加到活动节点列表中
void FBehaviorTreeInstance::AddToActiveAuxNodes(UBTAuxiliaryNode* AuxNode)
{
#if DO_ENSURE
	ensureAlwaysMsgf(bIteratingNodes == false, TEXT("Adding aux node while iterating through them is not allowed."));
#endif // DO_ENSURE
	MEM_STAT_UPDATE_WRAPPER(ActiveAuxNodes.Add(AuxNode));
}

// 从活动节点列表中删除指定的节点
void FBehaviorTreeInstance::RemoveFromActiveAuxNodes(UBTAuxiliaryNode* AuxNode)
{
#if DO_ENSURE
	ensureAlwaysMsgf(bIteratingNodes == false, TEXT("Removing aux node while iterating through them is not allowed."));
#endif // DO_ENSURE
	MEM_STAT_UPDATE_WRAPPER(ActiveAuxNodes.RemoveSingleSwap(AuxNode));
}

// 从活动节点列表删除所有的辅助节点
void FBehaviorTreeInstance::ResetActiveAuxNodes()
{
#if DO_ENSURE
	ensureAlwaysMsgf(bIteratingNodes == false, TEXT("Resetting aux node list while iterating through them is not allowed."));
#endif // DO_ENSURE
	MEM_STAT_UPDATE_WRAPPER(ActiveAuxNodes.Reset());
}

// 活动的并行任务
void FBehaviorTreeInstance::AddToParallelTasks(FBehaviorTreeParallelTask&& ParallelTask)
{
#if DO_ENSURE
	ensureMsgf(ParallelTaskIndex == INDEX_NONE, TEXT("Adding to the the list of parallel tasks from ExecuteOnEachParallelTask is not allowed."));
#endif // DO_ENSURE
	MEM_STAT_UPDATE_WRAPPER(ParallelTasks.Add(ParallelTask));
}

// 在给定的索引上删除一个并行任务
void FBehaviorTreeInstance::RemoveParallelTaskAt(int32 TaskIndex)
{
	check(ParallelTasks.IsValidIndex(TaskIndex));
#if DO_ENSURE
	ensureMsgf(ParallelTaskIndex == INDEX_NONE || ParallelTaskIndex == TaskIndex, 
		TEXT("Removing from the list of parallel tasks from ExecuteOnEachParallelTask is only supported for the current task. Otherwise the iteration is broken."));
#endif // DO_ENSURE

	MEM_STAT_UPDATE_WRAPPER(ParallelTasks.RemoveAt(TaskIndex, /*Count=*/1, /*bAllowShrinking=*/false));
}

// 将给定索引处的并行任务标记为挂起中止
void FBehaviorTreeInstance::MarkParallelTaskAsAbortingAt(int32 TaskIndex)
{
	check(ParallelTasks.IsValidIndex(TaskIndex));
	ParallelTasks[TaskIndex].Status = EBTTaskStatus::Aborting;
}

// 设置实例内存
void FBehaviorTreeInstance::SetInstanceMemory(const TArray<uint8>& Memory)
{
	MEM_STAT_UPDATE_WRAPPER(InstanceMemory = Memory);
}

#undef MEM_STAT_UPDATE_WRAPPER

// 遍历辅助节点并在它们上面调用ExecFunc。迭代期间无法增加或者删除节点
void FBehaviorTreeInstance::ExecuteOnEachAuxNode(TFunctionRef<void(const UBTAuxiliaryNode&)> ExecFunc)
{
#if DO_ENSURE
	TGuardValue<bool> IteratingGuard(bIteratingNodes, true);
#endif // DO_ENSURE

	for (const UBTAuxiliaryNode* AuxNode : ActiveAuxNodes)
	{
		check(AuxNode != NULL);
		ExecFunc(*AuxNode);
	}
}

// 遍历并行任务并且在每个任务上面调用ExecFunc
void FBehaviorTreeInstance::ExecuteOnEachParallelTask(TFunctionRef<void(const FBehaviorTreeParallelTask&, const int32)> ExecFunc)
{
	// calling ExecFunc might unregister parallel task, modifying array we're iterating on - iterator needs to be moved one step back in that case
	// 调用ExecFunc可能会取消注册并行任务，从而修改我们要迭代的数组-在这种情况下，迭代器需要后退一步
	for (int32 Index = 0; Index < ParallelTasks.Num(); ++Index)
	{
		const FBehaviorTreeParallelTask& ParallelTaskInfo = ParallelTasks[Index];
		const UBTTaskNode* CachedParallelTask = ParallelTaskInfo.TaskNode;
		const int32 CachedNumTasks = ParallelTasks.Num();

#if DO_ENSURE
		ensureAlways(ParallelTaskIndex == INDEX_NONE);
		TGuardValue<int32> IndexGuard(ParallelTaskIndex, Index);
#endif // DO_ENSURE

		ExecFunc(ParallelTaskInfo, Index);
		// 检查索引是否正确，并且节点还是原来那个节点
		const bool bIsStillValid = ParallelTasks.IsValidIndex(Index) && (ParallelTaskInfo.TaskNode == CachedParallelTask);
		if (!bIsStillValid)
		{
			// move iterator back if current task was unregistered
			// 如果当前的任务已经取消，就将迭代器往后一步
			Index--;
		}
	}
}

// 检查实例是否具有给定执行索引的活动节点
bool FBehaviorTreeInstance::HasActiveNode(uint16 TestExecutionIndex) const
{
	// 当前活动节点
	if (ActiveNode && ActiveNode->GetExecutionIndex() == TestExecutionIndex)
	{
		return (ActiveNodeType == EBTActiveNode::ActiveTask);
	}

	// 并行任务节点
	for (int32 Idx = 0; Idx < ParallelTasks.Num(); Idx++)
	{
		const FBehaviorTreeParallelTask& ParallelTask = ParallelTasks[Idx];
		if (ParallelTask.TaskNode && ParallelTask.TaskNode->GetExecutionIndex() == TestExecutionIndex)
		{
			return (ParallelTask.Status == EBTTaskStatus::Active);
		}
	}

	// 活动辅组节点
	for (int32 Idx = 0; Idx < ActiveAuxNodes.Num(); Idx++)
	{
		if (ActiveAuxNodes[Idx] && ActiveAuxNodes[Idx]->GetExecutionIndex() == TestExecutionIndex)
		{
			return true;
		}
	}

	return false;
}

// 停用所有活动的辅助节点并从SearchData中删除其请求
void FBehaviorTreeInstance::DeactivateNodes(FBehaviorTreeSearchData& SearchData, uint16 InstanceIndex)
{
	// 从SearchData中删除对应索引的节点
	for (int32 Idx = SearchData.PendingUpdates.Num() - 1; Idx >= 0; Idx--)
	{
		FBehaviorTreeSearchUpdate& UpdateInfo = SearchData.PendingUpdates[Idx];
		if (UpdateInfo.InstanceIndex == InstanceIndex && UpdateInfo.Mode == EBTNodeUpdateMode::Add)
		{
			UE_VLOG(SearchData.OwnerComp.GetOwner(), LogBehaviorTree, Verbose, TEXT("Search node update[%s]: %s"),
				*UBehaviorTreeTypes::DescribeNodeUpdateMode(EBTNodeUpdateMode::Remove),
				*UBehaviorTreeTypes::DescribeNodeHelper(UpdateInfo.AuxNode ? (UBTNode*)UpdateInfo.AuxNode : (UBTNode*)UpdateInfo.TaskNode));

			SearchData.PendingUpdates.RemoveAt(Idx, 1, false);
		}
	}

	// 加入激活的并行任务
	for (int32 Idx = 0; Idx < ParallelTasks.Num(); Idx++)
	{
		const FBehaviorTreeParallelTask& ParallelTask = ParallelTasks[Idx];
		if (ParallelTask.TaskNode && ParallelTask.Status == EBTTaskStatus::Active)
		{
			SearchData.AddUniqueUpdate(FBehaviorTreeSearchUpdate(ParallelTask.TaskNode, InstanceIndex, EBTNodeUpdateMode::Remove));
		}
	}

	// 加入激活的辅助节点
	for (int32 Idx = 0; Idx < ActiveAuxNodes.Num(); Idx++)
	{
		if (ActiveAuxNodes[Idx])
		{
			SearchData.AddUniqueUpdate(FBehaviorTreeSearchUpdate(ActiveAuxNodes[Idx], InstanceIndex, EBTNodeUpdateMode::Remove));
		}
	}
}


//----------------------------------------------------------------------//
// FBTNodeIndex
//----------------------------------------------------------------------//

bool FBTNodeIndex::TakesPriorityOver(const FBTNodeIndex& Other) const
{
	// instance closer to root is more important
	// 离根节点越近越重要
	if (InstanceIndex != Other.InstanceIndex)
	{
		return InstanceIndex < Other.InstanceIndex;
	}

	// higher priority is more important
	// 优先级越高的越重要
	return ExecutionIndex < Other.ExecutionIndex;
}

//----------------------------------------------------------------------//
// FBehaviorTreeSearchData
//----------------------------------------------------------------------//

int32 FBehaviorTreeSearchData::NextSearchId = 1;
// 将更新信息添加到PendingUpdates数组，删除此节点的所有以前的更新 
void FBehaviorTreeSearchData::AddUniqueUpdate(const FBehaviorTreeSearchUpdate& UpdateInfo)
{
	UE_VLOG(OwnerComp.GetOwner(), LogBehaviorTree, Verbose, TEXT("Search node update[%s]: %s"),
		*UBehaviorTreeTypes::DescribeNodeUpdateMode(UpdateInfo.Mode),
		*UBehaviorTreeTypes::DescribeNodeHelper(UpdateInfo.AuxNode ? (UBTNode*)UpdateInfo.AuxNode : (UBTNode*)UpdateInfo.TaskNode));

	// 将原来的删除
	bool bSkipAdding = false;
	for (int32 UpdateIndex = 0; UpdateIndex < PendingUpdates.Num(); UpdateIndex++)
	{
		const FBehaviorTreeSearchUpdate& Info = PendingUpdates[UpdateIndex];
		if (Info.AuxNode == UpdateInfo.AuxNode && Info.TaskNode == UpdateInfo.TaskNode)
		{
			// duplicate, skip
			// 重复，就不用管了吧
			if (Info.Mode == UpdateInfo.Mode)
			{
				// 跳过后面的增加
				bSkipAdding = true;
				break;
			}

			// don't add pairs add-remove
			bSkipAdding = (Info.Mode == EBTNodeUpdateMode::Remove) || (UpdateInfo.Mode == EBTNodeUpdateMode::Remove);

			PendingUpdates.RemoveAt(UpdateIndex, 1, false);
		}
	}
	
	// don't add Remove updates for inactive aux nodes, as they will block valid Add update coming later from the same search
	// check only aux nodes, it happens due to UBTCompositeNode::NotifyDecoratorsOnActivation
	// 不要为非活动aux节点添加Remove更新，因为它们将阻止来自同一搜索的有效Add更新，以后仅检查aux节点，
	// 这是由于UBTCompositeNode :: NotifyDecoratorsOnActivation 
	if (!bSkipAdding && UpdateInfo.Mode == EBTNodeUpdateMode::Remove && UpdateInfo.AuxNode)
	{
		// 检查是否是活动节点，如果是不是活动节点，就跳过
		const bool bIsActive = OwnerComp.IsAuxNodeActive(UpdateInfo.AuxNode, UpdateInfo.InstanceIndex);
		bSkipAdding = !bIsActive;
	}
	// 不能跳过
	if (!bSkipAdding)
	{
		// 增加更新信息
		const int32 Idx = PendingUpdates.Add(UpdateInfo);
		// 是否后置更新
		PendingUpdates[Idx].bPostUpdate = (UpdateInfo.Mode == EBTNodeUpdateMode::Add) && (Cast<UBTService>(UpdateInfo.AuxNode) != NULL);
	}
	else
	{
		UE_VLOG(OwnerComp.GetOwner(), LogBehaviorTree, Verbose, TEXT(">> or not, update skipped"));
	}
}

// 分配搜索ID
void FBehaviorTreeSearchData::AssignSearchId()
{
	SearchId = NextSearchId;
	NextSearchId++;
}

void FBehaviorTreeSearchData::Reset()
{
	PendingUpdates.Reset();
	PendingNotifies.Reset();
	SearchRootNode = FBTNodeIndex();
	SearchStart = FBTNodeIndex();
	SearchEnd = FBTNodeIndex();
	RollbackInstanceIdx = INDEX_NONE;
	DeactivatedBranchStart = FBTNodeIndex();
	DeactivatedBranchEnd = FBTNodeIndex();
	bFilterOutRequestFromDeactivatedBranch = false;
	bSearchInProgress = false;
	bPostponeSearch = false;
	bPreserveActiveNodeMemoryOnRollback = false;
}

//----------------------------------------------------------------------//
// FBlackboardKeySelector
//----------------------------------------------------------------------//
void FBlackboardKeySelector::ResolveSelectedKey(const UBlackboardData& BlackboardAsset)
{
	// 已经设置好了名字
	if (SelectedKeyName.IsNone() == false || !bNoneIsAllowedValue)
	{
		// 选择键名字为空
		if (SelectedKeyName.IsNone() && !bNoneIsAllowedValue)
		{
			InitSelection(BlackboardAsset);
		}

		// 通过名字设置选择键ID
		SelectedKeyID = BlackboardAsset.GetKeyID(SelectedKeyName);
		// 通过ID得到选择键类型
		SelectedKeyType = BlackboardAsset.GetKeyType(SelectedKeyID);
		UE_CLOG(IsSet() == false, LogBehaviorTree, Warning
			, TEXT("%s> Failed to find key \'%s\' in BB asset %s. BB Key Selector will be set to \'Invalid\'")
			, *UBehaviorTreeTypes::GetBTLoggingContext()
			, *SelectedKeyName.ToString()
			, *BlackboardAsset.GetFullName()
		);
	}
}

// 初始化选择
void FBlackboardKeySelector::InitSelection(const UBlackboardData& BlackboardAsset)
{
	// 遍历所有的黑板资源，向上遍历
	for (const UBlackboardData* It = &BlackboardAsset; It; It = It->Parent)
	{
		// 遍历所有的键
		for (int32 KeyIndex = 0; KeyIndex < It->Keys.Num(); KeyIndex++)
		{
			const FBlackboardEntry& EntryInfo = It->Keys[KeyIndex];
			if (EntryInfo.KeyType)
			{
				bool bFilterPassed = true;
				if (AllowedTypes.Num())
				{
					bFilterPassed = false;
					// 考虑所有的可接受的类型
					for (int32 FilterIndex = 0; FilterIndex < AllowedTypes.Num(); FilterIndex++)
					{
						if (EntryInfo.KeyType->IsAllowedByFilter(AllowedTypes[FilterIndex]))
						{
							bFilterPassed = true;
							break;
						}
					}
				}

				// 找到第一个
				if (bFilterPassed)
				{
					SelectedKeyName = EntryInfo.EntryName;
					break;
				}
			}
		}
	}
}

void FBlackboardKeySelector::AddObjectFilter(UObject* Owner, FName PropertyName, TSubclassOf<UObject> AllowedClass)
{
	// 创建一个唯一名字
	const FName FilterName = MakeUniqueObjectName(Owner, UBlackboardKeyType_Object::StaticClass(), *FString::Printf(TEXT("%s_Object"), *PropertyName.ToString()));
	UBlackboardKeyType_Object* FilterOb = NewObject<UBlackboardKeyType_Object>(Owner, FilterName);
	FilterOb->BaseClass = AllowedClass;
	// 允许的类型列表
	AllowedTypes.Add(FilterOb);
}

void FBlackboardKeySelector::AddClassFilter(UObject* Owner, FName PropertyName, TSubclassOf<UObject> AllowedClass)
{
	// 创建一个唯一名字
	const FName FilterName = MakeUniqueObjectName(Owner, UBlackboardKeyType_Class::StaticClass(), *FString::Printf(TEXT("%s_Class"), *PropertyName.ToString()));
	UBlackboardKeyType_Class* FilterOb = NewObject<UBlackboardKeyType_Class>(Owner, FilterName);
	FilterOb->BaseClass = AllowedClass;
	// 允许的类型列表
	AllowedTypes.Add(FilterOb);
}

void FBlackboardKeySelector::AddEnumFilter(UObject* Owner, FName PropertyName, UEnum* AllowedEnum)
{
	// 创建一个唯一名字
	const FName FilterName = MakeUniqueObjectName(Owner, UBlackboardKeyType_Enum::StaticClass(), *FString::Printf(TEXT("%s_Enum"), *PropertyName.ToString()));
	UBlackboardKeyType_Enum* FilterOb = NewObject<UBlackboardKeyType_Enum>(Owner, FilterName);
	FilterOb->EnumType = AllowedEnum;
	// 允许的类型列表
	AllowedTypes.Add(FilterOb);
}

void FBlackboardKeySelector::AddNativeEnumFilter(UObject* Owner, FName PropertyName, const FString& AllowedEnumName)
{
	//  创建一个唯一名字
	const FName FilterName = MakeUniqueObjectName(Owner, UBlackboardKeyType_NativeEnum::StaticClass(), *FString::Printf(TEXT("%s_NativeEnum"), *PropertyName.ToString()));
	UBlackboardKeyType_NativeEnum* FilterOb = NewObject<UBlackboardKeyType_NativeEnum>(Owner, FilterName);
	FilterOb->EnumName = AllowedEnumName;
	// 允许的类型列表
	AllowedTypes.Add(FilterOb);
}

void FBlackboardKeySelector::AddIntFilter(UObject* Owner, FName PropertyName)
{
	const FString FilterName = PropertyName.ToString() + TEXT("_Int");
	// 允许的类型列表
	AllowedTypes.Add(NewObject<UBlackboardKeyType_Int>(Owner, *FilterName));
}

void FBlackboardKeySelector::AddFloatFilter(UObject* Owner, FName PropertyName)
{
	const FString FilterName = PropertyName.ToString() + TEXT("_Float");
	// 允许的类型列表
	AllowedTypes.Add(NewObject<UBlackboardKeyType_Float>(Owner, *FilterName));
}

void FBlackboardKeySelector::AddBoolFilter(UObject* Owner, FName PropertyName)
{
	const FString FilterName = PropertyName.ToString() + TEXT("_Bool");
	// 允许的类型列表
	AllowedTypes.Add(NewObject<UBlackboardKeyType_Bool>(Owner, *FilterName));
}

void FBlackboardKeySelector::AddVectorFilter(UObject* Owner, FName PropertyName)
{
	const FString FilterName = PropertyName.ToString() + TEXT("_Vector");
	// 允许的类型列表
	AllowedTypes.Add(NewObject<UBlackboardKeyType_Vector>(Owner, *FilterName));
}

void FBlackboardKeySelector::AddRotatorFilter(UObject* Owner, FName PropertyName)
{
	const FString FilterName = PropertyName.ToString() + TEXT("_Rotator");
	// 允许的类型列表
	AllowedTypes.Add(NewObject<UBlackboardKeyType_Rotator>(Owner, *FilterName));
}

void FBlackboardKeySelector::AddStringFilter(UObject* Owner, FName PropertyName)
{
	const FString FilterName = PropertyName.ToString() + TEXT("_String");
	// 允许的类型列表
	AllowedTypes.Add(NewObject<UBlackboardKeyType_String>(Owner, *FilterName));
}

void FBlackboardKeySelector::AddNameFilter(UObject* Owner, FName PropertyName)
{
	const FString FilterName = PropertyName.ToString() + TEXT("_Name");
	// 允许的类型列表
	AllowedTypes.Add(NewObject<UBlackboardKeyType_Name>(Owner, *FilterName));
}

// deprecated
void FBlackboardKeySelector::AddClassFilter(UObject* Owner, FName PropertyName, TSubclassOf<UClass> AllowedClass)
{
	TSubclassOf<UObject> AllowedObjectClass = AllowedClass;
	AddClassFilter(Owner, PropertyName, AllowedObjectClass);
}

//----------------------------------------------------------------------//
// UBehaviorTreeTypes
//----------------------------------------------------------------------//
FString UBehaviorTreeTypes::BTLoggingContext;
// 描述节点结果
FString UBehaviorTreeTypes::DescribeNodeResult(EBTNodeResult::Type NodeResult)
{
	static FString ResultDesc[] = { TEXT("Succeeded"), TEXT("Failed"), TEXT("Aborted"), TEXT("InProgress") };
	return (NodeResult < UE_ARRAY_COUNT(ResultDesc)) ? ResultDesc[NodeResult] : FString();
}

// 描述流中断模式
FString UBehaviorTreeTypes::DescribeFlowAbortMode(EBTFlowAbortMode::Type AbortMode)
{
	static FString AbortModeDesc[] = { TEXT("None"), TEXT("Lower Priority"), TEXT("Self"), TEXT("Both") };
	return (AbortMode < UE_ARRAY_COUNT(AbortModeDesc)) ? AbortModeDesc[AbortMode] : FString();
}

// 描述激活节点
FString UBehaviorTreeTypes::DescribeActiveNode(EBTActiveNode::Type ActiveNodeType)
{
	static FString ActiveDesc[] = { TEXT("Composite"), TEXT("ActiveTask"), TEXT("AbortingTask"), TEXT("InactiveTask") };
	return (ActiveNodeType < UE_ARRAY_COUNT(ActiveDesc)) ? ActiveDesc[ActiveNodeType] : FString();
}

// 描述任务状态
FString UBehaviorTreeTypes::DescribeTaskStatus(EBTTaskStatus::Type TaskStatus)
{
	static FString TaskStatusDesc[] = { TEXT("Active"), TEXT("Aborting"), TEXT("Inactive") };
	return (TaskStatus < UE_ARRAY_COUNT(TaskStatusDesc)) ? TaskStatusDesc[TaskStatus] : FString();
}

// 描述更新模式
FString UBehaviorTreeTypes::DescribeNodeUpdateMode(EBTNodeUpdateMode::Type UpdateMode)
{
	static FString UpdateModeDesc[] = { TEXT("Unknown"), TEXT("Add"), TEXT("Remove") };
	return (UpdateMode < UE_ARRAY_COUNT(UpdateModeDesc)) ? UpdateModeDesc[UpdateMode] : FString();
}

// 描述节点帮助
FString UBehaviorTreeTypes::DescribeNodeHelper(const UBTNode* Node)
{
	return Node ? FString::Printf(TEXT("%s::%s[%d]"), *GetNameSafe(Node->GetTreeAsset()), *Node->GetNodeName(), Node->GetExecutionIndex()) : FString();
}

// 得到短类型名字
FString UBehaviorTreeTypes::GetShortTypeName(const UObject* Ob)
{
	if (Ob->GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint))
	{
		return Ob->GetClass()->GetName().LeftChop(2);
	}

	FString TypeDesc = Ob->GetClass()->GetName();
	const int32 ShortNameIdx = TypeDesc.Find(TEXT("_"), ESearchCase::CaseSensitive);
	if (ShortNameIdx != INDEX_NONE)
	{
		TypeDesc.MidInline(ShortNameIdx + 1, MAX_int32, false);
	}

	return TypeDesc;
}

void UBehaviorTreeTypes::SetBTLoggingContext(const UBTNode* NewBTLoggingContext)
{
	BTLoggingContext = NewBTLoggingContext 
		? FString::Printf(TEXT("%s[%d]"), *NewBTLoggingContext->GetNodeName(), NewBTLoggingContext->GetExecutionIndex())
		: TEXT("");
}

//----------------------------------------------------------------------//
// DEPRECATED
//----------------------------------------------------------------------//
