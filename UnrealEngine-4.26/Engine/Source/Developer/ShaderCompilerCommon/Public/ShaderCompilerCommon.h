// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UniformBuffer.h"
#include "ShaderCore.h"
#include "ShaderCompilerCore.h"
#include "CrossCompilerDefinitions.h"

/**
 * This function looks for resources specified in ResourceTableMap in the 
 * parameter map, adds them to the resource table, and removes them from the
 * parameter map. If a resource is used from a currently unmapped uniform 
 * buffer we allocate a slot for it from UsedUniformBufferSlots.
 * Returns false if there's any internal error.
 */
extern SHADERCOMPILERCOMMON_API bool BuildResourceTableMapping(
		const TMap<FString,FResourceTableEntry>& ResourceTableMap,
		const TMap<FString,uint32>& ResourceTableLayoutHashes,
		TBitArray<>& UsedUniformBufferSlots,
		FShaderParameterMap& ParameterMap,
		FShaderCompilerResourceTable& OutSRT
	);

/** Culls global uniform buffer entries from the parameter map. */
extern SHADERCOMPILERCOMMON_API void CullGlobalUniformBuffers(const TMap<FString, FString>& ResourceTableLayoutSlots, FShaderParameterMap& ParameterMap);

/**
 * Builds a token stream out of the resource map. The resource map is one
 * of the arrays generated by BuildResourceTableMapping. The token stream
 * is used at runtime to gather resources from tables and bind them to the
 * appropriate slots.
 */
extern SHADERCOMPILERCOMMON_API void BuildResourceTableTokenStream(
	const TArray<uint32>& InResourceMap,
	int32 MaxBoundResourceTable,
	TArray<uint32>& OutTokenStream,
	bool bGenerateEmptyTokenStreamIfNoResources = false
	);

// Finds the number of used uniform buffers in a resource map
extern SHADERCOMPILERCOMMON_API int16 GetNumUniformBuffersUsed(const FShaderCompilerResourceTable& InSRT);


/** Validates and moves all the shader loose data parameter defined in the root scope of the shader into the root uniform buffer. */
class SHADERCOMPILERCOMMON_API FShaderParameterParser
{
public:
	bool ParseAndMoveShaderParametersToRootConstantBuffer(
		const FShaderCompilerInput& CompilerInput,
		FShaderCompilerOutput& CompilerOutput,
		FString& PreprocessedShaderSource,
		const TCHAR* ConstantBufferType);

	void ValidateShaderParameterTypes(
		const FShaderCompilerInput& CompilerInput,
		FShaderCompilerOutput& CompilerOutput) const;

private:
	struct FParsedShaderParameter
	{
		FString Type;
		int32 PragamLineoffset;
		int32 LineOffset;

		bool IsFound() const
		{
			return !Type.IsEmpty();
		}
	};

	void ExtractFileAndLine(int32 PragamLineoffset, int32 LineOffset, FString& OutFile, FString& OutLine) const;

	FString OriginalParsedShader;

	TMap<FString, FParsedShaderParameter> ParsedParameters;
};

// The cross compiler doesn't yet support struct initializers needed to construct static structs for uniform buffers
// Replace all uniform buffer struct member references (View.WorldToClip) with a flattened name that removes the struct dependency (View_WorldToClip)
extern SHADERCOMPILERCOMMON_API void RemoveUniformBuffersFromSource(const FShaderCompilerEnvironment& Environment, FString& PreprocessedShaderSource);
extern SHADERCOMPILERCOMMON_API bool RemoveUnusedOutputs(FString& InOutSourceCode, const TArray<FString>& InUsedOutputs, const TArray<FString>& InExceptions, FString& InOutEntryPoint, TArray<FString>& OutErrors);

extern SHADERCOMPILERCOMMON_API bool RemoveUnusedInputs(FString& InOutSourceCode, const TArray<FString>& InUsedInputs, FString& InOutEntryPoint, TArray<FString>& OutErrors);

extern SHADERCOMPILERCOMMON_API bool ConvertFromFP32ToFP16(FString& InOutSourceCode, TArray<FString>& OutErrors);

