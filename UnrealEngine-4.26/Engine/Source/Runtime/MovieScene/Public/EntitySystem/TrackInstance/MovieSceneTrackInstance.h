// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "EntitySystem/MovieSceneInstanceRegistry.h"

#include "MovieSceneTrackInstance.generated.h"

class UMovieSceneSection;
class UMovieSceneEntitySystemLinker;

USTRUCT()
struct FMovieSceneTrackInstanceInput
{
	GENERATED_BODY()

	/** The section that acts as an input for this animator */
	UPROPERTY()
	UMovieSceneSection* Section;

	/** The instance that this input relates to */
	UE::MovieScene::FInstanceHandle InstanceHandle;

	friend bool operator==(const FMovieSceneTrackInstanceInput& A, const FMovieSceneTrackInstanceInput& B)
	{
		return A.Section == B.Section && A.InstanceHandle == B.InstanceHandle;
	}

	friend bool operator<(const FMovieSceneTrackInstanceInput& A, const FMovieSceneTrackInstanceInput& B)
	{
		if (A.Section == B.Section)
		{
			return A.InstanceHandle < B.InstanceHandle;
		}
		return A.Section < B.Section;
	}

	friend uint32 GetTypeHash(const FMovieSceneTrackInstanceInput& Input)
	{
		// Use arbitrary primes to salt this thing.
		uint32 hash = 7211;
		hash = (hash * 307) + GetTypeHash(Input.Section);
		hash = (hash * 307) + GetTypeHash(Input.InstanceHandle);
		return hash;
	}

	/** Serializer that ensures both the section and instance handle are copied over when an input is copied for a reinstanced animator */
	bool Serialize(FArchive& Ar)
	{
		Ar << (UObject*&)Section << InstanceHandle.InstanceID << InstanceHandle.InstanceSerial;
		return true;
	}
};

template<>
struct TStructOpsTypeTraits<FMovieSceneTrackInstanceInput> : public TStructOpsTypeTraitsBase2<FMovieSceneTrackInstanceInput>
{
	enum
	{
		WithIdenticalViaEquality       = true,
		WithSerializer                 = true,
	};
};


/**
 * Base class for all track instances. Can also be used for master tracks where AnimatedObject will be nullptr
 */
UCLASS(transient)
class MOVIESCENE_API UMovieSceneTrackInstance : public UObject
{
public:
	GENERATED_BODY()

	/**
	 * Initialize this instance with the object to animate, and a linker
	 *
	 * @param InAnimatedObject     (optional) The object that we should animate
	 * @param InLinker             The entity system linker that owns this animator. Used for retrieving contexts and player information.
	 */
	void Initialize(UObject* InAnimatedObject, UMovieSceneEntitySystemLinker* InLinker);


	/**
	 * Run this animator for the current frame
	 */
	void Animate();


	/**
	 * Update the inputs that contribute to this animator
	 */
	void UpdateInputs(TArray<FMovieSceneTrackInstanceInput>&& InNewInputs);


	/**
	 * Destroy this animator. The animator may still have inputs when it is destroyed.
	 */
	void Destroy();


protected:


	/**
	 * Retrieve the object that is being animated (may be nullptr for master tracks)
	 */
	UObject* GetAnimatedObject() const
	{
		return AnimatedObject;
	}


	/**
	 * Retrieve the linker that owns this animator
	 */
	UMovieSceneEntitySystemLinker* GetLinker() const
	{
		return Linker;
	}


	/**
	 * Retrieve this animator's inputs
	 */
	TArrayView<const FMovieSceneTrackInstanceInput> GetInputs() const
	{
		return Inputs;
	}

private:

	/** Called when this animator is first created to perform any initialization */
	virtual void OnInitialize() {}

	/** Called when the sequence is updated to apply animation */
	virtual void OnAnimate() {}

	/** Called when this animator is about to have its inputs updated */
	virtual void OnBeginUpdateInputs() {}

	/** Called when this animator has had an input added */
	virtual void OnInputAdded(const FMovieSceneTrackInstanceInput& InInput) {}

	/** Called when this animator has had an input removed */
	virtual void OnInputRemoved(const FMovieSceneTrackInstanceInput& InInput) {}

	/** Called after this animator has finished updating its inputs */
	virtual void OnEndUpdateInputs() {}

	/** Called when this animator is being destroyed in order that it can perform any final fixup */
	virtual void OnDestroyed() {}

	virtual UWorld* GetWorld() const override;

private:

	UPROPERTY()
	UObject* AnimatedObject;

	UPROPERTY()
	bool bIsMasterTrackInstance;

	UPROPERTY()
	UMovieSceneEntitySystemLinker* Linker;

	UPROPERTY()
	TArray<FMovieSceneTrackInstanceInput> Inputs;
};
