// Copyright Epic Games, Inc. All Rights Reserved.

#include "Evaluation/MovieScenePropertyTemplates.h"
#include "Sections/MovieSceneBoolSection.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Sections/MovieSceneByteSection.h"
#include "Sections/MovieSceneEnumSection.h"
#include "Sections/MovieSceneIntegerSection.h"
#include "Sections/MovieSceneVectorSection.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Sections/MovieSceneStringSection.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "MovieScene.h"
#include "Evaluation/MovieSceneEvaluation.h"
#include "MovieSceneTemplateCommon.h"
#include "Evaluation/Blending/MovieSceneMultiChannelBlending.h"

namespace
{
	FName SanitizeBoolPropertyName(FName InPropertyName)
	{
		FString PropertyVarName = InPropertyName.ToString();
		PropertyVarName.RemoveFromStart("b", ESearchCase::CaseSensitive);
		return FName(*PropertyVarName);
	}
}

//	----------------------------------------------------------------------------
//	Boolean Property Template
FMovieSceneBoolPropertySectionTemplate::FMovieSceneBoolPropertySectionTemplate(const UMovieSceneBoolSection& Section, const UMovieScenePropertyTrack& Track)
	: FMovieScenePropertySectionTemplate(Track.GetPropertyName(), Track.GetPropertyPath().ToString())
	, BoolCurve(Section.GetChannel())
{
	PropertyData.PropertyName = SanitizeBoolPropertyName(PropertyData.PropertyName);
}