/**
* Fall back to using the View uniform buffer directly for platforms that don't support instanced stereo.
* @param ShaderSource - Preprocessed shader source
*/
extern SHADERCOMPILERCOMMON_API void StripInstancedStereo(FString& ShaderSource);

extern SHADERCOMPILERCOMMON_API FString CreateShaderCompilerWorkerDirectCommandLine(const FShaderCompilerInput& Input, uint32 CCFlags = 0);

enum class EShaderConductorTarget
{
	Dxil,
	Spirv,
};
 extern SHADERCOMPILERCOMMON_API void WriteShaderConductorCommandLine(const FShaderCompilerInput& Input, const FString& SourceFilename, EShaderConductorTarget Target);

 // Gets the string that DumpDebugUSF writes out
extern SHADERCOMPILERCOMMON_API FString GetDumpDebugUSFContents(const FShaderCompilerInput& Input, const FString& Source, uint32 HlslCCFlags);

// Utility functions shared amongst all backends to write out a dumped USF
extern SHADERCOMPILERCOMMON_API void DumpDebugUSF(const FShaderCompilerInput& Input, const ANSICHAR* Source, uint32 HlslCCFlags = 0, const TCHAR* OverrideBaseFilename = nullptr);
extern SHADERCOMPILERCOMMON_API void DumpDebugUSF(const FShaderCompilerInput& Input, const FString& Source, uint32 HlslCCFlags = 0, const TCHAR* OverrideBaseFilename = nullptr);

extern SHADERCOMPILERCOMMON_API void DumpDebugShaderText(const FShaderCompilerInput& Input, const FString& InSource, const FString& FileExtension);
extern SHADERCOMPILERCOMMON_API void DumpDebugShaderText(const FShaderCompilerInput& Input, ANSICHAR* InSource, int32 InSourceLength, const FString& FileExtension);
extern SHADERCOMPILERCOMMON_API void DumpDebugShaderBinary(const FShaderCompilerInput& Input, void* InData, int32 InDataByteSize, const FString& FileExtension);

UE_DEPRECATED(4.26, "SourceLength is no longer needed.")
inline void DumpDebugUSF(const FShaderCompilerInput& Input, const ANSICHAR* Source, int32 SourceLength, uint32 HlslCCFlags = 0, const TCHAR* OverrideBaseFilename = nullptr)
{
	DumpDebugUSF(Input, Source, HlslCCFlags, OverrideBaseFilename);
}

// calls 'Mali Offline Compiler' to compile the glsl source code and extract the generated instruction count
extern SHADERCOMPILERCOMMON_API void CompileOfflineMali(const FShaderCompilerInput &Input, FShaderCompilerOutput& ShaderOutput, const ANSICHAR* ShaderSource, const int32 SourceSize, bool bVulkanSpirV, const ANSICHAR* VulkanSpirVEntryPoint = nullptr);

// Cross compiler support/common functionality
namespace CrossCompiler
{
	extern SHADERCOMPILERCOMMON_API FString CreateResourceTableFromEnvironment(const FShaderCompilerEnvironment& Environment);
	extern SHADERCOMPILERCOMMON_API void CreateEnvironmentFromResourceTable(const FString& String, FShaderCompilerEnvironment& OutEnvironment);

	extern SHADERCOMPILERCOMMON_API void ParseHlslccError(TArray<FShaderCompilerError>& OutErrors, const FString& InLine, bool bUseAbsolutePaths = false);

	struct SHADERCOMPILERCOMMON_API FHlslccHeader
	{
		FHlslccHeader();
		virtual ~FHlslccHeader() { }

		bool Read(const ANSICHAR*& ShaderSource, int32 SourceLen);

		// After the standard header, different backends can output their own info
		virtual bool ParseCustomHeaderEntries(const ANSICHAR*& ShaderSource)
		{
			return true;
		}

		struct FInOut
		{
			FString Type;
			int32 Index;
			int32 ArrayCount;
			FString Name;
		};

		struct FAttribute
		{
			int32 Index;
			FString Name;
		};

		struct FPackedGlobal
		{
			ANSICHAR PackedType;
			FString Name;
			int32 Offset;
			int32 Count;
		};

		//struct FUniform
		//{
		//};

