// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_audio.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace LUMIN_MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_audio, const char*, MLAudioGetResultString)
#define MLAudioGetResultString ::LUMIN_MLSDK_API::MLAudioGetResultStringShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioCreateLoadedResource)
#define MLAudioCreateLoadedResource ::LUMIN_MLSDK_API::MLAudioCreateLoadedResourceShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioCreateStreamedResource)
#define MLAudioCreateStreamedResource ::LUMIN_MLSDK_API::MLAudioCreateStreamedResourceShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioCheckResource)
#define MLAudioCheckResource ::LUMIN_MLSDK_API::MLAudioCheckResourceShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetResourceSize)
#define MLAudioCheckReMLAudioGetResourceSizesource ::LUMIN_MLSDK_API::MLAudioGetResourceSizeShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioRefreshResource)
#define MLAudioRefreshResource ::LUMIN_MLSDK_API::MLAudioRefreshResourceShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioDestroyResource)
#define MLAudioDestroyResource ::LUMIN_MLSDK_API::MLAudioDestroyResourceShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioCreateSoundWithLoadedResource)
#define MLAudioCreateSoundWithLoadedResource ::LUMIN_MLSDK_API::MLAudioCreateSoundWithLoadedResourceShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioCreateSoundWithStreamedResource)
#define MLAudioCreateSoundWithStreamedResource ::LUMIN_MLSDK_API::MLAudioCreateSoundWithStreamedResourceShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioCreateSoundWithLoadedFile)
#define MLAudioCreateSoundWithLoadedFile ::LUMIN_MLSDK_API::MLAudioCreateSoundWithLoadedFileShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioCreateSoundWithStreamedFile)
#define MLAudioCreateSoundWithStreamedFile ::LUMIN_MLSDK_API::MLAudioCreateSoundWithStreamedFileShim
CREATE_DEPRECATED_MSG_SHIM(ml_audio, MLResult, MLAudioCreateSoundWithOutputStream, "Replaced by MLCreateSoundWithBufferedOutput.")
#define MLAudioCreateSoundWithOutputStream ::LUMIN_MLSDK_API::MLAudioCreateSoundWithOutputStreamShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioCreateSoundWithBufferedOutput)
#define MLAudioCreateSoundWithBufferedOutput ::LUMIN_MLSDK_API::MLAudioCreateSoundWithBufferedOutputShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioDestroySound)
#define MLAudioDestroySound ::LUMIN_MLSDK_API::MLAudioDestroySoundShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioStartSound)
#define MLAudioStartSound ::LUMIN_MLSDK_API::MLAudioStartSoundShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioStopSound)
#define MLAudioStopSound ::LUMIN_MLSDK_API::MLAudioStopSoundShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioPauseSound)
#define MLAudioPauseSound ::LUMIN_MLSDK_API::MLAudioPauseSoundShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioResumeSound)
#define MLAudioResumeSound ::LUMIN_MLSDK_API::MLAudioResumeSoundShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetSoundState)
#define MLAudioGetSoundState ::LUMIN_MLSDK_API::MLAudioGetSoundStateShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetSoundFormat)
#define MLAudioGetSoundFormat ::LUMIN_MLSDK_API::MLAudioGetSoundFormatShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetSoundEventCallback)
#define MLAudioSetSoundEventCallback ::LUMIN_MLSDK_API::MLAudioSetSoundEventCallbackShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetSpatialSoundEnable)
#define MLAudioSetSpatialSoundEnable ::LUMIN_MLSDK_API::MLAudioSetSpatialSoundEnableShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetSpatialSoundEnable)
#define MLAudioGetSpatialSoundEnable ::LUMIN_MLSDK_API::MLAudioGetSpatialSoundEnableShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetSpatialSoundPosition)
#define MLAudioSetSpatialSoundPosition ::LUMIN_MLSDK_API::MLAudioSetSpatialSoundPositionShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetSpatialSoundPosition)
#define MLAudioGetSpatialSoundPosition ::LUMIN_MLSDK_API::MLAudioGetSpatialSoundPositionShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetSpatialSoundDirection)
#define MLAudioSetSpatialSoundDirection ::LUMIN_MLSDK_API::MLAudioSetSpatialSoundDirectionShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetSpatialSoundDirection)
#define MLAudioGetSpatialSoundDirection ::LUMIN_MLSDK_API::MLAudioGetSpatialSoundDirectionShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetSpatialSoundDistanceProperties)
#define MLAudioSetSpatialSoundDistanceProperties ::LUMIN_MLSDK_API::MLAudioSetSpatialSoundDistancePropertiesShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetSpatialSoundDistanceProperties)
#define MLAudioGetSpatialSoundDistanceProperties ::LUMIN_MLSDK_API::MLAudioGetSpatialSoundDistancePropertiesShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetSpatialSoundRadiationProperties)
#define MLAudioSetSpatialSoundRadiationProperties ::LUMIN_MLSDK_API::MLAudioSetSpatialSoundRadiationPropertiesShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetSpatialSoundRadiationProperties)
#define MLAudioGetSpatialSoundRadiationProperties ::LUMIN_MLSDK_API::MLAudioGetSpatialSoundRadiationPropertiesShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetSpatialSoundDirectSendLevels)
#define MLAudioSetSpatialSoundDirectSendLevels ::LUMIN_MLSDK_API::MLAudioSetSpatialSoundDirectSendLevelsShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetSpatialSoundDirectSendLevels)
#define MLAudioGetSpatialSoundDirectSendLevels ::LUMIN_MLSDK_API::MLAudioGetSpatialSoundDirectSendLevelsShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetSpatialSoundRoomSendLevels)
#define MLAudioSetSpatialSoundRoomSendLevels ::LUMIN_MLSDK_API::MLAudioSetSpatialSoundRoomSendLevelsShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetSpatialSoundRoomSendLevels)
#define MLAudioGetSpatialSoundRoomSendLevels ::LUMIN_MLSDK_API::MLAudioGetSpatialSoundRoomSendLevelsShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetSpatialSoundRoomProperties)
#define MLAudioSetSpatialSoundRoomProperties ::LUMIN_MLSDK_API::MLAudioSetSpatialSoundRoomPropertiesShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetSpatialSoundRoomProperties)
#define MLAudioGetSpatialSoundRoomProperties ::LUMIN_MLSDK_API::MLAudioGetSpatialSoundRoomPropertiesShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetSpatialSoundControlFrequencies)
#define MLAudioSetSpatialSoundControlFrequencies ::LUMIN_MLSDK_API::MLAudioSetSpatialSoundControlFrequenciesShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetSpatialSoundControlFrequencies)
#define MLAudioGetSpatialSoundControlFrequencies ::LUMIN_MLSDK_API::MLAudioGetSpatialSoundControlFrequenciesShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetSpatialSoundHeadRelative)
#define MLAudioSetSpatialSoundHeadRelative ::LUMIN_MLSDK_API::MLAudioSetSpatialSoundHeadRelativeShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioIsSpatialSoundHeadRelative)
#define MLAudioIsSpatialSoundHeadRelative ::LUMIN_MLSDK_API::MLAudioIsSpatialSoundHeadRelativeShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetSoundVolumeLinear)
#define MLAudioSetSoundVolumeLinear ::LUMIN_MLSDK_API::MLAudioSetSoundVolumeLinearShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetSoundVolumeLinear)
#define MLAudioGetSoundVolumeLinear ::LUMIN_MLSDK_API::MLAudioGetSoundVolumeLinearShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetSoundVolumeDb)
#define MLAudioSetSoundVolumeDb ::LUMIN_MLSDK_API::MLAudioSetSoundVolumeDbShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetSoundVolumeDb)
#define MLAudioGetSoundVolumeDb ::LUMIN_MLSDK_API::MLAudioGetSoundVolumeDbShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetSoundPitch)
#define MLAudioSetSoundPitch ::LUMIN_MLSDK_API::MLAudioSetSoundPitchShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetSoundPitch)
#define MLAudioGetSoundPitch ::LUMIN_MLSDK_API::MLAudioGetSoundPitchShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetSoundMute)
#define MLAudioSetSoundMute ::LUMIN_MLSDK_API::MLAudioSetSoundMuteShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioIsSoundMuted)
#define MLAudioIsSoundMuted ::LUMIN_MLSDK_API::MLAudioIsSoundMutedShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetSoundLooping)
#define MLAudioSetSoundLooping ::LUMIN_MLSDK_API::MLAudioSetSoundLoopingShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioIsSoundLooping)
#define MLAudioIsSoundLooping ::LUMIN_MLSDK_API::MLAudioIsSoundLoopingShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetStreamedFileOffset)
#define MLAudioSetStreamedFileOffset ::LUMIN_MLSDK_API::MLAudioSetStreamedFileOffsetShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetStreamedFileOffset)
#define MLAudioGetStreamedFileOffset ::LUMIN_MLSDK_API::MLAudioGetStreamedFileOffsetShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetOutputDevice)
#define MLAudioGetOutputDevice ::LUMIN_MLSDK_API::MLAudioGetOutputDeviceShim
CREATE_DEPRECATED_MSG_SHIM(ml_audio, MLResult, MLAudioGetOutputStreamDefaults, "Replaced by MLAudioGetBufferedOutputDefaults.")
#define MLAudioGetOutputStreamDefaults ::LUMIN_MLSDK_API::MLAudioGetOutputStreamDefaultsShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetBufferedOutputDefaults)
#define MLAudioGetBufferedOutputDefaults ::LUMIN_MLSDK_API::MLAudioGetBufferedOutputDefaultsShim
CREATE_DEPRECATED_MSG_SHIM(ml_audio, MLResult, MLAudioGetOutputStreamLatency, "Replaced by MLAudioGetBufferedOutputLatency.")
#define MLAudioGetOutputStreamLatency ::LUMIN_MLSDK_API::MLAudioGetOutputStreamLatencyShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetBufferedOutputLatency)
#define MLAudioGetBufferedOutputLatency ::LUMIN_MLSDK_API::MLAudioGetBufferedOutputLatencyShim
CREATE_DEPRECATED_MSG_SHIM(ml_audio, MLResult, MLAudioGetOutputStreamFramesPlayed, "Replaced by MLAudioGetBufferedOutputFramesPlayed.")
#define MLAudioGetOutputStreamFramesPlayed ::LUMIN_MLSDK_API::MLAudioGetOutputStreamFramesPlayedShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetBufferedOutputFramesPlayed)
#define MLAudioGetBufferedOutputFramesPlayed ::LUMIN_MLSDK_API::MLAudioGetBufferedOutputFramesPlayedShim
CREATE_DEPRECATED_MSG_SHIM(ml_audio, MLResult, MLAudioGetOutputStreamBuffer, "Replaced by MLAudioGetOutputBuffer.")
#define MLAudioGetOutputStreamBuffer ::LUMIN_MLSDK_API::MLAudioGetOutputStreamBufferShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetOutputBuffer)
#define MLAudioGetOutputBuffer ::LUMIN_MLSDK_API::MLAudioGetOutputBufferShim
CREATE_DEPRECATED_MSG_SHIM(ml_audio, MLResult, MLAudioReleaseOutputStreamBuffer, "Replaced by MLAudioReleaseOutputBuffer.")
#define MLAudioReleaseOutputStreamBuffer ::LUMIN_MLSDK_API::MLAudioReleaseOutputStreamBufferShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioReleaseOutputBuffer)
#define MLAudioReleaseOutputBuffer ::LUMIN_MLSDK_API::MLAudioReleaseOutputBufferShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetMasterVolume)
#define MLAudioGetMasterVolume ::LUMIN_MLSDK_API::MLAudioGetMasterVolumeShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetMasterVolumeCallback)
#define MLAudioSetMasterVolumeCallback ::LUMIN_MLSDK_API::MLAudioSetMasterVolumeCallbackShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioCreateInputFromVoiceComm)
#define MLAudioCreateInputFromVoiceComm ::LUMIN_MLSDK_API::MLAudioCreateInputFromVoiceCommShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioCreateInputFromWorldCapture)
#define MLAudioCreateInputFromWorldCapture ::LUMIN_MLSDK_API::MLAudioCreateInputFromWorldCaptureShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioDestroyInput)
#define MLAudioDestroyInput ::LUMIN_MLSDK_API::MLAudioDestroyInputShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioStartInput)
#define MLAudioStartInput ::LUMIN_MLSDK_API::MLAudioStartInputShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioStopInput)
#define MLAudioStopInput ::LUMIN_MLSDK_API::MLAudioStopInputShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetInputState)
#define MLAudioGetInputState ::LUMIN_MLSDK_API::MLAudioGetInputStateShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetInputEventCallback)
#define MLAudioSetInputEventCallback ::LUMIN_MLSDK_API::MLAudioSetInputEventCallbackShim
CREATE_DEPRECATED_MSG_SHIM(ml_audio, MLResult, MLAudioGetInputStreamDefaults, "Replaced by MLAudioGetBufferedInputDefaults.")
#define MLAudioGetInputStreamDefaults ::LUMIN_MLSDK_API::MLAudioGetInputStreamDefaultsShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetBufferedInputDefaults)
#define MLAudioGetBufferedInputDefaults ::LUMIN_MLSDK_API::MLAudioGetBufferedInputDefaultsShim
CREATE_DEPRECATED_MSG_SHIM(ml_audio, MLResult, MLAudioGetInputStreamLatency, "Replaced by MLAudioGetBufferedInputLatency.")
#define MLAudioGetInputStreamLatency ::LUMIN_MLSDK_API::MLAudioGetInputStreamLatencyShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetBufferedInputLatency)
#define MLAudioGetBufferedInputLatency ::LUMIN_MLSDK_API::MLAudioGetBufferedInputLatencyShim
CREATE_DEPRECATED_MSG_SHIM(ml_audio, MLResult, MLAudioGetInputStreamBuffer, "Replaced by MLAudioGetInputBuffer.")
#define MLAudioGetInputStreamBuffer ::LUMIN_MLSDK_API::MLAudioGetInputStreamBufferShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetInputBuffer)
#define MLAudioGetInputBuffer ::LUMIN_MLSDK_API::MLAudioGetInputBufferShim
CREATE_DEPRECATED_MSG_SHIM(ml_audio, MLResult, MLAudioReleaseInputStreamBuffer, "Replaced by MLAudioReleaseInputBuffer.")
#define MLAudioReleaseInputStreamBuffer ::LUMIN_MLSDK_API::MLAudioReleaseInputStreamBufferShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioReleaseInputBuffer)
#define MLAudioReleaseInputBuffer ::LUMIN_MLSDK_API::MLAudioReleaseInputBufferShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetMicMute)
#define MLAudioSetMicMute ::LUMIN_MLSDK_API::MLAudioSetMicMuteShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioIsMicMuted)
#define MLAudioIsMicMuted ::LUMIN_MLSDK_API::MLAudioIsMicMutedShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetMicMuteCallback)
#define MLAudioSetMicMuteCallback ::LUMIN_MLSDK_API::MLAudioSetMicMuteCallbackShim
CREATE_DEPRECATED_SHIM(ml_audio, MLResult, MLAudioCreateInputFromVirtualCapture)
#define MLAudioCreateInputFromVirtualCapture ::LUMIN_MLSDK_API::MLAudioCreateInputFromVirtualCaptureShim
CREATE_DEPRECATED_SHIM(ml_audio, MLResult, MLAudioCreateInputFromMixedCapture)
#define MLAudioCreateInputFromMixedCapture ::LUMIN_MLSDK_API::MLAudioCreateInputFromMixedCaptureShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
