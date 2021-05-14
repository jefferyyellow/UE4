// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Engine/Blueprint.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree.generated.h"

class UBlackboardData;
class UBTDecorator;

UCLASS(BlueprintType)
class AIMODULE_API UBehaviorTree : public UObject, public IBlackboardAssetProvider
{
	GENERATED_UCLASS_BODY()

	/** root node of loaded tree */
	// 加载树的根节点 
	UPROPERTY()
	UBTCompositeNode* RootNode;

#if WITH_EDITORONLY_DATA

	/** Graph for Behavior Tree */
	UPROPERTY()
	class UEdGraph*	BTGraph;

	/** Info about the graphs we last edited */
	UPROPERTY()
	TArray<FEditedDocumentInfo> LastEditedDocuments;

#endif

	// BEGIN IBlackboardAssetProvider
	/** @return blackboard asset */
	virtual UBlackboardData* GetBlackboardAsset() const override;
	// END IBlackboardAssetProvider

	/** blackboard asset for this tree */
	// 这棵树的黑板资产
	UPROPERTY()
	UBlackboardData* BlackboardAsset;

	/** root level decorators, used by subtrees */
	// 子树使用的根级装饰器
	UPROPERTY()
	TArray<UBTDecorator*> RootDecorators;

	/** logic operators for root level decorators, used by subtrees  */
	// 子树使用的根级装饰器的逻辑操作符
	UPROPERTY()
	TArray<FBTDecoratorLogic> RootDecoratorOps;

	/** memory size required for instance of this tree */
	// 此树实例所需的内存大小
	uint16 InstanceMemorySize;
};
