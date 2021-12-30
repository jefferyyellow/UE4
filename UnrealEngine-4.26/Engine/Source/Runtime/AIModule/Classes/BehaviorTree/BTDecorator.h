// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "BehaviorTree/BTAuxiliaryNode.h"
#include "BTDecorator.generated.h"

class FBehaviorDecoratorDetails;

enum class EBTDecoratorAbortRequest : uint8
{
	// request execution update when only result of condition changes and active branch of tree can potentially change too
	// 只有条件变化的结果和树的活动分支也可能发生变化时，请求执行更新
	ConditionResultChanged,

	// request execution update every time as long as condition is still passing
	// 只要条件仍然通过，每次都请求执行更新
	ConditionPassing,
};

/** 
 * Decorators are supporting nodes placed on parent-child connection, that receive notification about execution flow and can be ticked
 *
 * Because some of them can be instanced for specific AI, following virtual functions are not marked as const:
 *  - OnNodeActivation
 *  - OnNodeDeactivation
 *  - OnNodeProcessed
 *  - OnBecomeRelevant (from UBTAuxiliaryNode)
 *  - OnCeaseRelevant (from UBTAuxiliaryNode)
 *  - TickNode (from UBTAuxiliaryNode)
 *
 * If your node is not being instanced (default behavior), DO NOT change any properties of object within those functions!
 * Template nodes are shared across all behavior tree components using the same tree asset and must store
 * their runtime properties in provided NodeMemory block (allocation size determined by GetInstanceMemorySize() )
 *
 */

UCLASS(Abstract)
class AIMODULE_API UBTDecorator : public UBTAuxiliaryNode
{
	GENERATED_UCLASS_BODY()

	/** wrapper for node instancing: CalculateRawConditionValue */
	// 实例化节点包装函数：CalculateRawConditionValue
	bool WrappedCanExecute(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const;

	/** wrapper for node instancing: OnNodeActivation  */
	// 实例化节点包装函数：OnNodeActivation
	void WrappedOnNodeActivation(FBehaviorTreeSearchData& SearchData) const;
	
	/** wrapper for node instancing: OnNodeDeactivation */
	// 实例化节点包装函数：OnNodeDeactivation
	void WrappedOnNodeDeactivation(FBehaviorTreeSearchData& SearchData, EBTNodeResult::Type NodeResult) const;

	/** wrapper for node instancing: OnNodeProcessed */
	// 实例化节点包装函数：OnNodeProcessed
	void WrappedOnNodeProcessed(FBehaviorTreeSearchData& SearchData, EBTNodeResult::Type& NodeResult) const;

	/** @return flow controller's abort mode */
	// 返回流控制中断模式
	EBTFlowAbortMode::Type GetFlowAbortMode() const;

	/** @return true if condition should be inversed */
	// 条件判断是否需要反转
	bool IsInversed() const;
	// 得到静态描述
	virtual FString GetStaticDescription() const override;

	/** modify current flow abort mode, so it can be used with parent composite */
	// 更新当前的流终止模式，它能够和父节点混合使用
	void UpdateFlowAbortMode();

	/** @return true if current abort mode can be used with parent composite */
	// 
	bool IsFlowAbortModeValid() const;

protected:

	/** if set, FlowAbortMode can be set to None */
	// 如果设置：不打断
	uint32 bAllowAbortNone : 1;

	/** if set, FlowAbortMode can be set to LowerPriority and Both */
	// 如果设置：打断低优先级
	uint32 bAllowAbortLowerPri : 1;

	/** if set, FlowAbortMode can be set to Self and Both */
	// 如果设置：打断自己
	uint32 bAllowAbortChildNodes : 1;

	/** if set, OnNodeActivation will be used */
	// 如果设置：OnNodeActivation将会被调用
	uint32 bNotifyActivation : 1;

	/** if set, OnNodeDeactivation will be used */
	// 如果设置：OnNodeDeactivation将会被调用
	uint32 bNotifyDeactivation : 1;

	/** if set, OnNodeProcessed will be used */
	// 如果设置：OnNodeProcessed将会被调用
	uint32 bNotifyProcessed : 1;

	/** if set, static description will include default description of inversed condition */
	// 如果设置，静态描述包括相反条件的默认描述
	uint32 bShowInverseConditionDesc : 1;

private:
	/** if set, condition check result will be inversed */
	// 如果设置，条件检查结果将被反转
	UPROPERTY(Category = Condition, EditAnywhere)
	uint32 bInverseCondition : 1;

protected:
	/** flow controller settings */
	// 流控制设置
	UPROPERTY(Category=FlowControl, EditAnywhere)
	TEnumAsByte<EBTFlowAbortMode::Type> FlowAbortMode;
	// 设置是否反转条件
	void SetIsInversed(bool bShouldBeInversed);

	/** called when underlying node is activated
	  * this function should be considered as const (don't modify state of object) if node is not instanced! */
	// 当底层节点激活时调用
	virtual void OnNodeActivation(FBehaviorTreeSearchData& SearchData);

	/** called when underlying node has finished
	 * this function should be considered as const (don't modify state of object) if node is not instanced! */
	// 当底层节点完成时调用
	virtual void OnNodeDeactivation(FBehaviorTreeSearchData& SearchData, EBTNodeResult::Type NodeResult);

	/** called when underlying node was processed (deactivated or failed to activate)
	 * this function should be considered as const (don't modify state of object) if node is not instanced! */
	// 底层节点处理时调用（停用或激活失败）
	virtual void OnNodeProcessed(FBehaviorTreeSearchData& SearchData, EBTNodeResult::Type& NodeResult);

	/** calculates raw, core value of decorator's condition. Should not include calling IsInversed */
	// 计算原始，核心的装饰节点的条件值。不应该包括相反的条件
	virtual bool CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const;

	/** more "flow aware" version of calling RequestExecution(this) on owning behavior tree component
	 *  should be used in external events that may change result of CalculateRawConditionValue
	 */
	// 应该在可能改变CalculateRawConditionValue结果的外部事件中使用更多“流感知”版本,在拥有行为树组件上调用RequestExecution(this)
	// 条件打断
	void ConditionalFlowAbort(UBehaviorTreeComponent& OwnerComp, EBTDecoratorAbortRequest RequestMode) const;

	friend FBehaviorDecoratorDetails;

	//----------------------------------------------------------------------//
	// DEPRECATED
	//----------------------------------------------------------------------//
	UE_DEPRECATED(4.12, "This function is deprecated, please use InitializeParentLink instead.")
	void InitializeDecorator(uint8 InChildIndex);
};


//////////////////////////////////////////////////////////////////////////
// Inlines

FORCEINLINE EBTFlowAbortMode::Type UBTDecorator::GetFlowAbortMode() const
{
	return FlowAbortMode;
}

FORCEINLINE bool UBTDecorator::IsInversed() const
{
	return bInverseCondition;
}