void FMovieSceneBoolPropertySectionTemplate::Evaluate(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const
{
	// Only evaluate if the curve has any data
	bool Result = false;
	if (BoolCurve.Evaluate(Context.GetTime(), Result))
	{
		ExecutionTokens.Add(TPropertyTrackExecutionToken<bool>(Result));
	}
}




//	----------------------------------------------------------------------------
//	Byte Property Template
FMovieSceneBytePropertySectionTemplate::FMovieSceneBytePropertySectionTemplate(const UMovieSceneByteSection& Section, const UMovieScenePropertyTrack& Track)
	: FMovieScenePropertySectionTemplate(Track.GetPropertyName(), Track.GetPropertyPath().ToString())
	, ByteCurve(Section.ByteCurve)
{}

void FMovieSceneBytePropertySectionTemplate::Evaluate(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const
{
	uint8 Result = 0;
	if (ByteCurve.Evaluate(Context.GetTime(), Result))
	{
		ExecutionTokens.Add(TPropertyTrackExecutionToken<uint8>(Result));
	}
}


//	----------------------------------------------------------------------------
//	Enum Property Template
FMovieSceneEnumPropertySectionTemplate::FMovieSceneEnumPropertySectionTemplate(const UMovieSceneEnumSection& Section, const UMovieScenePropertyTrack& Track)
	: FMovieScenePropertySectionTemplate(Track.GetPropertyName(), Track.GetPropertyPath().ToString())
	, EnumCurve(Section.EnumCurve)
{}

void FMovieSceneEnumPropertySectionTemplate::Evaluate(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const
{
	uint8 Result = 0;
	if (EnumCurve.Evaluate(Context.GetTime(), Result))
	{
		ExecutionTokens.Add(TPropertyTrackExecutionToken<uint8>(Result));
	}
}


//	----------------------------------------------------------------------------
//	Integer Property Template
FMovieSceneIntegerPropertySectionTemplate::FMovieSceneIntegerPropertySectionTemplate(const UMovieSceneIntegerSection& Section, const UMovieScenePropertyTrack& Track)
	: FMovieScenePropertySectionTemplate(Track.GetPropertyName(), Track.GetPropertyPath().ToString())
	, IntegerCurve(Section.GetChannel())
	, BlendType(Section.GetBlendType().Get())
{}

void FMovieSceneIntegerPropertySectionTemplate::Evaluate(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const
{
	int32 Result = 0;
	if (IntegerCurve.Evaluate(Context.GetTime(), Result))
	{
		// Actuator type ID for this property
		FMovieSceneBlendingActuatorID ActuatorTypeID = EnsureActuator<int32>(ExecutionTokens.GetBlendingAccumulator());

		// Add the blendable to the accumulator
		const float Weight = EvaluateEasing(Context.GetTime());
		ExecutionTokens.BlendToken(ActuatorTypeID, TBlendableToken<int32>(Result, BlendType, Weight));
	}
}

void FMovieSceneIntegerPropertySectionTemplate::Interrogate(const FMovieSceneContext& Context, FMovieSceneInterrogationData& Container, UObject* BindingOverride) const

{
	//using namespace UE::MovieScene;

	//int32 Result = 0.f;

	//// Only evaluate if the curve has any data
	//if (IntegerCurve.Evaluate(Context.GetTime(), Result))
	//{
	//	FMovieSceneAnimTypeID TypeID = GetPropertyTypeID();
	//	static FMovieSceneBlendingActuatorID ActuatorTypeID(TypeID);
	//	if (!Container.GetAccumulator().FindActuator<int32>(ActuatorTypeID))
	//	{
	//		PropertyTemplate::FSectionData SectionData;
	//		SectionData.Initialize(PropertyData.PropertyName, PropertyData.PropertyPath, PropertyData.FunctionName, PropertyData.NotifyFunctionName);
	//		Container.GetAccumulator().DefineActuator(ActuatorTypeID, MakeShared<TPropertyActuator<int32>>(SectionData));
	//	}

	//	const float Weight = EvaluateEasing(Context.GetTime());
	//	Container.GetAccumulator().BlendToken(FMovieSceneEvaluationOperand(), ActuatorTypeID, FMovieSceneEvaluationScope(), Context, TBlendableToken<int32>(Result, BlendType, Weight));
	//}
}


//	----------------------------------------------------------------------------
//	String Property Template
FMovieSceneStringPropertySectionTemplate::FMovieSceneStringPropertySectionTemplate(const UMovieSceneStringSection& Section, const UMovieScenePropertyTrack& Track)
	: FMovieScenePropertySectionTemplate(Track.GetPropertyName(), Track.GetPropertyPath().ToString())
	, StringCurve(Section.GetChannel())
{}

void FMovieSceneStringPropertySectionTemplate::Evaluate(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const
{
	const FString* Result = StringCurve.Evaluate(Context.GetTime());
	if (Result)
	{
		ExecutionTokens.Add(TPropertyTrackExecutionToken<FString>(*Result));
	}
}


//	----------------------------------------------------------------------------
//	Vector Property Template
FMovieSceneVectorPropertySectionTemplate::FMovieSceneVectorPropertySectionTemplate(const UMovieSceneVectorSection& Section, const UMovieScenePropertyTrack& Track)
	: FMovieScenePropertySectionTemplate(Track.GetPropertyName(), Track.GetPropertyPath().ToString())
	, NumChannelsUsed(Section.GetChannelsUsed())
	, BlendType(Section.GetBlendType().Get())
{
	for (int32 Index = 0; Index < NumChannelsUsed; ++Index)
	{
		ComponentCurves[Index] = Section.GetChannel(Index);
	}
}

/** Helper function for evaluating a number of curves for a specific vector type */
template<typename VectorType, uint8 N>
void EvaluateVectorCurve(EMovieSceneBlendType BlendType, float Weight, FFrameTime Time, const FMovieSceneFloatChannel* Channels, FMovieSceneBlendingActuatorID ActuatorTypeID, FMovieSceneExecutionTokens& ExecutionTokens)
{
	UE::MovieScene::TMultiChannelValue<float, N> AnimatedChannels;

	for (uint8 Index = 0; Index < N; ++Index)
	{
		const FMovieSceneFloatChannel& Channel = Channels[Index];
		float Result = 0;
		if (Channel.Evaluate(Time, Result))
		{
			AnimatedChannels.Set(Index, Result);
		}
	}

	// Only blend the token if at least one of the channels was animated
	if (!AnimatedChannels.IsEmpty())
	{
		ExecutionTokens.BlendToken(ActuatorTypeID, TBlendableToken<VectorType>(AnimatedChannels, BlendType, Weight));
	}
}

void FMovieSceneVectorPropertySectionTemplate::Evaluate(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const
{
	FMovieSceneBlendingAccumulator& Accumulator = ExecutionTokens.GetBlendingAccumulator();

	const FFrameTime Time   = Context.GetTime();
	const float      Weight = EvaluateEasing(Context.GetTime());

	switch (NumChannelsUsed)
	{
	case 2:
		EvaluateVectorCurve<FVector2D, 2>(BlendType, Weight, Time, ComponentCurves, EnsureActuator<FVector2D>(Accumulator), ExecutionTokens);
		break;

	case 3:
		EvaluateVectorCurve<FVector, 3>(BlendType, Weight, Time, ComponentCurves, EnsureActuator<FVector>(Accumulator), ExecutionTokens);
		break;

	case 4:
		EvaluateVectorCurve<FVector4, 4>(BlendType, Weight, Time, ComponentCurves, EnsureActuator<FVector4>(Accumulator), ExecutionTokens);
		break;

	default:
		UE_LOG(LogMovieScene, Warning, TEXT("Invalid number of channels(%d) for vector track"), NumChannelsUsed );
		break;
	}
}

/** Helper function for interrogating a number of curves for a specific vector type */
template<typename VectorType, uint8 N>
void InterrogateVectorCurve(const FMovieSceneContext& Context, const FMovieScenePropertySectionData& PropertyData,FMovieSceneBlendingActuatorID ActuatorTypeID, EMovieSceneBlendType BlendType, float Weight, FFrameTime Time, const FMovieSceneFloatChannel* Channels, FMovieSceneInterrogationData& Container)
{
	UE::MovieScene::TMultiChannelValue<float, N> AnimatedChannels;

	if (!Container.GetAccumulator().FindActuator<VectorType>(ActuatorTypeID))
	{
		PropertyTemplate::FSectionData SectionData;
		//SectionData.Initialize(PropertyData.PropertyName, PropertyData.PropertyPath, PropertyData.FunctionName, PropertyData.NotifyFunctionName);
		Container.GetAccumulator().DefineActuator(ActuatorTypeID, MakeShared<TPropertyActuator<VectorType>>(SectionData));
	}

	for (uint8 Index = 0; Index < N; ++Index)
	{
		const FMovieSceneFloatChannel& Channel = Channels[Index];
		float Result = 0;
		if (Channel.Evaluate(Time, Result))
		{
			AnimatedChannels.Set(Index, Result);
		}
	}

	// Only blend the token if at least one of the channels was animated
	if (!AnimatedChannels.IsEmpty())
	{
		Container.GetAccumulator().BlendToken(FMovieSceneEvaluationOperand(), ActuatorTypeID, FMovieSceneEvaluationScope(), Context, TBlendableToken<VectorType>(AnimatedChannels, BlendType, Weight));

	}
}

void FMovieSceneVectorPropertySectionTemplate::Interrogate(const FMovieSceneContext& Context, FMovieSceneInterrogationData& Container, UObject* BindingOverride) const
{
	const FFrameTime Time = Context.GetTime();
	const float      Weight = EvaluateEasing(Context.GetTime());
	FMovieSceneAnimTypeID TypeID = GetPropertyTypeID();
	FMovieSceneBlendingActuatorID ActuatorTypeID(TypeID);

	switch (NumChannelsUsed)
	{
	case 2:
		InterrogateVectorCurve<FVector2D, 2>(Context,  PropertyData, ActuatorTypeID, BlendType, Weight, Time, ComponentCurves, Container);
		break;

	case 3:
		InterrogateVectorCurve<FVector, 3>(Context, PropertyData, ActuatorTypeID, BlendType, Weight, Time, ComponentCurves, Container);
		break;

	case 4:
		InterrogateVectorCurve<FVector4, 4>(Context, PropertyData,  ActuatorTypeID, BlendType, Weight, Time, ComponentCurves, Container);
		break;

	default:
		UE_LOG(LogMovieScene, Warning, TEXT("Invalid number of channels(%d) for vector track"), NumChannelsUsed);
		break;
	}

}