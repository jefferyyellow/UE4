// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardAssetProvider.h"
#include "GameplayTaskOwnerInterface.h"
#include "Tasks/AITask.h"
#include "BTNode.generated.h"

class AActor;
class UBehaviorTree;
class UBlackboardData;
class UBTCompositeNode;
class UGameplayTasksComponent;

AIMODULE_API DECLARE_LOG_CATEGORY_EXTERN(LogBehaviorTree, Display, All);

class AAIController;
class UWorld;
class UBehaviorTree;
class UBehaviorTreeComponent;
class UBTCompositeNode;
class UBlackboardData;
struct FBehaviorTreeSearchData;

struct FBTInstancedNodeMemory
{
	int32 NodeIdx;
};

UCLASS(Abstract,config=Game)
class AIMODULE_API UBTNode : public UObject, public IGameplayTaskOwnerInterface
{
	GENERATED_UCLASS_BODY()
	// 
	virtual UWorld* GetWorld() const override;

	/** fill in data about tree structure */
	// 填写有关树形结构的数据
	void InitializeNode(UBTCompositeNode* InParentNode, uint16 InExecutionIndex, uint16 InMemoryOffset, uint8 InTreeDepth);

	/** initialize any asset related data */
	// 初始化任何资产相关数据
	virtual void InitializeFromAsset(UBehaviorTree& Asset);
	
	/** initialize memory block */
	// 初始化内存块
	virtual void InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryInit::Type InitType) const;

	/** cleanup memory block */
	// 清空内存块
	virtual void CleanupMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryClear::Type CleanupType) const;

	/** gathers description of all runtime parameters */
	// 收集所有运行时参数的描述
	virtual void DescribeRuntimeValues(const UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTDescriptionVerbosity::Type Verbosity, TArray<FString>& Values) const;

	/** size of instance memory */
	// 实例的内存大小
	virtual uint16 GetInstanceMemorySize() const;

	/** called when node instance is added to tree */
	// 将节点实例添加到树时调用 
	virtual void OnInstanceCreated(UBehaviorTreeComponent& OwnerComp);

	/** called when node instance is removed from tree */
	// 从树中删除节点实例时调用
	virtual void OnInstanceDestroyed(UBehaviorTreeComponent& OwnerComp);

	/** called on creating subtree to set up memory and instancing */
	// 创建子树设置内存和实例化时调用
	void InitializeInSubtree(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, int32& NextInstancedIndex, EBTMemoryInit::Type InitType) const;

	/** called on removing subtree to cleanup memory */
	// 调用删除子树来清除内存
	void CleanupInSubtree(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryClear::Type CleanupType) const;

	/** size of special, hidden memory block for internal mechanics */
	// 专用尺寸，由于内部机制隐藏的存储器块的大小
	virtual uint16 GetSpecialMemorySize() const;

#if USE_BEHAVIORTREE_DEBUGGER
	/** fill in data about execution order */
	// 填写有关执行顺序的数据
	void InitializeExecutionOrder(UBTNode* NextNode);

	/** @return next node in execution order */
	// 在执行顺序里面的下一个节点
	UBTNode* GetNextNode() const;
