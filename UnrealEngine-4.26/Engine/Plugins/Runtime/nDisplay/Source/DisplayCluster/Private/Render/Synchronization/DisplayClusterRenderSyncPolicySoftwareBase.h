// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Synchronization/DisplayClusterRenderSyncPolicyBase.h"


/**
 * Base network synchronization policy (soft sync)
 */
class FDisplayClusterRenderSyncPolicySoftwareBase
	: public FDisplayClusterRenderSyncPolicyBase
{
public:
	FDisplayClusterRenderSyncPolicySoftwareBase(const TMap<FString, FString>& Parameters);
	virtual ~FDisplayClusterRenderSyncPolicySoftwareBase() = 0;
};
