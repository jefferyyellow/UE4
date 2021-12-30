// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "BehaviorTree/BTNode.h"
#include "BTCompositeNode.generated.h"

class UBTCompositeNode;
class UBTDecorator;
class UBTService;
class UBTTaskNode;

DECLARE_DELEGATE_RetVal_ThreeParams(int32, FGetNextChildDelegate, FBehaviorTreeSearchData& /*search data*/, int32 /*last child index*/, EBTNodeResult::Type /*last result*/);

struct FBTCompositeMemory
{
	/** index of currently active child node */
	// 当前激活的子节点索引
	int8 CurrentChild;

	/** child override for next selection */
	// 下一次选择的子项覆盖
	int8 OverrideChild;
};

UENUM()
enum class EBTChildIndex : uint8
{
	FirstNode,
	TaskNode,
};

// 装饰逻辑
UENUM()
namespace EBTDecoratorLogic
{
	// keep in sync with DescribeLogicOp() in BTCompositeNode.cpp

	enum Type
	{
		Invalid,
		/** Test decorator conditions. */
		Test,
		/** logic op: AND */
		And,
		/** logic op: OR */
		Or,
		/** logic op: NOT */
		Not,
	};
}

USTRUCT()
struct FBTDecoratorLogic
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	TEnumAsByte<EBTDecoratorLogic::Type> Operation;

	UPROPERTY()
	uint16 Number;

	FBTDecoratorLogic() : Operation(EBTDecoratorLogic::Invalid), Number(0) {}
	FBTDecoratorLogic(uint8 InOperation, uint16 InNumber) : Operation(InOperation), Number(InNumber) {}
};

USTRUCT()
struct FBTCompositeChild
{
	GENERATED_USTRUCT_BODY()

	/** child node */
	// 子复合节点
	UPROPERTY()
	UBTCompositeNode* ChildComposite;

	// 子任务节点
	UPROPERTY()
	UBTTaskNode* ChildTask;

	/** execution decorators */
	// 装饰器节点
	UPROPERTY()
	TArray<UBTDecorator*> Decorators;

	/** logic operations for decorators */
	// 装饰器的逻辑操作
	UPROPERTY()
	TArray<FBTDecoratorLogic> DecoratorOps;
};

UCLASS(Abstract)
class AIMODULE_API UBTCompositeNode : public UBTNode
{
	GENERATED_UCLASS_BODY()

	/** child nodes */
	// 复合子节点，Children成员变量保存所有子节点的实例
	UPROPERTY()
	TArray<FBTCompositeChild> Children;

	/** service nodes */
	// 服务节点
	UPROPERTY()
	TArray<UBTService*> Services;

	/** delegate for finding next child to execute */
	// 查找下一个执行子节点的代理
	UE_DEPRECATED(4.21, "OnNextChild is no longer being used. Please override UBTCompositeNode::GetNextChildHandler instead")
	FGetNextChildDelegate OnNextChild;

	~UBTCompositeNode();

	/** fill in data about tree structure */
	// 填充树结构数据
	void InitializeComposite(uint16 InLastExecutionIndex);

	/** find next child branch to execute */
	// 查找下一个执行的子分支
	int32 FindChildToExecute(FBehaviorTreeSearchData& SearchData, EBTNodeResult::Type& LastResult) const;

	/** get index of child node (handle subtrees) */
	// 得到子节点的索引（处理子树）
	int32 GetChildIndex(FBehaviorTreeSearchData& SearchData, const UBTNode& ChildNode) const;
	/** get index of child node */
	// 得到子节点的得索引
	int32 GetChildIndex(const UBTNode& ChildNode) const;

	/** called before passing search to child node */
	// 传递搜索数据给子节点前调用
	void OnChildActivation(FBehaviorTreeSearchData& SearchData, const UBTNode& ChildNode) const;
	void OnChildActivation(FBehaviorTreeSearchData& SearchData, int32 ChildIndex) const;

	/** called after child has finished search */
	// 子节点搜索结束后调用
	void OnChildDeactivation(FBehaviorTreeSearchData& SearchData, const UBTNode& ChildNode, EBTNodeResult::Type& NodeResult) const;
	void OnChildDeactivation(FBehaviorTreeSearchData& SearchData, int32 ChildIndex, EBTNodeResult::Type& NodeResult) const;