		struct FPackedUB
		{
			FAttribute Attribute;
			struct FMember
			{
				FString Name;
				int32 Offset;
				int32 Count;
			};
			TArray<FMember> Members;
		};

		struct FPackedUBCopy
		{
			int32 SourceUB;
			int32 SourceOffset;
			int32 DestUB;
			ANSICHAR DestPackedType;
			int32 DestOffset;
			int32 Count;
		};

		struct FSampler
		{
			FString Name;
			int32 Offset;
			int32 Count;
			TArray<FString> SamplerStates;
		};

		struct FUAV
		{
			FString Name;
			int32 Offset;
			int32 Count;
		};

		FString Name;
		TArray<FInOut> Inputs;
		TArray<FInOut> Outputs;
		TArray<FAttribute> UniformBlocks;
		//TArray<FUniform> Uniforms;
		TArray<FPackedGlobal> PackedGlobals;
		TArray<FPackedUB> PackedUBs;
		TArray<FPackedUBCopy> PackedUBCopies;
		TArray<FPackedUBCopy> PackedUBGlobalCopies;
		TArray<FSampler> Samplers;
		TArray<FUAV> UAVs;
		TArray<FAttribute> SamplerStates;
		uint32 NumThreads[3];

		static bool ReadInOut(const ANSICHAR*& ShaderSource, TArray<FInOut>& OutAttributes);
		static bool ReadCopies(const ANSICHAR*& ShaderSource, bool bGlobals, TArray<FPackedUBCopy>& OutCopies);
	};

	extern SHADERCOMPILERCOMMON_API const TCHAR* GetFrequencyName(EShaderFrequency Frequency);

	inline bool ParseIdentifier(const ANSICHAR*& Str, FString& OutStr)
	{
		OutStr = TEXT("");
		FString Result;
		while ((*Str >= 'A' && *Str <= 'Z')
			|| (*Str >= 'a' && *Str <= 'z')
			|| (*Str >= '0' && *Str <= '9')
			|| *Str == '_')
		{
			OutStr += (TCHAR)*Str;
			++Str;
		}

		return OutStr.Len() > 0;
	}

	inline bool ParseIdentifier(const TCHAR*& Str, FString& OutStr)
	{
		OutStr = TEXT("");
		FString Result;
		while ((*Str >= 'A' && *Str <= 'Z')
			|| (*Str >= 'a' && *Str <= 'z')
			|| (*Str >= '0' && *Str <= '9')
			|| *Str == '_')
		{
			OutStr += (TCHAR)*Str;
			++Str;
		}

		return OutStr.Len() > 0;
	}

	inline bool ParseString(const ANSICHAR*& Str, FString& OutStr)
	{
		OutStr = TEXT("");
		FString Result;
		while (*Str != ' ' && *Str != '\n')
		{
			OutStr += (TCHAR)*Str;
			++Str;
		}

		return OutStr.Len() > 0;
	}

	inline bool ParseString(const TCHAR*& Str, FString& OutStr)
	{
		OutStr = TEXT("");
		FString Result;
		while (*Str != ' ' && *Str != '\n')
		{
			OutStr += (TCHAR)*Str;
			++Str;
		}

		return OutStr.Len() > 0;
	}

	FORCEINLINE bool Match(const ANSICHAR*& Str, ANSICHAR Char)
	{
		if (*Str == Char)
		{
			++Str;
			return true;
		}

		return false;
	}

	FORCEINLINE bool Match(const TCHAR*& Str, ANSICHAR Char)
	{
		if (*Str == Char)
		{
			++Str;
			return true;
		}

		return false;
	}

	FORCEINLINE bool Match(const ANSICHAR*& Str, const ANSICHAR* Sub)
	{
		int32 SubLen = FCStringAnsi::Strlen(Sub);
		if (FCStringAnsi::Strncmp(Str, Sub, SubLen) == 0)
		{
			Str += SubLen;
			return true;
		}

		return false;
	}

	FORCEINLINE bool Match(const TCHAR*& Str, const TCHAR* Sub)
	{
		int32 SubLen = FCString::Strlen(Sub);
		if (FCString::Strncmp(Str, Sub, SubLen) == 0)
		{
			Str += SubLen;
			return true;
		}

		return false;
	}