#endif

	template<typename T>
	T* GetNodeMemory(FBehaviorTreeSearchData& SearchData) const;

	template<typename T>
	const T* GetNodeMemory(const FBehaviorTreeSearchData& SearchData) const;

	template<typename T>
	T* GetNodeMemory(FBehaviorTreeInstance& BTInstance) const;

	template<typename T>
	const T* GetNodeMemory(const FBehaviorTreeInstance& BTInstance) const;

	template<typename T>
	T* CastInstanceNodeMemory(uint8* NodeMemory) const;

	/** get special memory block used for hidden shared data (e.g. node instancing) */
	// 获取用于隐藏共享数据的特殊内存块（例如，节点实例化） 
	template<typename T>
	T* GetSpecialNodeMemory(uint8* NodeMemory) const;

	/** @return parent node */
	// 返回父节点
	UBTCompositeNode* GetParentNode() const;

	/** @return name of node */
	// 返回节点名字
	FString GetNodeName() const;

	/** @return execution index */
	// 返回节点的执行索引
	uint16 GetExecutionIndex() const;

	/** @return memory offset */
	// 返回内存偏移
	uint16 GetMemoryOffset() const;

	/** @return depth in tree */
	// 返回树深度
	uint8 GetTreeDepth() const;

	/** sets bIsInjected flag, do NOT call this function unless you really know what you are doing! */
	// 设置bIsInjected，除非您真的知道自己在做什么，否则不要调用此函数！ 
	void MarkInjectedNode();

	/** @return true if node was injected by subtree */
	// 如果节点是由子树注入的，则为true
	bool IsInjected() const;

	/** sets bCreateNodeInstance flag, do NOT call this function on already pushed tree instance! */
	// 设置bCreateNodeInstance标志，请勿在已推送的树实例上调用此函数 
	void ForceInstancing(bool bEnable);

	/** @return true if node wants to be instanced */
	// 如果节点想去实例化，就返回true
	bool HasInstance() const;

	/** @return true if this object is instanced node */
	// 如果此对象是实例节点，则为true
	bool IsInstanced() const;

	/** @return tree asset */
	// 返回树资源
	UBehaviorTree* GetTreeAsset() const;

	/** @return blackboard asset */
	// 返回黑板资源
	UBlackboardData* GetBlackboardAsset() const;

	/** @return node instance if bCreateNodeInstance was set */
	// 如果bCreateNodeInstance设置好，就返回节点实例
	UBTNode* GetNodeInstance(const UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const;
	UBTNode* GetNodeInstance(FBehaviorTreeSearchData& SearchData) const;

	/** @return string containing description of this node instance with all relevant runtime values */
	// 包含此节点实例的描述以及所有相关运行时值的字符串 
	FString GetRuntimeDescription(const UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTDescriptionVerbosity::Type Verbosity) const;

	/** @return string containing description of this node with all setup values */
	// 包含此节点的描述以及所有设置值的字符串
	virtual FString GetStaticDescription() const;

#if WITH_EDITOR
	/** Get the name of the icon used to display this node in the editor */
	// 获取用于在编辑器中显示此节点的图标的名称
	virtual FName GetNodeIconName() const;

	/** Get whether this node is using a blueprint for its logic */
	// 获取此节点是否将蓝图用于其逻辑
	virtual bool UsesBlueprint() const;

	/** Called after creating new node in behavior tree editor, use for versioning */
	// 在行为树编辑器中创建新节点后调用，用于版本控制
	virtual void OnNodeCreated() {}
#endif

	/** Gets called only for instanced nodes(bCreateNodeInstance == true). In practive overridden by BP-implemented BT nodes */
	// 仅针对实例节点获取调用（CreateInstance == true）。 在实践中，由BP实现的BT节点覆盖
	virtual void SetOwner(AActor* ActorOwner) {}

	// BEGIN IGameplayTaskOwnerInterface
	virtual UGameplayTasksComponent* GetGameplayTasksComponent(const UGameplayTask& Task) const override;
	virtual AActor* GetGameplayTaskOwner(const UGameplayTask* Task) const override;
	virtual AActor* GetGameplayTaskAvatar(const UGameplayTask* Task) const override;
	virtual uint8 GetGameplayTaskDefaultPriority() const override;
	virtual void OnGameplayTaskInitialized(UGameplayTask& Task) override;
	// END IGameplayTaskOwnerInterface

	UBehaviorTreeComponent* GetBTComponentForTask(UGameplayTask& Task) const;
	
	template <class T>
	T* NewBTAITask(UBehaviorTreeComponent& BTComponent)
	{
		check(BTComponent.GetAIOwner());
		bOwnsGameplayTasks = true;
		return UAITask::NewAITask<T>(*BTComponent.GetAIOwner(), *this, TEXT("Behavior"));
	}

	/** node name */
	// 节点名称
	UPROPERTY(Category=Description, EditAnywhere)
	FString NodeName;
	
private:

	/** source asset */
	// 源资源
	UPROPERTY()
	UBehaviorTree* TreeAsset;

	/** parent node */
	// 父节点
	UPROPERTY()
	UBTCompositeNode* ParentNode;

#if USE_BEHAVIORTREE_DEBUGGER
	/** next node in execution order */
	// 执行顺序的下一个节点
	UBTNode* NextExecutionNode;
#endif

	/** depth first index (execution order) */
	// 度优先索引（执行顺序）
	uint16 ExecutionIndex;

	/** instance memory offset */
	// 例内存偏移 
	uint16 MemoryOffset;

	/** depth in tree */
	// 树深度
	uint8 TreeDepth;

	/** set automatically for node instances. Should never be set manually */
	// 为节点自动设置实例化。 永远不要手动设置 
	uint8 bIsInstanced : 1;

	/** if set, node is injected by subtree. Should never be set manually */
	// 如果设置，则节点由子树注入。 永远不要手动设置 
	uint8 bIsInjected : 1;

protected:

	/** if set, node will be instanced instead of using memory block and template shared with all other BT components */
	// 如果设置，将实例化节点，而不是使用与所有其他BT组件共享的内存块和模板
	uint8 bCreateNodeInstance : 1;

	/** set to true if task owns any GameplayTasks. Note this requires tasks to be created via NewBTAITask
	 *	Otherwise specific BT task node class is responsible for ending the gameplay tasks on node finish */
	// 如果task拥有任何GameplayTasks，则设置为true。 
	// 请注意，这要求通过NewBTAITask创建任务，否则特定的BT任务节点类负责在节点完成时结束游戏任务
	uint8 bOwnsGameplayTasks : 1;
};

