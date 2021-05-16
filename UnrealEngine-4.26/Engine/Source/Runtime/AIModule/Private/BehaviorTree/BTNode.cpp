// Copyright Epic Games, Inc. All Rights Reserved.

#include "BehaviorTree/BTNode.h"
#include "Engine/World.h"
#include "AIController.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BehaviorTree.h"
#include "GameplayTasksComponent.h"
#include "UObject/Package.h"

//----------------------------------------------------------------------//
// UBTNode
//----------------------------------------------------------------------//

UBTNode::UBTNode(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	ParentNode = NULL;
	TreeAsset = NULL;
	ExecutionIndex = 0;
	MemoryOffset = 0;
	TreeDepth = 0;
	bCreateNodeInstance = false;
	bIsInstanced = false;
	bIsInjected = false;

#if USE_BEHAVIORTREE_DEBUGGER
	NextExecutionNode = NULL;
#endif
}

UWorld* UBTNode::GetWorld() const
{
	if (GetOuter() == nullptr)
	{
		return nullptr;
	}

	// Special case for behavior tree nodes in the editor
	// 编辑器中行为树节点的特殊情况
	if (Cast<UPackage>(GetOuter()) != nullptr)
	{
		// GetOuter should return a UPackage and its Outer is a UWorld
		// GetOuter应该返回一个UPackage并且它的Outer就是UWorld
		return Cast<UWorld>(GetOuter()->GetOuter());
	}

	// In all other cases...
	// 其他的情况
	return GetOuter()->GetWorld();
}

// 填写有关树形结构的数据
void UBTNode::InitializeNode(UBTCompositeNode* InParentNode, uint16 InExecutionIndex, uint16 InMemoryOffset, uint8 InTreeDepth)
{
	ParentNode = InParentNode;
	ExecutionIndex = InExecutionIndex;
	MemoryOffset = InMemoryOffset;
	TreeDepth = InTreeDepth;
}

void UBTNode::InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryInit::Type InitType) const
{
	// empty in base 
}

void UBTNode::CleanupMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryClear::Type CleanupType) const
{
	// empty in base 
}

void UBTNode::OnInstanceCreated(UBehaviorTreeComponent& OwnerComp)
{
	// empty in base class
}

void UBTNode::OnInstanceDestroyed(UBehaviorTreeComponent& OwnerComp)
{
	// empty in base class
}

// 创建子树设置内存和实例化时调用
void UBTNode::InitializeInSubtree(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, int32& NextInstancedIndex, EBTMemoryInit::Type InitType) const
{
	// 从节点内存，获得对应的索引，然后初始化
	FBTInstancedNodeMemory* SpecialMemory = GetSpecialNodeMemory<FBTInstancedNodeMemory>(NodeMemory);
	if (SpecialMemory)
	{
		SpecialMemory->NodeIdx = INDEX_NONE;
	}

	// 是否需要实例化节点
	if (bCreateNodeInstance)
	{
		// composite nodes can't be instanced!
		// 复合节点无法被实例化！
		check(IsA(UBTCompositeNode::StaticClass()) == false);

		UBTNode* NodeInstance = OwnerComp.NodeInstances.IsValidIndex(NextInstancedIndex) ? OwnerComp.NodeInstances[NextInstancedIndex] : NULL;
		if (NodeInstance == NULL)
		{
			// 复制Object
			NodeInstance = (UBTNode*)StaticDuplicateObject(this, &OwnerComp);
			// 初始化节点
			NodeInstance->InitializeNode(GetParentNode(), GetExecutionIndex(), GetMemoryOffset(), GetTreeDepth());
			// 初始化完成
			NodeInstance->bIsInstanced = true;
			// 加入节点实例化
			OwnerComp.NodeInstances.Add(NodeInstance);
		}

		check(NodeInstance);
		check(SpecialMemory);

		// 设置节点索引
		SpecialMemory->NodeIdx = NextInstancedIndex;

		NodeInstance->SetOwner(OwnerComp.GetOwner());
		NodeInstance->InitializeMemory(OwnerComp, NodeMemory, InitType);
		check(TreeAsset);
		// 初始化树
		NodeInstance->InitializeFromAsset(*TreeAsset);
		NodeInstance->OnInstanceCreated(OwnerComp);
		// 增加节点实例索引
		NextInstancedIndex++;
	}
	else
	{
		InitializeMemory(OwnerComp, NodeMemory, InitType);
	}
}