	template <typename T>
	inline bool ParseIntegerNumber(const ANSICHAR*& Str, T& OutNum)
	{
		auto* OriginalStr = Str;
		OutNum = 0;
		while (*Str >= '0' && *Str <= '9')
		{
			OutNum = OutNum * 10 + *Str++ - '0';
		}

		return Str != OriginalStr;
	}

	template <typename T>
	inline bool ParseIntegerNumber(const TCHAR*& Str, T& OutNum)
	{
		auto* OriginalStr = Str;
		OutNum = 0;
		while (*Str >= '0' && *Str <= '9')
		{
			OutNum = OutNum * 10 + *Str++ - '0';
		}

		return Str != OriginalStr;
	}

	inline bool ParseSignedNumber(const ANSICHAR*& Str, int32& OutNum)
	{
		int32 Sign = Match(Str, '-') ? -1 : 1;
		uint32 Num = 0;
		if (ParseIntegerNumber(Str, Num))
		{
			OutNum = Sign * (int32)Num;
			return true;
		}

		return false;
	}

	inline bool ParseSignedNumber(const TCHAR*& Str, int32& OutNum)
	{
		int32 Sign = Match(Str, '-') ? -1 : 1;
		uint32 Num = 0;
		if (ParseIntegerNumber(Str, Num))
		{
			OutNum = Sign * (int32)Num;
			return true;
		}

		return false;
	}

	/** Wrapper structure to pass options descriptor to ShaderConductor. This is mapped to <struct ShaderConductor::Compiler::Options>. */
	struct SHADERCOMPILERCOMMON_API FShaderConductorOptions
	{
		/** Removes unused global variables and resources. This can only be used in the HLSL rewrite pass, i.e. 'RewriteHlslSource'. */
		bool bRemoveUnusedGlobals = false;

		/** Experimental: Decide how a matrix get packed. */
		bool bPackMatricesInRowMajor = false;

		/** Enable 16-bit types, such as half, uint16_t. Requires shader model 6.2+. */
		bool bEnable16bitTypes = false;

		/** Embed debug info into the binary. */
		bool bEnableDebugInfo = false;

		/** Force to turn off optimizations. Ignore optimizationLevel below. */
		bool bDisableOptimizations = false;

		/** Enable a pass that converts floating point MUL+ADD pairs into FMAs to avoid re-association. */
		bool bEnableFMAPass = false;

		/** Cross compile global variables as push constants (for Vulkan backend). */
		bool bGlobalsAsPushConstants = false;

		/** Target shader profile. By default HCT_FeatureLevelSM5. */
		EHlslCompileTarget TargetProfile = HCT_FeatureLevelSM5;
	};

	/** Target high level languages for ShaderConductor output. */
	enum class EShaderConductorLanguage
	{
		Glsl,
		Essl,
		Metal_macOS,
		Metal_iOS,
	};

	/** Shader conductor output target descriptor. */
	struct SHADERCOMPILERCOMMON_API FShaderConductorTarget
	{
		/** Target shader semantics, e.g. "macOS" or "iOS" for Metal GPU semantics. */
		EShaderConductorLanguage Language = EShaderConductorLanguage::Glsl;

		/**
		Target shader version.
		Valid values for Metal family: 20100, 20000, 10200, 10100, 10000.
		Valid values for GLSL family: 310, 320, 330, 430.
		*/
		int32 Version = 0;

		/** Cross compilation flags. This is used for high-level cross compilation (such as Metal output) that is send over to SPIRV-Cross, e.g. { "invariant_float_math", "1" }. */
		FShaderCompilerDefinitions CompileFlags;
	};

	/** Wrapper class to handle interface between UE and ShaderConductor. Use to compile HLSL shaders to SPIR-V or high-level languages such as Metal. */
	class SHADERCOMPILERCOMMON_API FShaderConductorContext
	{
	public:
		/** Initializes the context with internal buffers used for the conversion of input and option descriptors between UE and ShaderConductor. */
		FShaderConductorContext();

		/** Release the internal buffers. */
		~FShaderConductorContext();