//////////////////////////////////////////////////////////////////////////
// Inlines

FORCEINLINE UBehaviorTree* UBTNode::GetTreeAsset() const
{
	return TreeAsset;
}

FORCEINLINE UBTCompositeNode* UBTNode::GetParentNode() const
{
	return ParentNode;
}

#if USE_BEHAVIORTREE_DEBUGGER
FORCEINLINE UBTNode* UBTNode::GetNextNode() const
{
	return NextExecutionNode;
}
#endif

FORCEINLINE uint16 UBTNode::GetExecutionIndex() const
{
	return ExecutionIndex;
}

FORCEINLINE uint16 UBTNode::GetMemoryOffset() const
{
	return MemoryOffset;
}

FORCEINLINE uint8 UBTNode::GetTreeDepth() const
{
	return TreeDepth;
}

FORCEINLINE void UBTNode::MarkInjectedNode()
{
	bIsInjected = true;
}

FORCEINLINE bool UBTNode::IsInjected() const
{
	return bIsInjected;
}

FORCEINLINE void UBTNode::ForceInstancing(bool bEnable)
{
	// allow only in not initialized trees, side effect: root node always blocked
	check(ParentNode == NULL);

	bCreateNodeInstance = bEnable;
}

FORCEINLINE bool UBTNode::HasInstance() const
{
	return bCreateNodeInstance;
}

FORCEINLINE bool UBTNode::IsInstanced() const
{
	return bIsInstanced;
}

template<typename T>
T* UBTNode::GetNodeMemory(FBehaviorTreeSearchData& SearchData) const
{
	return GetNodeMemory<T>(SearchData.OwnerComp.InstanceStack[SearchData.OwnerComp.GetActiveInstanceIdx()]);
}

template<typename T>
const T* UBTNode::GetNodeMemory(const FBehaviorTreeSearchData& SearchData) const
{
	return GetNodeMemory<T>(SearchData.OwnerComp.InstanceStack[SearchData.OwnerComp.GetActiveInstanceIdx()]);
}

template<typename T>
T* UBTNode::GetNodeMemory(FBehaviorTreeInstance& BTInstance) const
{
	return (T*)(BTInstance.GetInstanceMemory().GetData() + MemoryOffset);
}

template<typename T>
const T* UBTNode::GetNodeMemory(const FBehaviorTreeInstance& BTInstance) const
{
	return (const T*)(BTInstance.GetInstanceMemory().GetData() + MemoryOffset);
}

template<typename T>
T* UBTNode::CastInstanceNodeMemory(uint8* NodeMemory) const
{
	// using '<=' rather than '==' to allow child classes to extend parent's
	// memory class as well (which would make GetInstanceMemorySize return 
	// a value equal or greater to sizeof(T)).
	checkf(sizeof(T) <= GetInstanceMemorySize(), TEXT("Requesting type of %zu bytes but GetInstanceMemorySize returns %u. Make sure GetInstanceMemorySize is implemented properly in %s class hierarchy."), sizeof(T), GetInstanceMemorySize(), *GetFName().ToString());
	return reinterpret_cast<T*>(NodeMemory);
}

template<typename T>
T* UBTNode::GetSpecialNodeMemory(uint8* NodeMemory) const
{
	const int32 SpecialMemorySize = GetSpecialMemorySize();
	return SpecialMemorySize ? (T*)(NodeMemory - ((SpecialMemorySize + 3) & ~3)) : nullptr;
}
