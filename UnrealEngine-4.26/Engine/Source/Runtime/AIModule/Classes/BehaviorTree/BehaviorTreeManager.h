// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "BehaviorTreeManager.generated.h"

class UBehaviorTree;
class UBehaviorTreeComponent;
class UBTCompositeNode;
class UBTDecorator;

USTRUCT()
struct FBehaviorTreeTemplateInfo
{
	GENERATED_USTRUCT_BODY()

	/** behavior tree asset */
	// 行为树资源
	UPROPERTY()
	UBehaviorTree* Asset;

	/** initialized template */
	// 初始模板
	UPROPERTY(transient)
	UBTCompositeNode* Template;

	/** size required for instance memory */
	// 实例内存的大小
	uint16 InstanceMemorySize;
};

UCLASS(config=Engine, Transient)
class AIMODULE_API UBehaviorTreeManager : public UObject
{
	GENERATED_UCLASS_BODY()

	/** limit for recording execution steps for debugger */
	// 调试器记录的最大执行步骤
	UPROPERTY(config)
	int32 MaxDebuggerSteps;

	/** get behavior tree template for given blueprint */
	// 通过给定的蓝图，得到行为树模板
	bool LoadTree(UBehaviorTree& Asset, UBTCompositeNode*& Root, uint16& InstanceMemorySize);

	/** get aligned memory size */
	// 得到对齐的内存大小
	static int32 GetAlignedDataSize(int32 Size);

	/** helper function for sorting and aligning node memory */
	// 排序和对其节点内存的帮助函数
	static void InitializeMemoryHelper(const TArray<UBTDecorator*>& Nodes, TArray<uint16>& MemoryOffsets, int32& MemorySize, bool bForceInstancing = false);

	/** cleanup hooks for map loading */
	// 清理函数
	virtual void FinishDestroy() override;

	void DumpUsageStats() const;

	/** register new behavior tree component for tracking */
	// 注册新的行为树组件以进行跟踪 
	void AddActiveComponent(UBehaviorTreeComponent& Component);

	/** unregister behavior tree component from tracking */
	// 取消用于跟踪的行为树组件
	void RemoveActiveComponent(UBehaviorTreeComponent& Component);
	// 得到当前的行为树管理器
	static UBehaviorTreeManager* GetCurrent(UWorld* World);
	static UBehaviorTreeManager* GetCurrent(UObject* WorldContextObject);

protected:

	/** initialized tree templates */
	// 已初始化的树模板
	UPROPERTY()
	TArray<FBehaviorTreeTemplateInfo> LoadedTemplates;

	// 行为树组件列表
	UPROPERTY()
	TArray<UBehaviorTreeComponent*> ActiveComponents;
};