// 调用删除子树来清除内存
void UBTNode::CleanupInSubtree(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryClear::Type CleanupType) const
{
	// 如果是否需要实例化节点
	const UBTNode* NodeOb = bCreateNodeInstance ? GetNodeInstance(OwnerComp, NodeMemory) : this;
	if (NodeOb)
	{
		NodeOb->CleanupMemory(OwnerComp, NodeMemory, CleanupType);
	}
}

#if USE_BEHAVIORTREE_DEBUGGER
void UBTNode::InitializeExecutionOrder(UBTNode* NextNode)
{
	NextExecutionNode = NextNode;
}
#endif

// 初始化任何资产相关数据
void UBTNode::InitializeFromAsset(UBehaviorTree& Asset)
{
	TreeAsset = &Asset;
}

// 得到黑板资源
UBlackboardData* UBTNode::GetBlackboardAsset() const
{
	return TreeAsset ? TreeAsset->BlackboardAsset : NULL;
}

uint16 UBTNode::GetInstanceMemorySize() const
{
	return 0;
}

uint16 UBTNode::GetSpecialMemorySize() const
{
	return bCreateNodeInstance ? sizeof(FBTInstancedNodeMemory) : 0;
}

// 得到节点实例
UBTNode* UBTNode::GetNodeInstance(const UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	// 先得到索引
	FBTInstancedNodeMemory* MyMemory = GetSpecialNodeMemory<FBTInstancedNodeMemory>(NodeMemory);
	// 通过索引得到对应的实例
	return MyMemory && OwnerComp.NodeInstances.IsValidIndex(MyMemory->NodeIdx) ?
		OwnerComp.NodeInstances[MyMemory->NodeIdx] : NULL;
}

// 得到节点实例
UBTNode* UBTNode::GetNodeInstance(FBehaviorTreeSearchData& SearchData) const
{
	return GetNodeInstance(SearchData.OwnerComp, GetNodeMemory<uint8>(SearchData));
}

// 得到节点名字
FString UBTNode::GetNodeName() const
{
	return NodeName.Len() ? NodeName : UBehaviorTreeTypes::GetShortTypeName(this);
}

// 得到运行时描述
FString UBTNode::GetRuntimeDescription(const UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTDescriptionVerbosity::Type Verbosity) const
{
	// 基本描述
	FString Description = NodeName.Len() ? FString::Printf(TEXT("%d. %s [%s]"), ExecutionIndex, *NodeName, *GetStaticDescription()) : GetStaticDescription();
	TArray<FString> RuntimeValues;

	// 运行时参数
	const UBTNode* NodeOb = bCreateNodeInstance ? GetNodeInstance(OwnerComp, NodeMemory) : this;
	if (NodeOb)
	{
		NodeOb->DescribeRuntimeValues(OwnerComp, NodeMemory, Verbosity, RuntimeValues);
	}

	// 运行时值
	for (int32 ValueIndex = 0; ValueIndex < RuntimeValues.Num(); ValueIndex++)
	{
		Description += TEXT(", ");
		Description += RuntimeValues[ValueIndex];
	}

	return Description;
}

// 包含此节点的描述以及所有设置值的字符串
FString UBTNode::GetStaticDescription() const
{
	// short type name
	return UBehaviorTreeTypes::GetShortTypeName(this);
}