	/** called when start enters this node */
	// 当开始进入该节点时调用
	void OnNodeActivation(FBehaviorTreeSearchData& SearchData) const;

	/** called when search leaves this node */
	// 当搜索离开该节点时调用
	void OnNodeDeactivation(FBehaviorTreeSearchData& SearchData, EBTNodeResult::Type& NodeResult) const;

	/** called when search needs to reactivate this node */
	// 当搜索需要重新激活此节点时调用
	void OnNodeRestart(FBehaviorTreeSearchData& SearchData) const;

	/** notify about task execution start */
	// 任务执行开始的通知
	void ConditionalNotifyChildExecution(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, const UBTNode& ChildNode, EBTNodeResult::Type& NodeResult) const;

	/** size of instance memory */
	// 实例化的内存大小
	virtual uint16 GetInstanceMemorySize() const override;

	/** @return child node at given index */
	// 得到指定索引的子节点
	UBTNode* GetChildNode(int32 Index) const;

	/** @return children count */
	// 子节点数量
	int32 GetChildrenNum() const;

	/** @return execution index of child node */
	// 返回子节点的执行索引
	uint16 GetChildExecutionIndex(int32 Index, EBTChildIndex ChildMode = EBTChildIndex::TaskNode) const;

	/** @return execution index of last node in child branches */
	// 子分支的最后节点的执行索引
	uint16 GetLastExecutionIndex() const;

	/** set override for next child index */
	// 为下一个子索引设置覆盖
	virtual void SetChildOverride(FBehaviorTreeSearchData& SearchData, int8 Index) const;

	/** gathers description of all runtime parameters */
	// 收集所有运行时参数的描述
	virtual void DescribeRuntimeValues(const UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTDescriptionVerbosity::Type Verbosity, TArray<FString>& Values) const override;

	/** check if child node can execute new subtree */
	// 检查子节点是否可以执行新的子树
	virtual bool CanPushSubtree(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, int32 ChildIdx) const;

#if WITH_EDITOR
	/** @return allowed flow abort modes for decorators */
	// 返回装饰器允许的流中断模式
	virtual bool CanAbortLowerPriority() const;
	virtual bool CanAbortSelf() const;
#endif // WITH_EDITOR

	/** find branch containing specified node index */
	// 查找包含指定节点索引的分支
	int32 GetMatchingChildIndex(int32 ActiveInstanceIdx, FBTNodeIndex& NodeIdx) const;

	/** get first execution index of given branch */
	// 获取给定分支的第一个执行索引
	uint16 GetBranchExecutionIndex(uint16 NodeInBranchIdx) const;

	/** is child execution allowed by decorators? */
	// 装饰节点是否允许子树执行
	bool DoDecoratorsAllowExecution(UBehaviorTreeComponent& OwnerComp, int32 InstanceIdx, int32 ChildIdx) const;

	bool IsApplyingDecoratorScope() const;

protected:

	/** if set, all decorators in branch below will be removed when execution flow leaves (decorators on this node are not affected) */
	// 如果设置：当执行流程离开时，所有分支上的的装饰器将被删除（该节点的装饰器不会被影响）
	UPROPERTY(EditAnywhere, Category = Composite)
	uint32 bApplyDecoratorScope : 1;

	/** if set, NotifyChildExecution will be called */
	// 如果设置：NotifyChildExecution将被调用
	uint32 bUseChildExecutionNotify : 1;

	/** if set, NotifyNodeActivation will be called */
	// 如果设置：NotifyNodeActivation将会被调用
	uint32 bUseNodeActivationNotify : 1;

	/** if set, NotifyNodeDeactivation will be called */
	// 如果设置：NotifyNodeDeactivation将会被调用
	uint32 bUseNodeDeactivationNotify : 1;

	/** if set, CanNotifyDecoratorsOnActivation will be called */
	// 如果设置：CanNotifyDecoratorsOnActivation将会别调用
	uint32 bUseDecoratorsActivationCheck : 1;

	/** if set, CanNotifyDecoratorsOnDeactivation will be called */
	// 如果设置：CanNotifyDecoratorsOnDeactivation将会被调用
	uint32 bUseDecoratorsDeactivationCheck : 1;

	/** if set, CanNotifyDecoratorsOnFailedActivation will be called */
	// 如果设置：CanNotifyDecoratorsOnFailedActivation将会被调用
	uint32 bUseDecoratorsFailedActivationCheck : 1;