		/** Move constructor to take ownership of internal buffers from 'Rhs'. */
		FShaderConductorContext(FShaderConductorContext&& Rhs);

		/** Move operator to take ownership of internal buffers from 'Rhs'. */
		FShaderConductorContext& operator = (FShaderConductorContext&& Rhs);

		FShaderConductorContext(const FShaderConductorContext&) = delete;
		FShaderConductorContext& operator = (const FShaderConductorContext&) = delete;

		/** Loads the shader source and converts the input descriptor to a format suitable for ShaderConductor. If 'Definitions' is null, the previously loaded definitions are not modified. */
		bool LoadSource(const FString& ShaderSource, const FString& Filename, const FString& EntryPoint, EHlslShaderFrequency ShaderStage, const FShaderCompilerDefinitions* Definitions = nullptr);
		bool LoadSource(const ANSICHAR* ShaderSource, const ANSICHAR* Filename, const ANSICHAR* EntryPoint, EHlslShaderFrequency ShaderStage, const FShaderCompilerDefinitions* Definitions = nullptr);

		/** Rewrites the specified HLSL shader source code. This allows to reduce the HLSL code by removing unused global resources for instance.
		This will update the internally loaded source (see 'LoadSource'), so the output parameter 'OutSource' is optional. */
		bool RewriteHlsl(const FShaderConductorOptions& Options, FString* OutSource = nullptr);

		/** Compiles the specified HLSL shader source code to SPIR-V. */
		bool CompileHlslToSpirv(const FShaderConductorOptions& Options, TArray<uint32>& OutSpirv);

		/** Compiles the specified SPIR-V shader binary code to high level source code (Metal or GLSL). */
		bool CompileSpirvToSource(const FShaderConductorOptions& Options, const FShaderConductorTarget& Target, const void* InSpirv, uint32 InSpirvByteSize, FString& OutSource);

		/** Compiles the specified SPIR-V shader binary code to high level source code (Metal or GLSL) stored as null terminated ANSI string. */
		bool CompileSpirvToSourceAnsi(const FShaderConductorOptions& Options, const FShaderConductorTarget& Target, const void* InSpirv, uint32 InSpirvByteSize, TArray<ANSICHAR>& OutSource);

		/** Compiles the specified SPIR-V shader binary code to high level source code (Metal or GLSL) stored as byte buffer (without null terminator as it comes from ShaderConductor). */
		bool CompileSpirvToSourceBuffer(const FShaderConductorOptions& Options, const FShaderConductorTarget& Target, const void* InSpirv, uint32 InSpirvByteSize, const TFunction<void(const void* Data, uint32 Size)>& OutputCallback);

		/** Flushes the list of current compile errors and moves the ownership to the caller. */
		void FlushErrors(TArray<FShaderCompilerError>& OutErrors);

		/** Returns a pointer to a null terminated ANSI string of the internal loaded sources, or null if no source has been loaded yet. This is automatically updated when RewriteHlsl() is called. */
		const ANSICHAR* GetSourceString() const;

		/** Returns a length of the internal loaded sources (excluding the null terminator). This is automatically updated when RewriteHlsl() is called. */
		int32 GetSourceLength() const;

		/** Returns the list of current compile errors. */
		inline const TArray<FShaderCompilerError>& GetErrors() const
		{
			return Errors;
		}

	public:
		/** Convert array of error string lines into array of <FShaderCompilerError>. */
		static void ConvertCompileErrors(TArray<FString>&& ErrorStringLines, TArray<FShaderCompilerError>& OutErrors);

		/** Returns whether the specified variable name denotes an intermediate output variable.
		This is only true for a special identifiers generated by DXC to communicate patch constant data in the Hull Shader. */
		static bool IsIntermediateSpirvOutputVariable(const ANSICHAR* SpirvVariableName);

	public:
		struct FShaderConductorIntermediates; // Pimpl idiom

	private:
		TArray<FShaderCompilerError> Errors;
		FShaderConductorIntermediates* Intermediates; // Pimpl idiom
	};

}

// Error code for SCW to help track down crashes
extern SHADERCOMPILERCOMMON_API ESCWErrorCode GSCWErrorCode;