// 收集所有运行时参数的描述
void UBTNode::DescribeRuntimeValues(const UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTDescriptionVerbosity::Type Verbosity, TArray<FString>& Values) const
{
	// nothing stored in memory for base class
}

#if WITH_EDITOR

FName UBTNode::GetNodeIconName() const
{
	return NAME_None;
}

bool UBTNode::UsesBlueprint() const
{
	return false;
}

#endif

UGameplayTasksComponent* UBTNode::GetGameplayTasksComponent(const UGameplayTask& Task) const
{
	const UAITask* AITask = Cast<const UAITask>(&Task);
	return (AITask && AITask->GetAIController()) ? AITask->GetAIController()->GetGameplayTasksComponent(Task) : Task.GetGameplayTasksComponent();
}

//  得到Task的Avatar的Owner
AActor* UBTNode::GetGameplayTaskOwner(const UGameplayTask* Task) const
{
	if (Task == nullptr)
	{
		if (IsInstanced())
		{
			const UBehaviorTreeComponent* BTComponent = Cast<const UBehaviorTreeComponent>(GetOuter());
			//not having BT component for an instanced BT node is invalid!
			check(BTComponent);
			return BTComponent->GetAIOwner();
		}
		else
		{
			UE_LOG(LogBehaviorTree, Warning, TEXT("%s: Unable to determine default GameplayTaskOwner!"), *GetName());
			return nullptr;
		}
	}

	const UAITask* AITask = Cast<const UAITask>(Task);
	if (AITask)
	{
		return AITask->GetAIController();
	}

	const UGameplayTasksComponent* TasksComponent = Task->GetGameplayTasksComponent();
	return TasksComponent ? TasksComponent->GetGameplayTaskOwner(Task) : nullptr;
}

// 得到Task的Avatar的Owner
AActor* UBTNode::GetGameplayTaskAvatar(const UGameplayTask* Task) const
{
	if (Task == nullptr)
	{
		if (IsInstanced())
		{
			const UBehaviorTreeComponent* BTComponent = Cast<const UBehaviorTreeComponent>(GetOuter());
			//not having BT component for an instanced BT node is invalid!
			check(BTComponent);
			return BTComponent->GetAIOwner();
		}
		else
		{
			UE_LOG(LogBehaviorTree, Warning, TEXT("%s: Unable to determine default GameplayTaskAvatar!"), *GetName());
			return nullptr;
		}
	}

	// 通过AITask,得到Controller，然后得到对应的Pawn
	const UAITask* AITask = Cast<const UAITask>(Task);
	if (AITask)
	{
		return AITask->GetAIController() ? AITask->GetAIController()->GetPawn() : nullptr;
	}

	const UGameplayTasksComponent* TasksComponent = Task->GetGameplayTasksComponent();
	return TasksComponent ? TasksComponent->GetGameplayTaskAvatar(Task) : nullptr;
}

uint8 UBTNode::GetGameplayTaskDefaultPriority() const
{
	return static_cast<uint8>(EAITaskPriority::AutonomousAI);
}

void UBTNode::OnGameplayTaskInitialized(UGameplayTask& Task)
{
	const UAITask* AITask = Cast<const UAITask>(&Task);
	if (AITask && (AITask->GetAIController() == nullptr))
	{
		// this means that the task has either been created without specifying 
		// UAITAsk::OwnerController's value (like via BP's Construct Object node)
		// or it has been created in C++ with inappropriate function
		UE_LOG(LogBehaviorTree, Error, TEXT("Missing AIController in AITask %s"), *AITask->GetName());
	}
}

UBehaviorTreeComponent* UBTNode::GetBTComponentForTask(UGameplayTask& Task) const
{
	UAITask* AITask = Cast<UAITask>(&Task);
	return (AITask && AITask->GetAIController()) ? Cast<UBehaviorTreeComponent>(AITask->GetAIController()->BrainComponent) : nullptr;
}

//----------------------------------------------------------------------//
// DEPRECATED
//----------------------------------------------------------------------//