	/** execution index of last node in child branches */
	// 子分支中最后一个节点的执行索引
	uint16 LastExecutionIndex;

	/** called just after child execution, allows to modify result */
	// 子节点执行之后立马调用，允许修改结果
	virtual void NotifyChildExecution(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, int32 ChildIdx, EBTNodeResult::Type& NodeResult) const;

	/** called when start enters this node */
	// 当开始进入该节点时调用
	virtual void NotifyNodeActivation(FBehaviorTreeSearchData& SearchData) const;

	/** called when start leaves this node */
	// 当开始离开改节点时调用
	virtual void NotifyNodeDeactivation(FBehaviorTreeSearchData& SearchData, EBTNodeResult::Type& NodeResult) const;

	/** check if NotifyDecoratorsOnActivation is allowed, requires bUseDecoratorsActivationCheck flag  */
	// 检查是否允许NotifyDecoratorsOnActivation，要求bUseDecoratorsActivationCheck标志
	virtual bool CanNotifyDecoratorsOnActivation(FBehaviorTreeSearchData& SearchData, int32 ChildIdx) const;

	/** check if NotifyDecoratorsOnDeactivation is allowed, requires bUseDecoratorsDeactivationCheck flag  */
	// 检查是否允许NotifyDecoratorsOnDeactivation，要求bUseDecoratorsDeactivationCheck标志
	virtual bool CanNotifyDecoratorsOnDeactivation(FBehaviorTreeSearchData& SearchData, int32 ChildIdx, EBTNodeResult::Type& NodeResult) const;

	/** check if NotifyDecoratorsOnFailedActivation is allowed, requires bUseDecoratorsActivationCheck flag  */
	// 检查是否允许NotifyDecoratorsOnFailedActivation，要求bUseDecoratorsActivationCheck标志
	virtual bool CanNotifyDecoratorsOnFailedActivation(FBehaviorTreeSearchData& SearchData, int32 ChildIdx, EBTNodeResult::Type& NodeResult) const;

	/** runs through decorators on given child node and notify them about activation */
	// 遍历给定子节点上装饰器并且通知他们激活
	void NotifyDecoratorsOnActivation(FBehaviorTreeSearchData& SearchData, int32 ChildIdx) const;

	/** runs through decorators on given child node and notify them about deactivation */
	// 遍历给定子节点上装饰器并且通知他们停用
	void NotifyDecoratorsOnDeactivation(FBehaviorTreeSearchData& SearchData, int32 ChildIdx, EBTNodeResult::Type& NodeResult) const;

	/** runs through decorators on given child node and notify them about failed activation */
	// 遍历给定子节点上装饰器并且通知他们激活失败
	void NotifyDecoratorsOnFailedActivation(FBehaviorTreeSearchData& SearchData, int32 ChildIdx, EBTNodeResult::Type& NodeResult) const;

	/** get next child to process and store it in CurrentChild */
	// 得到下一个处理的子节点并且将它保存在CurrentChild
	int32 GetNextChild(FBehaviorTreeSearchData& SearchData, int32 LastChildIdx, EBTNodeResult::Type LastResult) const;

	/** store delayed execution request */
	// 保存延迟执行的请求
	void RequestDelayedExecution(UBehaviorTreeComponent& OwnerComp, EBTNodeResult::Type LastResult) const;

protected:
	virtual int32 GetNextChildHandler(struct FBehaviorTreeSearchData& SearchData, int32 PrevChild, EBTNodeResult::Type LastResult) const { return BTSpecialChild::ReturnToParent; }
};


//////////////////////////////////////////////////////////////////////////
// Inlines

FORCEINLINE UBTNode* UBTCompositeNode::GetChildNode(int32 Index) const
{
	return Children.IsValidIndex(Index) ?
		(Children[Index].ChildComposite ?
			(UBTNode*)Children[Index].ChildComposite :
			(UBTNode*)Children[Index].ChildTask) :
		nullptr;
}

FORCEINLINE int32 UBTCompositeNode::GetChildrenNum() const
{
	return Children.Num();
}

FORCEINLINE uint16 UBTCompositeNode::GetLastExecutionIndex() const
{
	return LastExecutionIndex;
}

FORCEINLINE bool UBTCompositeNode::IsApplyingDecoratorScope() const
{
	return bApplyDecoratorScope;
}
