// Copyright Epic Games, Inc. All Rights Reserved.

#include "UObject/UnrealNames.h"
#include "UObject/NameBatchSerialization.h"
#include "Misc/AssertionMacros.h"
#include "Misc/MessageDialog.h"
#include "Math/NumericLimits.h"
#include "Math/UnrealMathUtility.h"
#include "HAL/UnrealMemory.h"
#include "Templates/UnrealTemplate.h"
#include "Misc/CString.h"
#include "Misc/Crc.h"
#include "Misc/StringBuilder.h"
#include "Containers/UnrealString.h"
#include "UObject/NameTypes.h"
#include "Logging/LogMacros.h"
#include "Misc/ByteSwap.h"
#include "UObject/ObjectVersion.h"
#include "HAL/ThreadSafeCounter.h"
#include "Misc/ScopeRWLock.h"
#include "Containers/Set.h"
#include "Internationalization/Text.h"
#include "Internationalization/Internationalization.h"
#include "Misc/OutputDeviceRedirector.h"
#include "HAL/IConsoleManager.h"
#include "HAL/LowLevelMemTracker.h"
#include "Serialization/MemoryImage.h"
#include "Hash/CityHash.h"
#include "Templates/AlignmentTemplates.h"

PRAGMA_DISABLE_UNSAFE_TYPECAST_WARNINGS

// Page protection to catch FNameEntry stomps
#ifndef FNAME_WRITE_PROTECT_PAGES
#define FNAME_WRITE_PROTECT_PAGES 0
#endif
#if FNAME_WRITE_PROTECT_PAGES
#	define FNAME_BLOCK_ALIGNMENT FPlatformMemory::GetConstants().PageSize
#else
#	define FNAME_BLOCK_ALIGNMENT alignof(FNameEntry)
#endif

DEFINE_LOG_CATEGORY_STATIC(LogUnrealNames, Log, All);

// 得到EName对应的字符串
const TCHAR* LexToString(EName Ename)
{
	switch (Ename)
	{
#define REGISTER_NAME(num,namestr) case num: return TEXT(#namestr);
#include "UObject/UnrealNames.inl"
#undef REGISTER_NAME
		default:
			return TEXT("*INVALID*");
	}
}

// 得到名字入口中，名字数据的偏移
int32 FNameEntry::GetDataOffset()
{
	return STRUCT_OFFSET(FNameEntry, AnsiName);
}

/*-----------------------------------------------------------------------------
	FName helpers. 
-----------------------------------------------------------------------------*/
// FNameEntryHeader类型的A和B是否相等
static bool operator==(FNameEntryHeader A, FNameEntryHeader B)
{
	static_assert(sizeof(FNameEntryHeader) == 2, "");
	return (uint16&)A == (uint16&)B;
}

// 不支持的转换
template<typename FromCharType, typename ToCharType>
ToCharType* ConvertInPlace(FromCharType* Str, uint32 Len)
{
	static_assert(TIsSame<FromCharType, ToCharType>::Value, "Unsupported conversion");
	return Str;
}


// Ansi字符串转换成Unicode字符串(这种转换只能用于字符编码中Ansi和Unicode的编码一样的字符)
template<>
WIDECHAR* ConvertInPlace<ANSICHAR, WIDECHAR>(ANSICHAR* Str, uint32 Len)
{
	for (uint32 Index = Len; Index--; )
	{
		reinterpret_cast<WIDECHAR*>(Str)[Index] = Str[Index];
	}

	return reinterpret_cast<WIDECHAR*>(Str);
}

// Unicode字符串转换成Ansi字符串（这种转换只能用于字符编码中Ansi和Unicode的编码一样的字符）
template<>
ANSICHAR* ConvertInPlace<WIDECHAR, ANSICHAR>(WIDECHAR* Str, uint32 Len)
{
	for (uint32 Index = 0; Index < Len; ++Index)
	{
		reinterpret_cast<ANSICHAR*>(Str)[Index] = Str[Index];
	}

	return reinterpret_cast<ANSICHAR*>(Str);
}

// 名字缓冲区
union FNameBuffer
{
	ANSICHAR AnsiName[NAME_SIZE];
	WIDECHAR WideName[NAME_SIZE];
};

// 名字和字符串视图
struct FNameStringView
{
	FNameStringView() : Data(nullptr), Len(0), bIsWide(false) {}
	FNameStringView(const ANSICHAR* Str, uint32 Len_) : Ansi(Str), Len(Len_), bIsWide(false) {}
	FNameStringView(const WIDECHAR* Str, uint32 Len_) : Wide(Str), Len(Len_), bIsWide(true) {}

	// 字符数据指针
	union
	{
		const void* Data;
		const ANSICHAR* Ansi;
		const WIDECHAR* Wide;
	};
	// 长度
	uint32 Len;
	// 是否宽字符
	bool bIsWide;

	bool IsAnsi() const { return !bIsWide; }

	// 包括终止符在内的字节的长度
	int32 BytesWithTerminator() const
	{
		return (Len + 1) * (bIsWide ? sizeof(WIDECHAR) : sizeof(ANSICHAR));
	}

	// 排除终止符在内的字节的长度
	int32 BytesWithoutTerminator() const
	{
		return Len * (bIsWide ? sizeof(WIDECHAR) : sizeof(ANSICHAR));
	}
};

// 两个FNameStringView是否相等
template<ENameCase Sensitivity>
FORCEINLINE bool EqualsSameDimensions(FNameStringView A, FNameStringView B)
{
	checkSlow(A.Len == B.Len && A.IsAnsi() == B.IsAnsi());

	int32 Len = A.Len;

	if (Sensitivity == ENameCase::CaseSensitive)
	{
		return B.IsAnsi() ? !FPlatformString::Strncmp(A.Ansi, B.Ansi, Len) : !FPlatformString::Strncmp(A.Wide, B.Wide, Len);
	}
	else
	{
		return B.IsAnsi() ? !FPlatformString::Strnicmp(A.Ansi, B.Ansi, Len) : !FPlatformString::Strnicmp(A.Wide, B.Wide, Len);
	}

}

// 两个FNameStringView是否相等
template<ENameCase Sensitivity>
FORCEINLINE bool Equals(FNameStringView A, FNameStringView B)
{
	return (A.Len == B.Len & A.IsAnsi() == B.IsAnsi()) && EqualsSameDimensions<Sensitivity>(A, B);
}

// Minimize stack lifetime of large decode buffers
#ifdef WITH_CUSTOM_NAME_ENCODING
#define OUTLINE_DECODE_BUFFER FORCENOINLINE
#else
#define OUTLINE_DECODE_BUFFER
#endif

// 两个FNameStringView是否相等
template<ENameCase Sensitivity>
OUTLINE_DECODE_BUFFER bool EqualsSameDimensions(const FNameEntry& Entry, FNameStringView Name)
{
	FNameBuffer DecodeBuffer;
	return EqualsSameDimensions<Sensitivity>(Entry.MakeView(DecodeBuffer), Name);
}

/** Remember to update natvis if you change these */
// 块的bit是13 块的Offset的bit是16
enum { FNameMaxBlockBits = 13 }; // Limit block array a bit, still allowing 8k * block size = 1GB - 2G of FName entry data
enum { FNameBlockOffsetBits = 16 };
// 最大的名字块数目 8192
enum { FNameMaxBlocks = 1 << FNameMaxBlockBits };
// 最大的块偏移 64K
enum { FNameBlockOffsets = 1 << FNameBlockOffsetBits };

/** An unpacked FNameEntryId */
// 一个没有打包（展开）的名字入口
struct FNameEntryHandle
{
	uint32 Block = 0;
	uint32 Offset = 0;

	FNameEntryHandle(uint32 InBlock, uint32 InOffset)
		: Block(InBlock)
		, Offset(InOffset)
	{}
	// 从一个入口ID初始化入口handle
	FNameEntryHandle(FNameEntryId Id)
		: Block(Id.ToUnstableInt() >> FNameBlockOffsetBits)
		, Offset(Id.ToUnstableInt() & (FNameBlockOffsets - 1))
	{}
	// 转换成入口ID
	operator FNameEntryId() const
	{
		// Block左移16位然后与Offset
		return FNameEntryId::FromUnstableInt(Block << FNameBlockOffsetBits | Offset);
	}

	explicit operator bool() const { return Block | Offset; }
};

// 得到类型Hash
static uint32 GetTypeHash(FNameEntryHandle Handle)
{
	return (Handle.Block << (32 - FNameMaxBlockBits)) + Handle.Block // Let block index impact most hash bits
		+ (Handle.Offset << FNameBlockOffsetBits) + Handle.Offset // Let offset impact most hash bits
		+ (Handle.Offset >> 4); // Reduce impact of non-uniformly distributed entry name lengths 
}

// 得到类型Hash
uint32 GetTypeHash(FNameEntryId Id)
{
	return GetTypeHash(FNameEntryHandle(Id));
}

FArchive& operator<<(FArchive& Ar, FNameEntryId& Id)
{
	if (Ar.IsLoading())
	{
		uint32 UnstableInt = 0;
		Ar << UnstableInt;
		Id = FNameEntryId::FromUnstableInt(UnstableInt);
	}
	else
	{
		uint32 UnstableInt = Id.ToUnstableInt();
		Ar << UnstableInt;
	}

	return Ar;
}

// 从一个uint32创建一个FNameEntryId
FNameEntryId FNameEntryId::FromUnstableInt(uint32 Value)
{
	FNameEntryId Id;
	Id.Value = Value;
	return Id;
}

struct FNameSlot
{
	// Use the remaining few bits to store a hash that can determine inequality
	// during probing without touching entry data
	// 名字的块和块内偏移的位的数目 29位
	static constexpr uint32 EntryIdBits = FNameMaxBlockBits + FNameBlockOffsetBits;
	// 块ID的掩码 低29位为1
	static constexpr uint32 EntryIdMask = (1 << EntryIdBits) - 1;
	// probe的hash位 29位
	static constexpr uint32 ProbeHashShift = EntryIdBits;
	// probe的hash掩码 高3位为1
	static constexpr uint32 ProbeHashMask = ~EntryIdMask;
	
	FNameSlot() {}
	FNameSlot(FNameEntryId Value, uint32 ProbeHash)
		: IdAndHash(Value.ToUnstableInt() | ProbeHash)
	{
		check(!(Value.ToUnstableInt() & ProbeHashMask) && !(ProbeHash & EntryIdMask) && Used());
	}
	// 得到ID
	FNameEntryId GetId() const { return FNameEntryId::FromUnstableInt(IdAndHash & EntryIdMask); }
	// 得到hash，通过Hash掩码
	uint32 GetProbeHash() const { return IdAndHash & ProbeHashMask; }
	
	bool operator==(FNameSlot Rhs) const { return IdAndHash == Rhs.IdAndHash; }

	bool Used() const { return !!IdAndHash;  }
private:
	// ID和Hash共用一个uint32
	uint32 IdAndHash = 0;
};

/**
 * Thread-safe paged FNameEntry allocator
 */
// 线程安全的FNameEntry分配器，一个块有64K个FName
class FNameEntryAllocator
{
public:
	enum { Stride = alignof(FNameEntry) };
	// 块大小 64K个FName
	enum { BlockSizeBytes = Stride * FNameBlockOffsets };

	/** Initializes all member variables. */
	// 初始化所有的成员变量
	FNameEntryAllocator()
	{
		LLM_SCOPE(ELLMTag::FName);
		Blocks[0] = (uint8*)FMemory::MallocPersistentAuxiliary(BlockSizeBytes, FNAME_BLOCK_ALIGNMENT);
	}

	~FNameEntryAllocator()
	{
		for (uint32 Index = 0; Index <= CurrentBlock; ++Index)
		{
			FMemory::Free(Blocks[Index]);
		}
	}

	// 预留指定数目的块
	void ReserveBlocks(uint32 Num)
	{
		FWriteScopeLock _(Lock);
		// 分配指定的块
		for (uint32 Idx = Num - 1; Idx > CurrentBlock && Blocks[Idx] == nullptr; --Idx)
		{
			Blocks[Idx] = AllocBlock();
		}
	}


	/**
	 * Allocates the requested amount of bytes and returns an id that can be used to access them
	 *
	 * @param   Size  Size in bytes to allocate, 
	 * @return  Allocation of passed in size cast to a FNameEntry pointer.
	 */
	// 分配请求的字节数并返回一个可用于访问它们的ID
	template <class ScopeLock>
	FNameEntryHandle Allocate(uint32 Bytes)
	{
		Bytes = Align(Bytes, alignof(FNameEntry));
		check(Bytes <= BlockSizeBytes);

		ScopeLock _(Lock);

		// Allocate a new pool if current one is exhausted. We don't worry about a little bit
		// of waste at the end given the relative size of pool to average and max allocation.
		// 如果当前的意见耗尽，分配一个新的pool，相比较于池大小和最大的分配大小的相对尺寸，我们无需担心块最后的一点小的浪费，
		if (BlockSizeBytes - CurrentByteCursor < Bytes)
		{
			AllocateNewBlock();
		}

		// Use current cursor position for this allocation and increment cursor for next allocation
		// 当前的块内偏移
		uint32 ByteOffset = CurrentByteCursor;
		// 增加游标到下一个可分配处
		CurrentByteCursor += Bytes;
		
		check(ByteOffset % Stride == 0 && ByteOffset / Stride < FNameBlockOffsets);
		// ByteOffset / Stride表示块内的索引
		// CurrentBlock块内索引
		return FNameEntryHandle(CurrentBlock, ByteOffset / Stride);
	}

	template<class ScopeLock>
	FNameEntryHandle Create(FNameStringView Name, TOptional<FNameEntryId> ComparisonId, FNameEntryHeader Header)
	{
		// 计算出FName需要的字节数
		FNameEntryHandle Handle = Allocate<ScopeLock>(FNameEntry::GetDataOffset() + Name.BytesWithoutTerminator());
		// 得到对应的入口
		FNameEntry& Entry = Resolve(Handle);

#if WITH_CASE_PRESERVING_NAME
		// 如果ComparisonId设置了，就用设置好的ComparisonId，如果没有设置，就用Handle
		Entry.ComparisonId = ComparisonId.IsSet() ? ComparisonId.GetValue() : FNameEntryId(Handle);
#endif
		// 设置头
		Entry.Header = Header;
		// 保存名字
		if (Name.bIsWide)
		{
			Entry.StoreName(Name.Wide, Name.Len);
		}
		else
		{
			Entry.StoreName(Name.Ansi, Name.Len);
		}

		return Handle;
	}

	// 取得FNameEntryHandle对应的FNameEntry
	FNameEntry& Resolve(FNameEntryHandle Handle) const
	{
		// Lock not needed
		return *reinterpret_cast<FNameEntry*>(Blocks[Handle.Block] + Stride * Handle.Offset);
	}

	void BatchLock() const
	{
		Lock.WriteLock();
	}

	void BatchUnlock() const
	{
		Lock.WriteUnlock();
	}

	/** Returns the number of blocks that have been allocated so far for names. */
	// 返回当前为names分配的blocks的数目
	uint32 NumBlocks() const
	{
		return CurrentBlock + 1;
	}
	
	uint8** GetBlocksForDebugVisualizer() { return Blocks; }

	// 转储所有的Block
	void DebugDump(TArray<const FNameEntry*>& Out) const
	{
		FRWScopeLock _(Lock, FRWScopeLockType::SLT_ReadOnly);

		for (uint32 BlockIdx = 0; BlockIdx < CurrentBlock; ++BlockIdx)
		{
			DebugDumpBlock(Blocks[BlockIdx], BlockSizeBytes, Out);
		}

		DebugDumpBlock(Blocks[CurrentBlock], CurrentByteCursor, Out);
	}

private:
	// 转储单个Block
	static void DebugDumpBlock(const uint8* It, uint32 BlockSize, TArray<const FNameEntry*>& Out)
	{
		const uint8* End = It + BlockSize - FNameEntry::GetDataOffset();
		while (It < End)
		{
			const FNameEntry* Entry = (const FNameEntry*)It;
			if (uint32 Len = Entry->Header.Len)
			{
				Out.Add(Entry);
				It += FNameEntry::GetSize(Len, !Entry->IsWide());
			}
			else // Null-terminator entry found
			{
				break;
			}
		}
	}
	// 分配一个Block
	static uint8* AllocBlock()
	{
		return (uint8*)FMemory::MallocPersistentAuxiliary(BlockSizeBytes, FNAME_BLOCK_ALIGNMENT);
	}
	
	// 分配一个新的Block
	void AllocateNewBlock()
	{
		LLM_SCOPE(ELLMTag::FName);
		// Null-terminate final entry to allow DebugDump() entry iteration
		// 以Null结尾的最后一个entry允许DebugDump的entry迭代
		if (CurrentByteCursor + FNameEntry::GetDataOffset() <= BlockSizeBytes)
		{
			FNameEntry* Terminator = (FNameEntry*)(Blocks[CurrentBlock] + CurrentByteCursor);
			Terminator->Header.Len = 0;
		}

#if FNAME_WRITE_PROTECT_PAGES
		FPlatformMemory::PageProtect(Blocks[CurrentBlock], BlockSizeBytes, /* read */ true, /* write */ false);
#endif
		// 当前的Block索引增加
		++CurrentBlock;
		// 当前的游标增加
		CurrentByteCursor = 0;

		check(CurrentBlock < FNameMaxBlocks);

		// Allocate block unless it's already reserved
		// 如果没有预留，就分配
		if (Blocks[CurrentBlock] == nullptr)
		{
			Blocks[CurrentBlock] = AllocBlock();
		}
	}

	mutable FRWLock Lock;
	uint32 CurrentBlock = 0;
	uint32 CurrentByteCursor = 0;
	uint8* Blocks[FNameMaxBlocks] = {};
};

// Increasing shards reduces contention but uses more memory and adds cache pressure.
// Reducing contention matters when multiple threads create FNames in parallel.
// Contention exists in some tool scenarios, for instance between main thread
// and asset data gatherer thread during editor startup.
// 增加分片可以减少争用，但会占用更多内存并增加缓存压力。 当多个线程并行创建FName时，减少争用很重要。
// 在某些工具方案中存在争用，例如在编辑器启动期间主线程和资产数据收集器线程之间。
#if WITH_CASE_PRESERVING_NAME
enum { FNamePoolShardBits = 10 };
#else
enum { FNamePoolShardBits = 4 };
#endif

enum { FNamePoolShards = 1 << FNamePoolShardBits };
// Slot的初始的数量表示的位数，8位，256
enum { FNamePoolInitialSlotBits = 8 };
// Slot的数目
enum { FNamePoolInitialSlotsPerShard = 1 << FNamePoolInitialSlotBits };

/** Hashes name into 64 bits that determines shard and slot index.
 *	
 *	A small part of the hash is also stored in unused bits of the slot and entry. 
 *	The former optimizes linear probing by accessing less entry data.
 *	The latter optimizes linear probing by avoiding copying and deobfuscating entry data.
 *
 *	The slot index could be stored in the slot, at least in non shipping / test configs.
 *	This costs memory by doubling slot size but would essentially never touch entry data
 *	nor copy and deobfuscate a name needlessy. It also allows growing the hash table
 *	without rehashing the strings, since the unmasked slot index would be known.
 */
// 将Name散列为64位，用于确定分片和插槽索引。
// 哈希的一小部分也存储在插槽和条目的未使用位中,前者通过访问较少的入口数据来优化线性探测，
// 后者通过避免复制和去混淆入口数据来优化线性探测.
// 
// 插槽索引可以存储在插槽中，最少化存储非传输/测试配置中，这会通过使插槽大小增加一倍
// 但基本上不会触及输入数据，也不会不必要地复制和模糊化名称
// 它还允许增长哈希表。无需重新散列字符串，因为将知道未屏蔽的插槽索引。
struct FNameHash
{
	uint32 ShardIndex;
	// 确定从哪个插槽索引开始探测
	uint32 UnmaskedSlotIndex; // Determines at what slot index to start probing
	// 探测插槽时帮助剔除是否相等（解码+ strnicmp）
	uint32 SlotProbeHash; // Helps cull equality checks (decode + strnicmp) when probing slots
	// 在探测检查条目时帮助剔除均等性检查
	FNameEntryHeader EntryProbeHeader; // Helps cull equality checks when probing inspects entries

	static constexpr uint64 AlgorithmId = 0xC1640000;

	template<class CharType>
	static uint64 GenerateHash(const CharType* Str, int32 Len)
	{
		// 直接调用city hash 64的算法
		return CityHash64(reinterpret_cast<const char*>(Str), Len * sizeof(CharType));
	}

	// 先将字符转换为小写，然后hash
	template<class CharType>
	static uint64 GenerateLowerCaseHash(const CharType* Str, uint32 Len);

	template<class CharType>
	FNameHash(const CharType* Str, int32 Len)
		: FNameHash(Str, Len, GenerateHash(Str, Len))
	{}

	template<class CharType>
	FNameHash(const CharType* Str, int32 Len, uint64 Hash)
	{
		// 将64位的hash拆分成高低位
		uint32 Hi = static_cast<uint32>(Hash >> 32);
		uint32 Lo = static_cast<uint32>(Hash);

		// "None" has FNameEntryId with a value of zero
		// Always set a bit in SlotProbeHash for "None" to distinguish unused slot values from None
		// @see FNameSlot::Used()
		// 
		uint32 IsNoneBit = IsAnsiNone(Str, Len) << FNameSlot::ProbeHashShift;

		static constexpr uint32 ShardMask = FNamePoolShards - 1;
		static_assert((ShardMask & FNameSlot::ProbeHashMask) == 0, "Masks overlap");

		// 分片索引
		ShardIndex = Hi & ShardMask;
		// 插槽索引
		UnmaskedSlotIndex = Lo;
		SlotProbeHash = (Hi & FNameSlot::ProbeHashMask) | IsNoneBit;
		EntryProbeHeader.Len = Len;
		EntryProbeHeader.bIsWide = sizeof(CharType) == sizeof(WIDECHAR);

		// When we always use lowercase hashing, we can store parts of the hash in the entry
		// to avoid copying and decoding entries needlessly. WITH_CUSTOM_NAME_ENCODING
		// that makes this important is normally on when WITH_CASE_PRESERVING_NAME is off.
#if !WITH_CASE_PRESERVING_NAME		
		static constexpr uint32 EntryProbeMask = (1u << FNameEntryHeader::ProbeHashBits) - 1; 
		EntryProbeHeader.LowercaseProbeHash = static_cast<uint16>((Hi >> FNamePoolShardBits) & EntryProbeMask);
#endif
	}
	
	uint32 GetProbeStart(uint32 SlotMask) const
	{
		return UnmaskedSlotIndex & SlotMask;
	}

	// 通过索引和掩码，得到起始索引
	static uint32 GetProbeStart(uint32 UnmaskedSlotIndex, uint32 SlotMask)
	{
		return UnmaskedSlotIndex & SlotMask;
	}

	static uint32 IsAnsiNone(const WIDECHAR* Str, int32 Len)
	{
		return 0;
	}

	static uint32 IsAnsiNone(const ANSICHAR* Str, int32 Len)
	{
		if (Len != 4)
		{
			return 0;
		}

#if PLATFORM_LITTLE_ENDIAN
		static constexpr uint32 NoneAsInt = 0x454e4f4e;
#else
		static constexpr uint32 NoneAsInt = 0x4e4f4e45;
#endif
		static constexpr uint32 ToUpperMask = 0xdfdfdfdf;

		uint32 FourChars = FPlatformMemory::ReadUnaligned<uint32>(Str);
		return (FourChars & ToUpperMask) == NoneAsInt;
	}

	bool operator==(const FNameHash& Rhs) const
	{
		return  ShardIndex == Rhs.ShardIndex &&
				UnmaskedSlotIndex == Rhs.UnmaskedSlotIndex &&
				SlotProbeHash == Rhs.SlotProbeHash &&
				EntryProbeHeader == Rhs.EntryProbeHeader;
	}
};

// 先将字符转换为小写，然后hash
template<class CharType>
FORCENOINLINE uint64 FNameHash::GenerateLowerCaseHash(const CharType* Str, uint32 Len)
{
	CharType LowerStr[NAME_SIZE];
	for (uint32 I = 0; I < Len; ++I)
	{
		LowerStr[I] = TChar<CharType>::ToLower(Str[I]);
	}

	return FNameHash::GenerateHash(LowerStr, Len);
}

template<class CharType>
FORCENOINLINE FNameHash HashLowerCase(const CharType* Str, uint32 Len)
{
	CharType LowerStr[NAME_SIZE];
	for (uint32 I = 0; I < Len; ++I)
	{
		LowerStr[I] = TChar<CharType>::ToLower(Str[I]);
	}
	return FNameHash(LowerStr, Len);
}

template<ENameCase Sensitivity>
FNameHash HashName(FNameStringView Name);

// 创建FNameHash
template<>
FNameHash HashName<ENameCase::IgnoreCase>(FNameStringView Name)
{
	return Name.IsAnsi() ? HashLowerCase(Name.Ansi, Name.Len) : HashLowerCase(Name.Wide, Name.Len);
}
template<>
FNameHash HashName<ENameCase::CaseSensitive>(FNameStringView Name)
{
	return Name.IsAnsi() ? FNameHash(Name.Ansi, Name.Len) : FNameHash(Name.Wide, Name.Len);
}

// 名字值
template<ENameCase Sensitivity>
struct FNameValue
{
	explicit FNameValue(FNameStringView InName)
		: Name(InName)
		, Hash(HashName<Sensitivity>(InName))
	{}

	FNameValue(FNameStringView InName, FNameHash InHash)
		: Name(InName)
		, Hash(InHash)
	{}

	// 视图
	FNameStringView Name;
	// Hash值
	FNameHash Hash;
	TOptional<FNameEntryId> ComparisonId;
};

using FNameComparisonValue = FNameValue<ENameCase::IgnoreCase>;
#if WITH_CASE_PRESERVING_NAME
using FNameDisplayValue = FNameValue<ENameCase::CaseSensitive>;
#endif

// For prelocked batch insertions
struct FNullScopeLock
{
	FNullScopeLock(FRWLock&) {}
};

// 名字池基类
class alignas(PLATFORM_CACHE_LINE_SIZE) FNamePoolShardBase : FNoncopyable
{
public:
	void Initialize(FNameEntryAllocator& InEntries)
	{
		LLM_SCOPE(ELLMTag::FName);
		Entries = &InEntries;
		
		// 分配初始的slot数组
		Slots = (FNameSlot*)FMemory::Malloc(FNamePoolInitialSlotsPerShard * sizeof(FNameSlot), alignof(FNameSlot));
		memset(Slots, 0, FNamePoolInitialSlotsPerShard * sizeof(FNameSlot));
		// 容量的掩码
		CapacityMask = FNamePoolInitialSlotsPerShard - 1;
	}

	// This and ~FNamePool() is not called during normal shutdown
	// but only via explicit FName::TearDown() call
	~FNamePoolShardBase()
	{
		FMemory::Free(Slots);
		UsedSlots = 0;
		CapacityMask = 0;
		Slots = nullptr;
		NumCreatedEntries = 0;
		NumCreatedWideEntries = 0;
	}
	// 容量
	uint32 Capacity() const	{ return CapacityMask + 1; }

	uint32 NumCreated() const { return NumCreatedEntries; }
	uint32 NumCreatedWide() const { return NumCreatedWideEntries; }

	// Used for batch insertion together with Insert<FNullScopeLock>()
	void BatchLock() const	 { Lock.WriteLock(); }
	void BatchUnlock() const { Lock.WriteUnlock(); }

protected:
	enum { LoadFactorQuotient = 9, LoadFactorDivisor = 10 }; // I.e. realloc slots when 90% full

	mutable FRWLock Lock;
	uint32 UsedSlots = 0;
	uint32 CapacityMask = 0;
	FNameSlot* Slots = nullptr;
	FNameEntryAllocator* Entries = nullptr;
	// 创建的Entry的数目
	uint32 NumCreatedEntries = 0;
	uint32 NumCreatedWideEntries = 0;


	template<ENameCase Sensitivity>
	FORCEINLINE static bool EntryEqualsValue(const FNameEntry& Entry, const FNameValue<Sensitivity>& Value)
	{
		return Entry.Header == Value.Hash.EntryProbeHeader && EqualsSameDimensions<Sensitivity>(Entry, Value.Name);
	}
};

// 名字池分片
template<ENameCase Sensitivity>
class FNamePoolShard : public FNamePoolShardBase
{
public:
	// 找到FNameValue对应的入口ID
	FNameEntryId Find(const FNameValue<Sensitivity>& Value) const
	{
		FRWScopeLock _(Lock, FRWScopeLockType::SLT_ReadOnly);

		return Probe(Value).GetId();
	}

	template<class ScopeLock = FWriteScopeLock>
	FORCEINLINE FNameEntryId Insert(const FNameValue<Sensitivity>& Value, bool& bCreatedNewEntry)
	{
		ScopeLock _(Lock);
		FNameSlot& Slot = Probe(Value);
		// 找到对应的Slot
		if (Slot.Used())
		{
			return Slot.GetId();
		}

		// 创建一个新的Entry
		FNameEntryId NewEntryId = Entries->Create<ScopeLock>(Value.Name, Value.ComparisonId, Value.Hash.EntryProbeHeader);
		// 将新的slot放入找到的Slot中
		ClaimSlot(Slot, FNameSlot(NewEntryId, Value.Hash.SlotProbeHash));
		// 增加创建的Entry
		++NumCreatedEntries;
		// 增加创建的Wide Entry
		NumCreatedWideEntries += Value.Name.bIsWide;
		bCreatedNewEntry = true;
		// 返回新创建的Entry ID
		return NewEntryId;
	}

	// 插入一个已经存在的Entry
	void InsertExistingEntry(FNameHash Hash, FNameEntryId ExistingId)
	{
		FNameSlot NewLookup(ExistingId, Hash.SlotProbeHash);

		FRWScopeLock _(Lock, FRWScopeLockType::SLT_Write);
		// 如果Hash对应的slot空闲，传入的Entry放入找到的Slot中
		FNameSlot& Slot = Probe(Hash.UnmaskedSlotIndex, [=](FNameSlot Old) { return Old == NewLookup; });
		if (!Slot.Used())
		{
			ClaimSlot(Slot, NewLookup);
		}
	}

	// 预留数量
	void Reserve(uint32 Num)
	{
		// 需要多保留一些
		uint32 WantedCapacity = FMath::RoundUpToPowerOfTwo(Num * LoadFactorDivisor / LoadFactorQuotient);

		FWriteScopeLock _(Lock);
		// 如果想要的容量比现在的容量大，就需要增长
		if (WantedCapacity > Capacity())
		{
			Grow(WantedCapacity);
		}
	}

private:
	void ClaimSlot(FNameSlot& UnusedSlot, FNameSlot NewValue)
	{
		UnusedSlot = NewValue;

		++UsedSlots;
		// 超过90%，就扩展
		if (UsedSlots * LoadFactorDivisor >= LoadFactorQuotient * Capacity())
		{
			Grow();
		}
	}
	// 直接扩展成原来的2倍容量
	void Grow()
	{
		Grow(Capacity() * 2);
	}

	// 增长到新的容量
	void Grow(const uint32 NewCapacity)
	{
		LLM_SCOPE(ELLMTag::FName);
		FNameSlot* const OldSlots = Slots;
		const uint32 OldUsedSlots = UsedSlots;
		const uint32 OldCapacity = Capacity();

		// 分配新的slots,并初始化为0
		Slots = (FNameSlot*)FMemory::Malloc(NewCapacity * sizeof(FNameSlot), alignof(FNameSlot));
		memset(Slots, 0, NewCapacity * sizeof(FNameSlot));
		UsedSlots = 0;
		CapacityMask = NewCapacity - 1;

		// 将原来的slot中的放到新的里面去
		for (uint32 OldIdx = 0; OldIdx < OldCapacity; ++OldIdx)
		{
			const FNameSlot& OldSlot = OldSlots[OldIdx];
			// slot使用了的话，需要放入新的里面去
			if (OldSlot.Used())
			{
				FNameHash Hash = Rehash(OldSlot.GetId());
				FNameSlot& NewSlot = Probe(Hash.UnmaskedSlotIndex, [](FNameSlot Slot) { return false; });
				NewSlot = OldSlot;
				++UsedSlots;
			}
		}

		check(OldUsedSlots == UsedSlots);
		// 释放老的
		FMemory::Free(OldSlots);
	}

	/** Find slot containing value or the first free slot that should be used to store it  */
	// 找到包含值或者第一个空闲的slot用于保存
	FORCEINLINE FNameSlot& Probe(const FNameValue<Sensitivity>& Value) const
	{
		return Probe(Value.Hash.UnmaskedSlotIndex, 
			[&](FNameSlot Slot)	{ return Slot.GetProbeHash() == Value.Hash.SlotProbeHash && 
									EntryEqualsValue<Sensitivity>(Entries->Resolve(Slot.GetId()), Value); });
	}

	/** Find slot that fulfills predicate or the first free slot  */
	// 找到满足条件的slot或者第一个空闲的slot
	template<class PredicateFn>
	FORCEINLINE FNameSlot& Probe(uint32 UnmaskedSlotIndex, PredicateFn Predicate) const
	{
		const uint32 Mask = CapacityMask;
		// 从开始的slot开始遍历
		for (uint32 I = FNameHash::GetProbeStart(UnmaskedSlotIndex, Mask); true; I = (I + 1) & Mask)
		{
			FNameSlot& Slot = Slots[I];
			// 如果slot空闲或者符合谓词条件，就返回它
			if (!Slot.Used() || Predicate(Slot))
			{
				return Slot;
			}
		}
	}

	// 从FNameEntryId得到对应的FNameHash
	OUTLINE_DECODE_BUFFER FNameHash Rehash(FNameEntryId EntryId)
	{
		// 根据ID从分配器里面找到对应的FNameEntry
		const FNameEntry& Entry = Entries->Resolve(EntryId);
		FNameBuffer DecodeBuffer;
		// 从FNameStringView得到HashName
		return HashName<Sensitivity>(Entry.MakeView(DecodeBuffer));
	}
};


// 名字池
class FNamePool
{
public:
	FNamePool();
	// 在分配器里面保留NumBlocks个字节的Block，所有的分配里面总共保留NumEntries个Entries
	void			Reserve(uint32 NumBlocks, uint32 NumEntries);
	// 保存名字
	FNameEntryId	Store(FNameStringView View);
	// 查找名字
	FNameEntryId	Find(FNameStringView View) const;
	FNameEntryId	Find(EName Ename) const;
	const EName*	FindEName(FNameEntryId Id) const;

	/** @pre !!Handle */
	// 根据Handle得到名字NameEntry
	FNameEntry&		Resolve(FNameEntryHandle Handle) const { return Entries.Resolve(Handle); }
	// Handle是否合法
	bool			IsValid(FNameEntryHandle Handle) const;
	// 批量锁定和解锁
	void			BatchLock();
	FNameEntryId	BatchStore(const FNameComparisonValue& ComparisonValue);
	void			BatchUnlock();

	/// Stats and debug related functions ///
	// Entries的数目
	uint32			NumEntries() const;
	// Ansi Entries的数目
	uint32			NumAnsiEntries() const;
	// Unicode Entries的数目
	uint32			NumWideEntries() const;
	// Block块的数目
	uint32			NumBlocks() const { return Entries.NumBlocks(); }
	// Slots的数目
	uint32			NumSlots() const;
	void			LogStats(FOutputDevice& Ar) const;
	uint8**			GetBlocksForDebugVisualizer() { return Entries.GetBlocksForDebugVisualizer(); }
	TArray<const FNameEntry*> DebugDump() const;

private:
	enum { MaxENames = 512 };
	// 分配器
	FNameEntryAllocator Entries;

#if WITH_CASE_PRESERVING_NAME
	// 显示分片
	FNamePoolShard<ENameCase::CaseSensitive> DisplayShards[FNamePoolShards];
#endif
	// 比较分片
	FNamePoolShard<ENameCase::IgnoreCase> ComparisonShards[FNamePoolShards];

	// Put constant lookup on separate cache line to avoid it being constantly invalidated by insertion
	// 将持续查找放在单独的缓存行上，以防止其因插入而不断失效
	alignas(PLATFORM_CACHE_LINE_SIZE) FNameEntryId ENameToEntry[NAME_MaxHardcodedNameIndex] = {};
	// 
	uint32 LargestEnameUnstableId;
	// EName和EntryId的Hash表
	TMap<FNameEntryId, EName, TInlineSetAllocator<MaxENames>> EntryToEName;
};

FNamePool::FNamePool()
{
	// 初始化比较分片
	for (FNamePoolShardBase& Shard : ComparisonShards)
	{
		Shard.Initialize(Entries);
	}

	// 初始化显示分片
#if WITH_CASE_PRESERVING_NAME
	for (FNamePoolShardBase& Shard : DisplayShards)
	{
		Shard.Initialize(Entries);
	}
#endif

	// Register all hardcoded names
	// 注册所有硬编码的名字
#define REGISTER_NAME(num, name) ENameToEntry[num] = Store(FNameStringView(#name, FCStringAnsi::Strlen(#name)));
#include "UObject/UnrealNames.inl"
#undef REGISTER_NAME

	// Make reverse mapping
	// 创建保留的名字HashMap
	LargestEnameUnstableId = 0;
	for (uint32 ENameIndex = 0; ENameIndex < NAME_MaxHardcodedNameIndex; ++ENameIndex)
	{
		if (ENameIndex == NAME_None || ENameToEntry[ENameIndex])
		{
			EntryToEName.Add(ENameToEntry[ENameIndex], (EName)ENameIndex);
			LargestEnameUnstableId = FMath::Max(LargestEnameUnstableId, ENameToEntry[ENameIndex].ToUnstableInt());
		}
	}

	// Verify all ENames are unique
	// 校验所有的ENames是否是唯一的
	if (NumAnsiEntries() != EntryToEName.Num())
	{
		// we can't print out here because there may be no log yet if this happens before main starts
		if (FPlatformMisc::IsDebuggerPresent())
		{
			UE_DEBUG_BREAK();
		}
		else
		{
			FPlatformMisc::PromptForRemoteDebugging(false);
			FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("UnrealEd", "DuplicatedHardcodedName", "Duplicate hardcoded name"));
			FPlatformMisc::RequestExit(false);
		}
	}
}

// 是不是都是Ansi编码
static bool IsPureAnsi(const WIDECHAR* Str, const int32 Len)
{
	// Consider SSE version if this function takes significant amount of time
	uint32 Result = 0;
	for (int32 I = 0; I < Len; ++I)
	{
		Result |= TChar<WIDECHAR>::ToUnsigned(Str[I]);
	}
	return !(Result & 0xffffff80u);
}

// 直接从数组里面取
FNameEntryId FNamePool::Find(EName Ename) const
{
	checkSlow(Ename < NAME_MaxHardcodedNameIndex);
	return ENameToEntry[Ename];
}

// 找到名字对应的EntryId
FNameEntryId FNamePool::Find(FNameStringView Name) const
{
#if WITH_CASE_PRESERVING_NAME
	FNameDisplayValue DisplayValue(Name);
	if (FNameEntryId Existing = DisplayShards[DisplayValue.Hash.ShardIndex].Find(DisplayValue))
	{
		return Existing;
	}
#endif

	FNameComparisonValue ComparisonValue(Name);
	return ComparisonShards[ComparisonValue.Hash.ShardIndex].Find(ComparisonValue);
}

// 注册名字
FNameEntryId FNamePool::Store(FNameStringView Name)
{
#if WITH_CASE_PRESERVING_NAME  
	// 如果大小写敏感的话，先从显示Shard中查找，如果找到，就直接使用
	// 如果Name和显示的名字一样，那其实Shards里面保存的就是ComparisonId
	FNameDisplayValue DisplayValue(Name);
	FNamePoolShard<ENameCase::CaseSensitive>& DisplayShard = DisplayShards[DisplayValue.Hash.ShardIndex];
	if (FNameEntryId Existing = DisplayShard.Find(DisplayValue))
	{
		return Existing;
	}
#endif

	bool bAdded = false;

	// Insert comparison name first since display value must contain comparison name
	// 首先插入比较名字因为显示值必须包含比较名字
	FNameComparisonValue ComparisonValue(Name);
	FNameEntryId ComparisonId = ComparisonShards[ComparisonValue.Hash.ShardIndex].Insert(ComparisonValue, bAdded);

#if WITH_CASE_PRESERVING_NAME
	// Check if ComparisonId can be used as DisplayId
	// 检查是否ComparisonId可以用来作为DisplayId
	if (bAdded || EqualsSameDimensions<ENameCase::CaseSensitive>(Resolve(ComparisonId), Name))
	{
		// 插入已经存在的
		DisplayShard.InsertExistingEntry(DisplayValue.Hash, ComparisonId);
		return ComparisonId;
	}
	else
	{
		// 加入已经存在的
		DisplayValue.ComparisonId = ComparisonId;
		return DisplayShard.Insert(DisplayValue, bAdded);
	}
#else
	return ComparisonId;
#endif
}

// 批量加锁
void FNamePool::BatchLock()
{
	for (const FNamePoolShardBase& Shard : ComparisonShards)
	{
		Shard.BatchLock();
	}

	// Acquire entry allocator lock after shard locks
	Entries.BatchLock();
}

FORCEINLINE FNameEntryId FNamePool::BatchStore(const FNameComparisonValue& ComparisonValue)
{
	bool bCreatedNewEntry;
	return ComparisonShards[ComparisonValue.Hash.ShardIndex].Insert<FNullScopeLock>(ComparisonValue, bCreatedNewEntry);
}

// 批量解锁
void FNamePool::BatchUnlock()
{
	Entries.BatchUnlock();

	for (int32 Idx = FNamePoolShards - 1; Idx >= 0; --Idx)
	{
		ComparisonShards[Idx].BatchUnlock();
	}
}

// 总共多少个Entries，
uint32 FNamePool::NumEntries() const
{
	uint32 Out = 0;
	// 遍历显示Shard（如果大小写敏感的话）
#if WITH_CASE_PRESERVING_NAME
	for (const FNamePoolShardBase& Shard : DisplayShards)
	{
		Out += Shard.NumCreated();
	}
#endif
	// 遍历比较Shard
	for (const FNamePoolShardBase& Shard : ComparisonShards)
	{
		Out += Shard.NumCreated();
	}

	return Out;
}

// 计算多少个Ansi的Entries
uint32 FNamePool::NumAnsiEntries() const
{
	// 总的Entries减去Unicode的Entries
	return NumEntries() - NumWideEntries();
}

// 计算多少个宽字符Entries
uint32 FNamePool::NumWideEntries() const
{
	uint32 Out = 0;
	// 遍历显示Shard（如果大小写敏感的话）,统计显示的Shard
#if WITH_CASE_PRESERVING_NAME
	for (const FNamePoolShardBase& Shard : DisplayShards)
	{
		Out += Shard.NumCreatedWide();
	}
#endif
	// 遍历比较Shard，统计比较的Shard
	for (const FNamePoolShardBase& Shard : ComparisonShards)
	{
		Out += Shard.NumCreatedWide();
	}

	return Out;
}

// 得到Slots的数目
uint32 FNamePool::NumSlots() const
{
	uint32 SlotCapacity = 0;
	// 显示Shard的Slot数目
#if WITH_CASE_PRESERVING_NAME
	for (const FNamePoolShardBase& Shard : DisplayShards)
	{
		SlotCapacity += Shard.Capacity();
	}
#endif
	// 比较Shard的数目
	for (const FNamePoolShardBase& Shard : ComparisonShards)
	{
		SlotCapacity += Shard.Capacity();
	}

	return SlotCapacity;
}

void FNamePool::LogStats(FOutputDevice& Ar) const
{
	Ar.Logf(TEXT("%i FNames using in %ikB + %ikB"), NumEntries(), sizeof(FNamePool), Entries.NumBlocks() * FNameEntryAllocator::BlockSizeBytes / 1024);
}

TArray<const FNameEntry*> FNamePool::DebugDump() const
{
	TArray<const FNameEntry*> Out;
	Out.Reserve(NumEntries());
	Entries.DebugDump(Out);
	return Out;
}

// Handle是否合法
bool FNamePool::IsValid(FNameEntryHandle Handle) const
{
	return Handle.Block < Entries.NumBlocks();
}

// 查找Id对应的EName
const EName* FNamePool::FindEName(FNameEntryId Id) const
{
	return Id.ToUnstableInt() > LargestEnameUnstableId ? nullptr : EntryToEName.Find(Id);
}

// 保留多少字节，每个分片保留多少个Entries
void FNamePool::Reserve(uint32 NumBytes, uint32 InNumEntries)
{
	// 计算出多少个Block
	uint32 NumBlocks = NumBytes / FNameEntryAllocator::BlockSizeBytes + 1;
	// 分配器保留多少个Block
	Entries.ReserveBlocks(NumBlocks);
	// 如果所有的Entries比要求的少
	if (NumEntries() < InNumEntries)
	{
		// 将要求的Entries数目平均分配在分片上
		uint32 NumEntriesPerShard = InNumEntries / FNamePoolShards + 1;

	#if WITH_CASE_PRESERVING_NAME
		// 每个显示分片保留多少个
		for (FNamePoolShard<ENameCase::CaseSensitive>& Shard : DisplayShards)
		{
			Shard.Reserve(NumEntriesPerShard);
		}
	#endif
		// 每个比较分片保留多少个
		for (FNamePoolShard<ENameCase::IgnoreCase>& Shard : ComparisonShards)
		{
			Shard.Reserve(NumEntriesPerShard);
		}
	}
}

static bool bNamePoolInitialized;
alignas(FNamePool) static uint8 NamePoolData[sizeof(FNamePool)];

// Only call this once per public FName function called
//
// Not using magic statics to run as little code as possible
// 得到名字库，如果没有初始化就初始化
static FNamePool& GetNamePool()
{
	if (bNamePoolInitialized)
	{
		return *(FNamePool*)NamePoolData;
	}

	FNamePool* Singleton = new (NamePoolData) FNamePool;
	bNamePoolInitialized = true;
	return *Singleton;
}

// Only call from functions guaranteed to run after FName lazy initialization
// 仅从保证在FName延迟初始化之后运行的函数调用
static FNamePool& GetNamePoolPostInit()
{
	// 确定名字池已经初始化
	checkSlow(bNamePoolInitialized);
	return (FNamePool&)NamePoolData;
}

// 比较名字入口的名字值和名字是否一致
bool operator==(FNameEntryId Id, EName Ename)
{
	return Id == GetNamePoolPostInit().Find(Ename);
}

// 按字母顺序比较不同的ID
static int32 CompareDifferentIdsAlphabetically(FNameEntryId AId, FNameEntryId BId)
{
	checkSlow(AId != BId);

	FNamePool& Pool = GetNamePool();
	FNameBuffer ABuffer, BBuffer;
	// 通过AId得到FNameStringView
	FNameStringView AView =	Pool.Resolve(AId).MakeView(ABuffer);
	FNameStringView BView =	Pool.Resolve(BId).MakeView(BBuffer);

	// If only one view is wide, convert the ansi view to wide as well
	// 如果其中一个是宽字符，另外一个是Ansi，就将Ansi字符转换成宽字符
	if (AView.bIsWide != BView.bIsWide)
	{
		FNameStringView& AnsiView = AView.bIsWide ? BView : AView;
		FNameBuffer& AnsiBuffer =	AView.bIsWide ? BBuffer : ABuffer;

#ifndef WITH_CUSTOM_NAME_ENCODING
		// 先将Ansi的字符串拷贝到Buffer
		FPlatformMemory::Memcpy(AnsiBuffer.AnsiName, AnsiView.Ansi, AnsiView.Len * sizeof(ANSICHAR));
		AnsiView.Ansi = AnsiBuffer.AnsiName;
#endif
		// 转换成宽字符
		ConvertInPlace<ANSICHAR, WIDECHAR>(AnsiBuffer.AnsiName, AnsiView.Len);
		AnsiView.bIsWide = true;
	}

	int32 MinLen = FMath::Min(AView.Len, BView.Len);
	// 根据是否是宽字符，调用不同的比较函数
	if (int32 StrDiff = AView.bIsWide ?	FCStringWide::Strnicmp(AView.Wide, BView.Wide, MinLen) :
										FCStringAnsi::Strnicmp(AView.Ansi, BView.Ansi, MinLen))
	{
		return StrDiff;
	}

	return AView.Len - BView.Len;
}

// 按字母顺序比较
int32 FNameEntryId::CompareLexical(FNameEntryId Rhs) const
{
	return Value != Rhs.Value && CompareDifferentIdsAlphabetically(*this, Rhs);
}

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	void CallNameCreationHook();
#else
	FORCEINLINE void CallNameCreationHook()
	{
	}
#endif

static FNameEntryId DebugCastNameEntryId(int32 Id) { return (FNameEntryId&)(Id); }

/**
* Helper function that can be used inside the debuggers watch window. E.g. "DebugFName(Class->Name.Index)". 
*
* @param	Index	Name index to look up string for
* @return			Associated name
*/
const TCHAR* DebugFName(FNameEntryId Index)
{
	// Hardcoded static array. This function is only used inside the debugger so it should be fine to return it.
	static TCHAR TempName[NAME_SIZE];
	FCString::Strcpy(TempName, *FName::SafeString(Index));
	return TempName;
}

/**
* Helper function that can be used inside the debuggers watch window. E.g. "DebugFName(Class->Name.Index, Class->Name.Number)". 
*
* @param	Index	Name index to look up string for
* @param	Number	Internal instance number of the FName to print (which is 1 more than the printed number)
* @return			Associated name
*/
const TCHAR* DebugFName(int32 Index, int32 Number)
{
	// Hardcoded static array. This function is only used inside the debugger so it should be fine to return it.
	static TCHAR TempName[NAME_SIZE];
	FCString::Strcpy(TempName, *FName::SafeString(DebugCastNameEntryId(Index), Number));
	return TempName;
}

/**
 * Helper function that can be used inside the debuggers watch window. E.g. "DebugFName(Class->Name)". 
 *
 * @param	Name	Name to look up string for
 * @return			Associated name
 */
const TCHAR* DebugFName(FName& Name)
{
	// Hardcoded static array. This function is only used inside the debugger so it should be fine to return it.
	static TCHAR TempName[NAME_SIZE];
	FCString::Strcpy(TempName, *FName::SafeString(Name.GetDisplayIndex(), Name.GetNumber()));
	return TempName;
}

// 得到最原始CRC32的Hash值
template <typename TCharType>
static uint16 GetRawCasePreservingHash(const TCharType* Source)
{
	return FCrc::StrCrc32(Source) & 0xFFFF;

}
template <typename TCharType>
static uint16 GetRawNonCasePreservingHash(const TCharType* Source)
{
	return FCrc::Strihash_DEPRECATED(Source) & 0xFFFF;
}

/*-----------------------------------------------------------------------------
	FNameEntry
-----------------------------------------------------------------------------*/
// 保存指定长度的字符，不一定有终止符的名字
void FNameEntry::StoreName(const ANSICHAR* InName, uint32 Len)
{
	FPlatformMemory::Memcpy(AnsiName, InName, sizeof(ANSICHAR) * Len);
	Encode(AnsiName, Len);
}

// 保存指定长度的字符，不一定有终止符的名字
void FNameEntry::StoreName(const WIDECHAR* InName, uint32 Len)
{
	FPlatformMemory::Memcpy(WideName, InName, sizeof(WIDECHAR) * Len);
	Encode(WideName, Len);
}

// 拷贝指定长度的，不一定有终止符的名字
void FNameEntry::CopyUnterminatedName(ANSICHAR* Out) const
{
	FPlatformMemory::Memcpy(Out, AnsiName, sizeof(ANSICHAR) * Header.Len);
	Decode(Out, Header.Len);
}

// 拷贝指定长度的，不一定有终止符的名字
void FNameEntry::CopyUnterminatedName(WIDECHAR* Out) const
{
	FPlatformMemory::Memcpy(Out, WideName, sizeof(WIDECHAR) * Header.Len);
	Decode(Out, Header.Len);
}

// 得到没有终止符的名字，如果有自定义的解码，需要调用解码，不然就直接返回
FORCEINLINE const WIDECHAR* FNameEntry::GetUnterminatedName(WIDECHAR(&OptionalDecodeBuffer)[NAME_SIZE]) const
{
#ifdef WITH_CUSTOM_NAME_ENCODING
	CopyUnterminatedName(OptionalDecodeBuffer);
	return OptionalDecodeBuffer;
#else
	return WideName;
#endif
}

// // 得到没有终止符的名字，如果有自定义的解码，需要调用解码，不然就直接返回
FORCEINLINE ANSICHAR const* FNameEntry::GetUnterminatedName(ANSICHAR(&OptionalDecodeBuffer)[NAME_SIZE]) const
{
#ifdef WITH_CUSTOM_NAME_ENCODING
	CopyUnterminatedName(OptionalDecodeBuffer);
	return OptionalDecodeBuffer;
#else
	return AnsiName;
#endif
}

// 通过NameBuffer创建String View
FORCEINLINE FNameStringView FNameEntry::MakeView(FNameBuffer& OptionalDecodeBuffer) const
{
	return IsWide()	? FNameStringView(GetUnterminatedName(OptionalDecodeBuffer.WideName), GetNameLength())
					: FNameStringView(GetUnterminatedName(OptionalDecodeBuffer.AnsiName), GetNameLength());
}

// 将不带终止符的名称复制到无需分配的TCHAR缓冲区
void FNameEntry::GetUnterminatedName(TCHAR* OutName, uint32 OutLen) const
{
	check(static_cast<int32>(OutLen) >= GetNameLength());
	CopyAndConvertUnterminatedName(OutName);
}

// 拷贝以0为结尾的名称到无需分配的TCHAR缓冲区
void FNameEntry::GetName(TCHAR(&OutName)[NAME_SIZE]) const
{
	CopyAndConvertUnterminatedName(OutName);
	OutName[GetNameLength()] = '\0';
}

// 拷贝并且转换名字
void FNameEntry::CopyAndConvertUnterminatedName(TCHAR* OutName) const
{
	if (sizeof(TCHAR) < sizeof(WIDECHAR) && IsWide()) // Normally compiled out
	{
		// 先拷贝到一个临时缓冲区
		FNameBuffer Temp;
		CopyUnterminatedName(Temp.WideName);
		// 转换
		ConvertInPlace<WIDECHAR, TCHAR>(Temp.WideName, Header.Len);
		// 再拷贝到输出参数中
		FPlatformMemory::Memcpy(OutName, Temp.AnsiName, Header.Len * sizeof(TCHAR));
	}
	else if (IsWide())
	{
		// 拷贝
		CopyUnterminatedName((WIDECHAR*)OutName);
		ConvertInPlace<WIDECHAR, TCHAR>((WIDECHAR*)OutName, Header.Len);
	}
	else
	{
		CopyUnterminatedName((ANSICHAR*)OutName);
		ConvertInPlace<ANSICHAR, TCHAR>((ANSICHAR*)OutName, Header.Len);
	}
}

// 拷贝null结尾的名字到无需分配的ANSICHAR缓冲区，入口必须不是宽字符
void FNameEntry::GetAnsiName(ANSICHAR(&Out)[NAME_SIZE]) const
{
	check(!IsWide());
	CopyUnterminatedName(Out);
	// 注意加终止符
	Out[Header.Len] = '\0';
}

// 拷贝null结尾的名字到无需分配的WIDECHAR缓冲区，入口必须是宽字符
void FNameEntry::GetWideName(WIDECHAR(&Out)[NAME_SIZE]) const
{
	check(IsWide());
	CopyUnterminatedName(Out);
	// 注意加终止符
	Out[Header.Len] = '\0';
}

/** @return null-terminated string */
// 返回null终止的字符串
static const TCHAR* EntryToCString(const FNameEntry& Entry, FNameBuffer& Temp)
{
	// 宽字符
	if (Entry.IsWide())
	{
		Entry.GetWideName(Temp.WideName);
		// 宽字符转换成TCHAR
		return ConvertInPlace<WIDECHAR, TCHAR>(Temp.WideName, Entry.GetNameLength() + 1);
	}
	else
	{
		// ANSI转换成TCHAR
		Entry.GetAnsiName(Temp.AnsiName);
		return ConvertInPlace<ANSICHAR, TCHAR>(Temp.AnsiName, Entry.GetNameLength() + 1);
	}
}

// 以字符串的方式返回名字
FString FNameEntry::GetPlainNameString() const
{
	FNameBuffer Temp;
	if (Header.bIsWide)
	{
		return FString(Header.Len, GetUnterminatedName(Temp.WideName));
	}
	else
	{
		return FString(Header.Len, GetUnterminatedName(Temp.AnsiName));
	}
}

// 将名字附加到字符串的尾部 
void FNameEntry::AppendNameToString(FString& Out) const
{
	FNameBuffer Temp;
	Out.Append(EntryToCString(*this, Temp), Header.Len);
}
// 将名字附加到字符串的尾部 
void FNameEntry::AppendNameToString(FStringBuilderBase& Out) const
{
	const int32 Offset = Out.AddUninitialized(Header.Len);
	TCHAR* OutChars = Out.GetData() + Offset;
	if (Header.bIsWide)
	{
		CopyUnterminatedName(reinterpret_cast<WIDECHAR*>(OutChars));
		ConvertInPlace<WIDECHAR, TCHAR>(reinterpret_cast<WIDECHAR*>(OutChars), Header.Len);
	}
	else
	{
		CopyUnterminatedName(reinterpret_cast<ANSICHAR*>(OutChars));
		ConvertInPlace<ANSICHAR, TCHAR>(reinterpret_cast<ANSICHAR*>(OutChars), Header.Len);
	}
}

// 将名字附加到字符串的尾部,入口不能说宽字符
void FNameEntry::AppendAnsiNameToString(FAnsiStringBuilderBase& Out) const
{
	check(!IsWide());
	const int32 Offset = Out.AddUninitialized(Header.Len);
	CopyUnterminatedName(Out.GetData() + Offset);
}

// 使用路径分隔符附加名字到字符串，使用FString::PathAppend()
void FNameEntry::AppendNameToPathString(FString& Out) const
{
	FNameBuffer Temp;
	Out.PathAppend(EntryToCString(*this, Temp), Header.Len);
}

// 返回以字节为单位的大小，这个不等于sizeof(FNameEntry)，我们只计算实际占用的字节，而不是整个结构体的字节数
int32 FNameEntry::GetSize(const TCHAR* Name)
{
	return FNameEntry::GetSize(FCString::Strlen(Name), FCString::IsPureAnsi(Name));
}

// 返回以字节为单位的大小，这个不等于sizeof(FNameEntry)，我们只计算实际占用的字节，而不是整个结构体的字节数
int32 FNameEntry::GetSize(int32 Length, bool bIsPureAnsi)
{
	int32 Bytes = GetDataOffset() + Length * (bIsPureAnsi ? sizeof(ANSICHAR) : sizeof(WIDECHAR));
	return Align(Bytes, alignof(FNameEntry));
}
// 返回以字节为单位的大小
int32 FNameEntry::GetSizeInBytes() const
{
	return GetSize(GetNameLength(), !IsWide());
}

// 从一个FNameEntry来构造FNameEntrySerialized
FNameEntrySerialized::FNameEntrySerialized(const FNameEntry& NameEntry)
{
	// 从一个FNameEntry来构造FNameEntrySerialized
	bIsWide = NameEntry.IsWide();
	if (bIsWide)
	{
		NameEntry.GetWideName(WideName);
		NonCasePreservingHash = GetRawNonCasePreservingHash(WideName);
		CasePreservingHash = GetRawCasePreservingHash(WideName);
	}
	else
	{
		NameEntry.GetAnsiName(AnsiName);
		NonCasePreservingHash = GetRawNonCasePreservingHash(AnsiName);
		CasePreservingHash = GetRawCasePreservingHash(AnsiName);
	}
}

/**
 * @return FString of name portion minus number.
 */
// 返回不包括编号部分的名字的FString
FString FNameEntrySerialized::GetPlainNameString() const
{
	if (bIsWide)
	{
		return FString(WideName);
	}
	else
	{
		return FString(AnsiName);
	}
}

/*-----------------------------------------------------------------------------
	FName statics.
-----------------------------------------------------------------------------*/
// 所有名字入口的内存大小
int32 FName::GetNameEntryMemorySize()
{
	return GetNamePool().NumBlocks() * FNameEntryAllocator::BlockSizeBytes;
}

// 名称表对象整体的内存大小：名字入口的内存+名字池本身的内存+名字池Slot的内存
int32 FName::GetNameTableMemorySize()
{
	return GetNameEntryMemorySize() + sizeof(FNamePool) + GetNamePool().NumSlots() * sizeof(FNameSlot);
}

// 名称表中ansi名称的数目
int32 FName::GetNumAnsiNames()
{
	return GetNamePool().NumAnsiEntries();
}

// 名称表中宽字符名称的数目
int32 FName::GetNumWideNames()
{
	return GetNamePool().NumWideEntries();
}

TArray<const FNameEntry*> FName::DebugDump()
{
	return GetNamePool().DebugDump();
}

// 得到EName中的名字入口
FNameEntry const* FName::GetEntry(EName Ename)
{
	FNamePool& Pool = GetNamePool();
	return &Pool.Resolve(Pool.Find(Ename));
}

// 得到Id对应的Entry
FNameEntry const* FName::GetEntry(FNameEntryId Id)
{
	return &GetNamePool().Resolve(Id);
}

FString FName::NameToDisplayString( const FString& InDisplayName, const bool bIsBool )
{
	// Copy the characters out so that we can modify the string in place
	const TArray< TCHAR >& Chars = InDisplayName.GetCharArray();

	// This is used to indicate that we are in a run of uppercase letter and/or digits.  The code attempts to keep
	// these characters together as breaking them up often looks silly (i.e. "Draw Scale 3 D" as opposed to "Draw Scale 3D"
	bool bInARun = false;
	bool bWasSpace = false;
	bool bWasOpenParen = false;
	bool bWasNumber = false;
	bool bWasMinusSign = false;

	FString OutDisplayName;
	OutDisplayName.GetCharArray().Reserve(Chars.Num());
	for( int32 CharIndex = 0 ; CharIndex < Chars.Num() ; ++CharIndex )
	{
		TCHAR ch = Chars[CharIndex];

		bool bLowerCase = FChar::IsLower( ch );
		bool bUpperCase = FChar::IsUpper( ch );
		bool bIsDigit = FChar::IsDigit( ch );
		bool bIsUnderscore = FChar::IsUnderscore( ch );

		// Skip the first character if the property is a bool (they should all start with a lowercase 'b', which we don't want to keep)
		if( CharIndex == 0 && bIsBool && ch == 'b' )
		{
			// Check if next character is uppercase as it may be a user created string that doesn't follow the rules of Unreal variables
			if (Chars.Num() > 1 && FChar::IsUpper(Chars[1]))
			{
				continue;
			}
		}

		// If the current character is upper case or a digit, and the previous character wasn't, then we need to insert a space if there wasn't one previously
		// We don't do this for numerical expressions, for example "-1.2" should not be formatted as "- 1. 2"
		if( (bUpperCase || (bIsDigit && !bWasMinusSign)) && !bInARun && !bWasOpenParen && !bWasNumber)
		{
			if( !bWasSpace && OutDisplayName.Len() > 0 )
			{
				OutDisplayName += TEXT( ' ' );
				bWasSpace = true;
			}
			bInARun = true;
		}

		// A lower case character will break a run of upper case letters and/or digits
		if( bLowerCase )
		{
			bInARun = false;
		}

		// An underscore denotes a space, so replace it and continue the run
		if( bIsUnderscore )
		{
			ch = TEXT( ' ' );
			bInARun = true;
		}

		// If this is the first character in the string, then it will always be upper-case
		if( OutDisplayName.Len() == 0 )
		{
			ch = FChar::ToUpper( ch );
		}
		else if( !bIsDigit && (bWasSpace || bWasOpenParen))	// If this is first character after a space, then make sure it is case-correct
		{
			// Some words are always forced lowercase
			const TCHAR* Articles[] =
			{
				TEXT( "In" ),
				TEXT( "As" ),
				TEXT( "To" ),
				TEXT( "Or" ),
				TEXT( "At" ),
				TEXT( "On" ),
				TEXT( "If" ),
				TEXT( "Be" ),
				TEXT( "By" ),
				TEXT( "The" ),
				TEXT( "For" ),
				TEXT( "And" ),
				TEXT( "With" ),
				TEXT( "When" ),
				TEXT( "From" ),
			};

			// Search for a word that needs case repaired
			bool bIsArticle = false;
			for( int32 CurArticleIndex = 0; CurArticleIndex < UE_ARRAY_COUNT( Articles ); ++CurArticleIndex )
			{
				// Make sure the character following the string we're testing is not lowercase (we don't want to match "in" with "instance")
				const int32 ArticleLength = FCString::Strlen( Articles[ CurArticleIndex ] );
				if( ( Chars.Num() - CharIndex ) > ArticleLength && !FChar::IsLower( Chars[ CharIndex + ArticleLength ] ) && Chars[ CharIndex + ArticleLength ] != '\0' )
				{
					// Does this match the current article?
					if( FCString::Strncmp( &Chars[ CharIndex ], Articles[ CurArticleIndex ], ArticleLength ) == 0 )
					{
						bIsArticle = true;
						break;
					}
				}
			}

			// Start of a keyword, force to lowercase
			if( bIsArticle )
			{
				ch = FChar::ToLower( ch );				
			}
			else	// First character after a space that's not a reserved keyword, make sure it's uppercase
			{
				ch = FChar::ToUpper( ch );
			}
		}

		bWasSpace = ( ch == TEXT( ' ' ) ? true : false );
		bWasOpenParen = ( ch == TEXT( '(' ) ? true : false );

		// What could be included as part of a numerical representation.
		// For example -1.2
		bWasMinusSign = (ch == TEXT('-'));
		const bool bPotentialNumericalChar = bWasMinusSign || (ch == TEXT('.'));
		bWasNumber = bIsDigit || (bWasNumber && bPotentialNumericalChar);

		OutDisplayName += ch;
	}

	return OutDisplayName;
}

// 获取此FName表示的EName或nullptr
const EName* FName::ToEName() const
{
	return GetNamePoolPostInit().FindEName(ComparisonIndex);
}

//  FNameEntryId是否合法
bool FName::IsWithinBounds(FNameEntryId Id)
{
	return GetNamePoolPostInit().IsValid(Id);
}

/*-----------------------------------------------------------------------------
	FName implementation.
-----------------------------------------------------------------------------*/

template<class CharType>
static bool NumberEqualsString(uint32 Number, const CharType* Str)
{
	CharType* End = nullptr;
	return TCString<CharType>::Strtoi64(Str, &End, 10) == Number && End && *End == '\0';
}

template<class CharType1, class CharType2>
static bool StringAndNumberEqualsString(const CharType1* Name, uint32 NameLen, int32 InternalNumber, const CharType2* Str)
{
	if (FPlatformString::Strnicmp(Name, Str, NameLen))
	{
		return false;
	}

	if (InternalNumber == NAME_NO_NUMBER_INTERNAL)
	{
		return Str[NameLen] == '\0';
	}

	uint32 Number = NAME_INTERNAL_TO_EXTERNAL(InternalNumber);
	return Str[NameLen] == '_' && NumberEqualsString(Number, Str + NameLen + 1);
}

struct FNameAnsiStringView
{
	using CharType = ANSICHAR;

	const ANSICHAR* Str;
	int32 Len;
};

struct FWideStringViewWithWidth
{
	using CharType = WIDECHAR;

	const WIDECHAR* Str;
	int32 Len;
	bool bIsWide;
};

static FNameAnsiStringView MakeUnconvertedView(const ANSICHAR* Str, int32 Len)
{
	return { Str, Len };
}

static FNameAnsiStringView MakeUnconvertedView(const ANSICHAR* Str)
{
	return { Str, Str ? FCStringAnsi::Strlen(Str) : 0 };
}

static bool IsWide(const WIDECHAR* Str, const int32 Len)
{
	uint32 UserCharBits = 0;
	for (int32 I = 0; I < Len; ++I)
	{
		UserCharBits |= TChar<WIDECHAR>::ToUnsigned(Str[I]);
	}
	return UserCharBits & 0xffffff80u;
}

static int32 GetLengthAndWidth(const WIDECHAR* Str, bool& bOutIsWide)
{
	uint32 UserCharBits = 0;
	const WIDECHAR* It = Str;
	if (Str)
	{
		while (*It)
		{
			UserCharBits |= TChar<WIDECHAR>::ToUnsigned(*It);
			++It;
		}
	}

	bOutIsWide = UserCharBits & 0xffffff80u;

	return UE_PTRDIFF_TO_INT32(It - Str);
}

static FWideStringViewWithWidth MakeUnconvertedView(const WIDECHAR* Str, int32 Len)
{
	return { Str, Len, IsWide(Str, Len) };
}

static FWideStringViewWithWidth MakeUnconvertedView(const WIDECHAR* Str)
{
	FWideStringViewWithWidth View;
	View.Str = Str;
	View.Len = GetLengthAndWidth(Str, View.bIsWide);
	return View;
}

// @pre Str contains only digits and the number is smaller than int64 max
template<typename CharType>
static constexpr int64 Atoi64(const CharType* Str, int32 Len)
{
    int64 N = 0;
    for (int32 Idx = 0; Idx < Len; ++Idx)
    {
        N = 10 * N + Str[Idx] - '0';
    }

    return N;
}

/** Templated implementations of non-templated member functions, helps keep header clean */
struct FNameHelper
{
	template<typename ViewType>
	static FName MakeDetectNumber(ViewType View, EFindName FindType)
	{
		if (View.Len == 0)
		{
			return FName();
		}
		
		uint32 InternalNumber = ParseNumber(View.Str, /* may be shortened */ View.Len);
		return MakeWithNumber(View, FindType, InternalNumber);
	}

	template<typename CharType>
	static uint32 ParseNumber(const CharType* Name, int32& InOutLen)
	{
		const int32 Len = InOutLen;
		int32 Digits = 0;
		for (const CharType* It = Name + Len - 1; It >= Name && *It >= '0' && *It <= '9'; --It)
		{
			++Digits;
		}

		const CharType* FirstDigit = Name + Len - Digits;
		static constexpr int32 MaxDigitsInt32 = 10;
		if (Digits && Digits < Len && *(FirstDigit - 1) == '_' && Digits <= MaxDigitsInt32)
		{
			// check for the case where there are multiple digits after the _ and the first one
			// is a 0 ("Rocket_04"). Can't split this case. (So, we check if the first char
			// is not 0 or the length of the number is 1 (since ROcket_0 is valid)
			if (Digits == 1 || *FirstDigit != '0')
			{
				int64 Number = Atoi64(Name + Len - Digits, Digits);
				if (Number < MAX_int32)
				{
					InOutLen -= 1 + Digits;
					return static_cast<uint32>(NAME_EXTERNAL_TO_INTERNAL(Number));
				}
			}
		}

		return NAME_NO_NUMBER_INTERNAL;
	}

	static FName MakeWithNumber(FNameAnsiStringView	 View, EFindName FindType, int32 InternalNumber)
	{
		// Ignore the supplied number if the name string is empty
		// to keep the semantics of the old FName implementation
		if (View.Len == 0)
		{
			return FName();
		}

		return Make(FNameStringView(View.Str, View.Len), FindType, InternalNumber);
	}

	static FName MakeWithNumber(const FWideStringViewWithWidth View, EFindName FindType, int32 InternalNumber)
	{
		// Ignore the supplied number if the name string is empty
		// to keep the semantics of the old FName implementation
		if (View.Len == 0)
		{
			return FName();
		}

		// Convert to narrow if possible
		if (!View.bIsWide)
		{
			// Consider _mm_packus_epi16 or similar if this proves too slow
			ANSICHAR AnsiName[NAME_SIZE];
			for (int32 I = 0, Len = FMath::Min<int32>(View.Len, NAME_SIZE); I < Len; ++I)
			{
				AnsiName[I] = View.Str[I];
			}
			return Make(FNameStringView(AnsiName, View.Len), FindType, InternalNumber);
		}
		else
		{
			return Make(FNameStringView(View.Str, View.Len), FindType, InternalNumber);
		}
	}

	static FName Make(FNameStringView View, EFindName FindType, int32 InternalNumber)
	{
		if (View.Len >= NAME_SIZE)
		{
			checkf(false, TEXT("FName's %d max length exceeded. Got %d characters excluding null-terminator."), NAME_SIZE - 1, View.Len);
			return FName("ERROR_NAME_SIZE_EXCEEDED");
		}
		
		FNamePool& Pool = GetNamePool();

		FNameEntryId DisplayId, ComparisonId;
		if (FindType == FNAME_Add)
		{
			DisplayId = Pool.Store(View);
#if WITH_CASE_PRESERVING_NAME
			ComparisonId = Pool.Resolve(DisplayId).ComparisonId;
#else
			ComparisonId = DisplayId;
#endif
		}
		else if (FindType == FNAME_Find)
		{
			DisplayId = Pool.Find(View);
#if WITH_CASE_PRESERVING_NAME
			ComparisonId = DisplayId ? Pool.Resolve(DisplayId).ComparisonId : DisplayId;
#else
			ComparisonId = DisplayId;
#endif
		}
		else
		{
			check(FindType == FNAME_Replace_Not_Safe_For_Threading);

#if FNAME_WRITE_PROTECT_PAGES
			checkf(false, TEXT("FNAME_Replace_Not_Safe_For_Threading can't be used together with page protection."));
#endif
			DisplayId = Pool.Store(View);
#if WITH_CASE_PRESERVING_NAME
			ComparisonId = Pool.Resolve(DisplayId).ComparisonId;
#else
			ComparisonId = DisplayId;
#endif
			ReplaceName(Pool.Resolve(ComparisonId), View);
		}

		return FName(ComparisonId, DisplayId, InternalNumber);
	}

	static FName MakeFromLoaded(const FNameEntrySerialized& LoadedEntry)
	{
		FNameStringView View = LoadedEntry.bIsWide
			? FNameStringView(LoadedEntry.WideName, FCStringWide::Strlen(LoadedEntry.WideName))
			: FNameStringView(LoadedEntry.AnsiName, FCStringAnsi::Strlen(LoadedEntry.AnsiName));

		return Make(View, FNAME_Add, NAME_NO_NUMBER_INTERNAL);
	}

	template<class CharType>
	static bool EqualsString(FName Name, const CharType* Str)
	{
		// Make NAME_None == TEXT("") or nullptr consistent with NAME_None == FName(TEXT("")) or FName(nullptr)
		if (Str == nullptr || Str[0] == '\0')
		{
			return Name.IsNone();
		}

		const FNameEntry& Entry = *Name.GetComparisonNameEntry();

		uint32 NameLen = Entry.Header.Len;
		FNameBuffer Temp;
		return Entry.IsWide()
			? StringAndNumberEqualsString(Entry.GetUnterminatedName(Temp.WideName), NameLen, Name.GetNumber(), Str)
			: StringAndNumberEqualsString(Entry.GetUnterminatedName(Temp.AnsiName), NameLen, Name.GetNumber(), Str);
	}

	static void ReplaceName(FNameEntry& Existing, FNameStringView Updated)
	{
		check(Existing.Header.bIsWide == Updated.bIsWide);
		check(Existing.Header.Len == Updated.Len);

		if (Updated.bIsWide)
		{
			Existing.StoreName(Updated.Wide, Updated.Len);
		}
		else
		{
			Existing.StoreName(Updated.Ansi, Updated.Len);
		}
	}
};


#if WITH_CASE_PRESERVING_NAME
// // 从显示ID中获得比较ID，先得到入口，在从入口中得到比较ID
FNameEntryId FName::GetComparisonIdFromDisplayId(FNameEntryId DisplayId)
{
	return GetEntry(DisplayId)->ComparisonId;
}
#endif

FName::FName(const WIDECHAR* Name, EFindName FindType)
	: FName(FNameHelper::MakeDetectNumber(MakeUnconvertedView(Name), FindType))
{}

FName::FName(const ANSICHAR* Name, EFindName FindType)
	: FName(FNameHelper::MakeDetectNumber(MakeUnconvertedView(Name), FindType))
{}

FName::FName(int32 Len, const WIDECHAR* Name, EFindName FindType)
	: FName(FNameHelper::MakeDetectNumber(MakeUnconvertedView(Name, Len), FindType))
{}

FName::FName(int32 Len, const ANSICHAR* Name, EFindName FindType)
	: FName(FNameHelper::MakeDetectNumber(MakeUnconvertedView(Name, Len), FindType))
{}

FName::FName(const WIDECHAR* Name, int32 InNumber, EFindName FindType)
	: FName(FNameHelper::MakeWithNumber(MakeUnconvertedView(Name), FindType, InNumber))
{}

FName::FName(const ANSICHAR* Name, int32 InNumber, EFindName FindType)
	: FName(FNameHelper::MakeWithNumber(MakeUnconvertedView(Name), FindType, InNumber))
{}

FName::FName(int32 Len, const WIDECHAR* Name, int32 InNumber, EFindName FindType)
	: FName(InNumber != NAME_NO_NUMBER_INTERNAL ? FNameHelper::MakeWithNumber(MakeUnconvertedView(Name, Len), FindType, InNumber)
												: FNameHelper::MakeDetectNumber(MakeUnconvertedView(Name, Len), FindType))
{}

FName::FName(int32 Len, const ANSICHAR* Name, int32 InNumber, EFindName FindType)
	: FName(InNumber != NAME_NO_NUMBER_INTERNAL ? FNameHelper::MakeWithNumber(MakeUnconvertedView(Name, Len), FindType, InNumber)
												: FNameHelper::MakeDetectNumber(MakeUnconvertedView(Name, Len), FindType))
{}

FName::FName(const TCHAR* Name, int32 InNumber, EFindName FindType, bool bSplitName)
	: FName(InNumber == NAME_NO_NUMBER_INTERNAL && bSplitName 
			? FNameHelper::MakeDetectNumber(MakeUnconvertedView(Name), FindType)
			: FNameHelper::MakeWithNumber(MakeUnconvertedView(Name), FindType, InNumber))
{}

FName::FName(const FNameEntrySerialized& LoadedEntry)
	: FName(FNameHelper::MakeFromLoaded(LoadedEntry))
{}

bool FName::operator==(const ANSICHAR* Str) const
{
	return FNameHelper::EqualsString(*this, Str);
}

bool FName::operator==(const WIDECHAR* Str) const
{
	return FNameHelper::EqualsString(*this, Str);
}

// 比较名称和传入的名称。排序按字母顺序升序
int32 FName::Compare( const FName& Other ) const
{
	// Names match, check whether numbers match.
	if (ComparisonIndex == Other.ComparisonIndex)
	{
		return GetNumber() - Other.GetNumber();
	}

	// Names don't match. This means we don't even need to check numbers.
	// 名称不匹配，这意味着我们甚至不需要检查数字
	return CompareDifferentIdsAlphabetically(ComparisonIndex, Other.ComparisonIndex);
}

// 拷贝除数字部分的名字到TCHAR缓冲区并且返回字符串长度，不分配内存
uint32 FName::GetPlainNameString(TCHAR(&OutName)[NAME_SIZE]) const
{
	const FNameEntry& Entry = *GetDisplayNameEntry();
	Entry.GetName(OutName);
	return Entry.GetNameLength();
}

// 获取没有数字部分的名称作为动态分配的字符串
FString FName::GetPlainNameString() const
{
	return GetDisplayNameEntry()->GetPlainNameString();
}

// 拷贝除数字部分的ANSI字符的名字，只用于ANSI字符的FNames，不分配内存
void FName::GetPlainANSIString(ANSICHAR(&AnsiName)[NAME_SIZE]) const
{
	GetDisplayNameEntry()->GetAnsiName(AnsiName);
}

// 拷贝除数字部分的宽字符名字，只用于宽字符的FNames，不分配内存
void FName::GetPlainWIDEString(WIDECHAR(&WideName)[NAME_SIZE]) const
{
	GetDisplayNameEntry()->GetWideName(WideName);
}

// 得到名字的比较入口
const FNameEntry* FName::GetComparisonNameEntry() const
{
	return &GetNamePool().Resolve(GetComparisonIndex());
}

// 得到名字的显示入口
const FNameEntry* FName::GetDisplayNameEntry() const
{
	// 通过显示索引，从名字池中得到对应的入口
	return &GetNamePool().Resolve(GetDisplayIndex());
}

// 将名字转换成可读的字符串
FString FName::ToString() const
{
	if (GetNumber() == NAME_NO_NUMBER_INTERNAL)
	{
		// Avoids some extra allocations in non-number case
		return GetDisplayNameEntry()->GetPlainNameString();
	}
	
	FString Out;	
	ToString(Out);
	return Out;
}

// 将FName转换成一个可读的格式
void FName::ToString(FString& Out) const
{
	// A version of ToString that saves at least one string copy
	const FNameEntry* const NameEntry = GetDisplayNameEntry();
	// 如果是空实例号
	if (GetNumber() == NAME_NO_NUMBER_INTERNAL)
	{
		// 创建一个名字长度的空字符串
		Out.Empty(NameEntry->GetNameLength());
		// 把名字放进去
		NameEntry->AppendNameToString(Out);
	}	
	else
	{
		// 创建一个名字长度+6的空字符串
		Out.Empty(NameEntry->GetNameLength() + 6);
		// 先把名字放进去
		NameEntry->AppendNameToString(Out);
		// 放入下划线
		Out += TEXT('_');
		// 放入实例号
		Out.AppendInt(NAME_INTERNAL_TO_EXTERNAL(GetNumber()));
	}
}

// 将FName转换成一个可读的格式
void FName::ToString(FStringBuilderBase& Out) const
{
	Out.Reset();
	AppendString(Out);
}

// 得到字符的数目，不包括null终止符
uint32 FName::GetStringLength() const
{
	const FNameEntry& Entry = *GetDisplayNameEntry();
	uint32 NameLen = Entry.GetNameLength();

	// 如果Number是无实例的Number
	if (GetNumber() == NAME_NO_NUMBER_INTERNAL)
	{
		return NameLen;
	}
	else
	{
		TCHAR NumberSuffixStr[16];
		int32 SuffixLen = FCString::Sprintf(NumberSuffixStr, TEXT("_%d"), NAME_INTERNAL_TO_EXTERNAL(GetNumber()));
		check(SuffixLen > 0);
		// 名称长度加上后缀长度
		return NameLen + SuffixLen;
	}
}

// 转换到string缓冲区防止动态分配并且返回字符串长度
uint32 FName::ToString(TCHAR* Out, uint32 OutSize) const
{
	// 得到名字入口
	const FNameEntry& Entry = *GetDisplayNameEntry();
	// 通过入口得到名字的长度
	uint32 NameLen = Entry.GetNameLength();
	Entry.GetUnterminatedName(Out, OutSize);

	// Number是无实例的
	if (GetNumber() == NAME_NO_NUMBER_INTERNAL)
	{
		Out[NameLen] = '\0';
		return NameLen;
	}
	else
	{
		TCHAR NumberSuffixStr[16];
		// 后缀的长度
		int32 SuffixLen = FCString::Sprintf(NumberSuffixStr, TEXT("_%d"), NAME_INTERNAL_TO_EXTERNAL(GetNumber()));
		uint32 TotalLen = NameLen + SuffixLen;
		check(SuffixLen > 0 && OutSize > TotalLen);
		// 把后缀拷贝进缓冲区中
		FPlatformMemory::Memcpy(Out + NameLen, NumberSuffixStr, SuffixLen * sizeof(TCHAR));
		Out[TotalLen] = '\0';
		return TotalLen;
	}
}

// 将FName转换成可读格式，并追加到一个现有的字符串后面
void FName::AppendString(FString& Out) const
{
	const FNameEntry* const NameEntry = GetDisplayNameEntry();
	// 通过名字入口追加到字符串后面
	NameEntry->AppendNameToString( Out );
	// 如果Number不是默认的，就追加Number
	if (GetNumber() != NAME_NO_NUMBER_INTERNAL)
	{
		Out += TEXT('_');
		Out.AppendInt(NAME_INTERNAL_TO_EXTERNAL(GetNumber()));
	}
}

// 将FName转换成可读格式，并追加到一个现有的字符串后面
void FName::AppendString(FStringBuilderBase& Out) const
{
	GetDisplayNameEntry()->AppendNameToString(Out);

	const int32 InternalNumber = GetNumber();
	// 追加Number的后缀
	if (InternalNumber != NAME_NO_NUMBER_INTERNAL)
	{
		Out << TEXT('_') << NAME_INTERNAL_TO_EXTERNAL(InternalNumber);
	}
}

// 将ANSI FName转换成可读格式并附加到字符串生成器
bool FName::TryAppendAnsiString(FAnsiStringBuilderBase& Out) const
{
	const FNameEntry* const NameEntry = GetDisplayNameEntry();
	// 宽字符
	if (NameEntry->IsWide())
	{
		return false;
	}
	// 转换成Ansi格式
	NameEntry->AppendAnsiNameToString(Out);

	const int32 InternalNumber = GetNumber();
	if (InternalNumber != NAME_NO_NUMBER_INTERNAL)
	{
		Out << '_' << NAME_INTERNAL_TO_EXTERNAL(InternalNumber);
	}

	return true;
}

void FName::DisplayHash(FOutputDevice& Ar)
{
	GetNamePool().LogStats(Ar);
}

FString FName::SafeString(FNameEntryId InDisplayIndex, int32 InstanceNumber)
{
	return FName(InDisplayIndex, InDisplayIndex, InstanceNumber).ToString();
}

// 检查以确保给定的类似名称的字符串遵循Unreal要求的规则
bool FName::IsValidXName(const FName InName, const FString& InInvalidChars, FText* OutReason, const FText* InErrorCtx)
{
	TStringBuilder<FName::StringBufferSize> NameStr;
	InName.ToString(NameStr);
	return IsValidXName(FStringView(NameStr), InInvalidChars, OutReason, InErrorCtx);
}

// 检查以确保给定的类似名称的字符串遵循Unreal要求的规则
bool FName::IsValidXName(const TCHAR* InName, const FString& InInvalidChars, FText* OutReason, const FText* InErrorCtx)
{
	return IsValidXName(FStringView(InName), InInvalidChars, OutReason, InErrorCtx);
}

// 检查以确保给定的类似名称的字符串遵循Unreal要求的规则
bool FName::IsValidXName(const FString& InName, const FString& InInvalidChars, FText* OutReason, const FText* InErrorCtx)
{
	return IsValidXName(FStringView(InName), InInvalidChars, OutReason, InErrorCtx);
}

// 检查以确保给定的类似名称的字符串遵循Unreal要求的规则
bool FName::IsValidXName(const FStringView& InName, const FString& InInvalidChars, FText* OutReason, const FText* InErrorCtx)
{
	if (InName.IsEmpty() || InInvalidChars.IsEmpty())
	{
		return true;
	}

	// See if the name contains invalid characters.
	// 检查name是否包含非法字符
	FString MatchedInvalidChars;
	TSet<TCHAR> AlreadyMatchedInvalidChars;
	// 遍历非法字符，然后加入一个set
	for (const TCHAR InvalidChar : InInvalidChars)
	{
		int32 InvalidCharIndex = INDEX_NONE;
		if (!AlreadyMatchedInvalidChars.Contains(InvalidChar) && InName.FindChar(InvalidChar, InvalidCharIndex))
		{
			MatchedInvalidChars.AppendChar(InvalidChar);
			AlreadyMatchedInvalidChars.Add(InvalidChar);
		}
	}

	// 是否包含非法字符
	if (MatchedInvalidChars.Len())
	{
		if (OutReason)
		{
			FFormatNamedArguments Args;
			Args.Add(TEXT("ErrorCtx"), (InErrorCtx) ? *InErrorCtx : NSLOCTEXT("Core", "NameDefaultErrorCtx", "Name"));
			Args.Add(TEXT("IllegalNameCharacters"), FText::FromString(MatchedInvalidChars));
			*OutReason = FText::Format(NSLOCTEXT("Core", "NameContainsInvalidCharacters", "{ErrorCtx} may not contain the following characters: {IllegalNameCharacters}"), Args);
		}
		return false;
	}

	return true;
}

// 
// 找到对应的入口，并将String附加到字符串后面
FStringBuilderBase& operator<<(FStringBuilderBase& Builder, FNameEntryId Id)
{
	FName::GetEntry(Id)->AppendNameToString(Builder);
	return Builder;
}

template <typename CharType, int N>
void CheckLazyName(const CharType(&Literal)[N])
{
	check(FName(Literal) == FLazyName(Literal));
	check(FLazyName(Literal) == FName(Literal));
	check(FLazyName(Literal) == FLazyName(Literal));
	check(FName(Literal) == FLazyName(Literal).Resolve());

	CharType Literal2[N];
	FMemory::Memcpy(Literal2, Literal);
	check(FLazyName(Literal) == FLazyName(Literal2));
}

static void TestNameBatch();

void FName::AutoTest()
{
#if DO_CHECK
	check(FNameHash::IsAnsiNone("None", 4) == 1);
	check(FNameHash::IsAnsiNone("none", 4) == 1);
	check(FNameHash::IsAnsiNone("NONE", 4) == 1);
	check(FNameHash::IsAnsiNone("nOnE", 4) == 1);
	check(FNameHash::IsAnsiNone("None", 5) == 0);
	check(FNameHash::IsAnsiNone(TEXT("None"), 4) == 0);
	check(FNameHash::IsAnsiNone("nono", 4) == 0);
	check(FNameHash::IsAnsiNone("enon", 4) == 0);

	const FName AutoTest_1("AutoTest_1");
	const FName autoTest_1("autoTest_1");
	const FName autoTeSt_1("autoTeSt_1");
	const FName AutoTest_2(TEXT("AutoTest_2"));
	const FName AutoTestB_2(TEXT("AutoTestB_2"));

	check(AutoTest_1 != AutoTest_2);
	check(AutoTest_1 == autoTest_1);
	check(AutoTest_1 == autoTeSt_1);

	TCHAR Buffer[FName::StringBufferSize];

#if WITH_CASE_PRESERVING_NAME
	check(!FCString::Strcmp(*AutoTest_1.ToString(), TEXT("AutoTest_1")));
	check(!FCString::Strcmp(*autoTest_1.ToString(), TEXT("autoTest_1")));
	check(!FCString::Strcmp(*autoTeSt_1.ToString(), TEXT("autoTeSt_1")));
	check(!FCString::Strcmp(*AutoTestB_2.ToString(), TEXT("AutoTestB_2")));
	
	check(FName("ABC").ToString(Buffer) == 3 &&			!FCString::Strcmp(Buffer, TEXT("ABC")));
	check(FName("abc").ToString(Buffer) == 3 &&			!FCString::Strcmp(Buffer, TEXT("abc")));
	check(FName(TEXT("abc")).ToString(Buffer) == 3 &&	!FCString::Strcmp(Buffer, TEXT("abc")));
	check(FName("ABC_0").ToString(Buffer) == 5 &&		!FCString::Strcmp(Buffer, TEXT("ABC_0")));
	check(FName("ABC_10").ToString(Buffer) == 6 &&		!FCString::Strcmp(Buffer, TEXT("ABC_10")));	
#endif

	check(autoTest_1.GetComparisonIndex() == AutoTest_2.GetComparisonIndex());
	check(autoTest_1.GetPlainNameString() == AutoTest_1.GetPlainNameString());
	check(autoTest_1.GetPlainNameString() == AutoTest_2.GetPlainNameString());
	check(*AutoTestB_2.GetPlainNameString() != *AutoTest_2.GetPlainNameString());
	check(AutoTestB_2.GetNumber() == AutoTest_2.GetNumber());
	check(autoTest_1.GetNumber() != AutoTest_2.GetNumber());

	check(FCStringAnsi::Strlen("None") == FName().GetStringLength());
	check(FCStringAnsi::Strlen("ABC") == FName("ABC").GetStringLength());
	check(FCStringAnsi::Strlen("ABC_0") == FName("ABC_0").GetStringLength());
	check(FCStringAnsi::Strlen("ABC_9") == FName("ABC_9").GetStringLength());
	check(FCStringAnsi::Strlen("ABC_10") == FName("ABC_10").GetStringLength());
	check(FCStringAnsi::Strlen("ABC_2000000000") == FName("ABC_2000000000").GetStringLength());
	check(FCStringAnsi::Strlen("ABC_4000000000") == FName("ABC_4000000000").GetStringLength());

	const FName NullName(static_cast<ANSICHAR*>(nullptr));
	check(NullName.IsNone());
	check(NullName == FName(static_cast<WIDECHAR*>(nullptr)));
	check(NullName == FName(NAME_None));
	check(NullName == FName());
	check(NullName == FName(""));
	check(NullName == FName(TEXT("")));
	check(NullName == FName("None"));
	check(NullName == FName("none"));
	check(NullName == FName("NONE"));
	check(NullName == FName(TEXT("None")));
	check(FName().ToEName());
	check(*FName().ToEName() == NAME_None);
	check(NullName.GetComparisonIndex().ToUnstableInt() == 0);

	const FName Cylinder(NAME_Cylinder);
	check(Cylinder == FName("Cylinder"));
	check(Cylinder.ToEName());
	check(*Cylinder.ToEName() == NAME_Cylinder);
	check(Cylinder.GetPlainNameString() == TEXT("Cylinder"));

	// Test numbers
	check(FName("Text_0") == FName("Text", NAME_EXTERNAL_TO_INTERNAL(0)));
	check(FName("Text_1") == FName("Text", NAME_EXTERNAL_TO_INTERNAL(1)));
	check(FName("Text_1_0") == FName("Text_1", NAME_EXTERNAL_TO_INTERNAL(0)));
	check(FName("Text_0_1") == FName("Text_0", NAME_EXTERNAL_TO_INTERNAL(1)));
	check(FName("Text_00") == FName("Text_00", NAME_NO_NUMBER_INTERNAL));
	check(FName("Text_01") == FName("Text_01", NAME_NO_NUMBER_INTERNAL));

	// Test unterminated strings
	check(FName("") == FName(0, "Unused"));
	check(FName("Used") == FName(4, "UsedUnused"));
	check(FName("Used") == FName(4, "Used"));
	check(FName("Used_0") == FName(6, "Used_01"));
	check(FName("Used_01") == FName(7, "Used_012"));
	check(FName("Used_123") == FName(8, "Used_123456"));
	check(FName("Used_123") == FName(8, "Used_123_456"));
	check(FName("Used_123") == FName(8, TEXT("Used_123456")));
	check(FName("Used_123") == FName(8, TEXT("Used_123_456")));
	check(FName("Used_2147483646") == FName(15, TEXT("Used_2147483646123")));
	check(FName("Used_2147483647") == FName(15, TEXT("Used_2147483647123")));
	check(FName("Used_2147483648") == FName(15, TEXT("Used_2147483648123")));

	// Test wide strings
	FString Wide("Wide ");
	Wide[4] = 60000;
	FName WideName(*Wide);
	check(WideName.GetPlainNameString() == Wide);
	check(FName(*Wide).GetPlainNameString() == Wide);
	check(FName(*Wide).ToString(Buffer) == 5 && !FCString::Strcmp(Buffer, *Wide));
	check(Wide.Len() == WideName.GetStringLength());
	FString WideLong = FString::ChrN(1000, 60000);
	check(FName(*WideLong).GetPlainNameString() == WideLong);


	// Check that FNAME_Find doesn't add entries
	static bool Once = true;
	if (Once)
	{
		check(FName("UniqueUnicorn!!", FNAME_Find) == FName());

		// Check that FNAME_Find can find entries
		const FName UniqueName("UniqueUnicorn!!", FNAME_Add);
		check(FName("UniqueUnicorn!!", FNAME_Find) == UniqueName);
		check(FName(TEXT("UniqueUnicorn!!"), FNAME_Find) == UniqueName);
		check(FName("UNIQUEUNICORN!!", FNAME_Find) == UniqueName);
		check(FName(TEXT("UNIQUEUNICORN!!"), FNAME_Find) == UniqueName);
		check(FName("uniqueunicorn!!", FNAME_Find) == UniqueName);

#if !FNAME_WRITE_PROTECT_PAGES
		// Check FNAME_Replace_Not_Safe_For_Threading updates casing
		check(0 != UniqueName.GetPlainNameString().Compare("UNIQUEunicorn!!", ESearchCase::CaseSensitive));
		const FName UniqueNameReplaced("UNIQUEunicorn!!", FNAME_Replace_Not_Safe_For_Threading);
		check(0 == UniqueName.GetPlainNameString().Compare("UNIQUEunicorn!!", ESearchCase::CaseSensitive));
		check(UniqueNameReplaced == UniqueName);

		// Check FNAME_Replace_Not_Safe_For_Threading works with wide string
		check(0 != UniqueName.GetPlainNameString().Compare("uniqueunicorn!!", ESearchCase::CaseSensitive));
		const FName UpdatedCasing(TEXT("uniqueunicorn!!"), FNAME_Replace_Not_Safe_For_Threading);
		check(0 == UniqueName.GetPlainNameString().Compare("uniqueunicorn!!", ESearchCase::CaseSensitive));

		// Check FNAME_Replace_Not_Safe_For_Threading adds entries that do not exist
		const FName AddedByReplace("WasAdded!!", FNAME_Replace_Not_Safe_For_Threading);
		check(FName("WasAdded!!", FNAME_Find) == AddedByReplace);
#endif
	
		Once = false;
	}

	check(NumberEqualsString(0, "0"));
	check(NumberEqualsString(11, "11"));
	check(NumberEqualsString(2147483647, "2147483647"));
	check(NumberEqualsString(4294967294, "4294967294"));

	check(!NumberEqualsString(0, "1"));
	check(!NumberEqualsString(1, "0"));
	check(!NumberEqualsString(11, "12"));
	check(!NumberEqualsString(12, "11"));
	check(!NumberEqualsString(2147483647, "2147483646"));
	check(!NumberEqualsString(2147483646, "2147483647"));

	check(StringAndNumberEqualsString("abc", 3, NAME_EXTERNAL_TO_INTERNAL(10), "abc_10"));
	check(!StringAndNumberEqualsString("aba", 3, NAME_EXTERNAL_TO_INTERNAL(10), "abc_10"));
	check(!StringAndNumberEqualsString("abc", 2, NAME_EXTERNAL_TO_INTERNAL(10), "abc_10"));
	check(!StringAndNumberEqualsString("abc", 2, NAME_EXTERNAL_TO_INTERNAL(11), "abc_10"));
	check(!StringAndNumberEqualsString("abc", 3, NAME_EXTERNAL_TO_INTERNAL(10), "aba_10"));
	check(!StringAndNumberEqualsString("abc", 3, NAME_EXTERNAL_TO_INTERNAL(10), "abc_11"));
	check(!StringAndNumberEqualsString("abc", 3, NAME_EXTERNAL_TO_INTERNAL(10), "abc_100"));

	check(StringAndNumberEqualsString("abc", 3, NAME_EXTERNAL_TO_INTERNAL(0), "abc_0"));
	check(!StringAndNumberEqualsString("abc", 3, NAME_EXTERNAL_TO_INTERNAL(0), "abc_1"));

	check(StringAndNumberEqualsString("abc", 3, NAME_NO_NUMBER_INTERNAL, "abc"));
	check(!StringAndNumberEqualsString("abc", 2, NAME_NO_NUMBER_INTERNAL, "abc"));
	check(!StringAndNumberEqualsString("abc", 3, NAME_NO_NUMBER_INTERNAL, "abcd"));
	check(!StringAndNumberEqualsString("abc", 3, NAME_NO_NUMBER_INTERNAL, "abc_0"));
	check(!StringAndNumberEqualsString("abc", 3, NAME_NO_NUMBER_INTERNAL, "abc_"));

	TArray<FName> Names;
	Names.Add("FooB");
	Names.Add("FooABCD");
	Names.Add("FooABC");
	Names.Add("FooAB");
	Names.Add("FooA");
	Names.Add("FooC");
	const WIDECHAR FooWide[] = {'F', 'o', 'o', (WIDECHAR)2000, '\0'};
	Names.Add(FooWide);
	Algo::Sort(Names, FNameLexicalLess()); 

	check(Names[0] == "FooA");
	check(Names[1] == "FooAB");
	check(Names[2] == "FooABC");
	check(Names[3] == "FooABCD");
	check(Names[4] == "FooB");
	check(Names[5] == "FooC");
	check(Names[6] == FooWide);

	
	CheckLazyName("Hej");
	CheckLazyName(TEXT("Hej"));
	CheckLazyName("Hej_0");
	CheckLazyName("Hej_00");
	CheckLazyName("Hej_1");
	CheckLazyName("Hej_01");
	CheckLazyName("Hej_-1");
	CheckLazyName("Hej__0");
	CheckLazyName("Hej_2147483647");
	CheckLazyName("Hej_123");
	CheckLazyName("None");
	CheckLazyName("none");
	CheckLazyName("None_0");
	CheckLazyName("None_1");

	TestNameBatch();

#if 0
	// Check hash table growth still yields the same unique FName ids
	static int32 OverflowAtLeastTwiceCount = 4 * FNamePoolInitialSlotsPerShard * FNamePoolShards;
	TArray<FNameEntryId> Ids;
	for (int I = 0; I < OverflowAtLeastTwiceCount; ++I)
	{
		FNameEntryId Id = FName(*FString::Printf(TEXT("%d"), I)).GetComparisonIndex();
		Ids.Add(Id);
	}

	for (int I = 0; I < OverflowAtLeastTwiceCount; ++I)
	{
		FNameEntryId Id = FName(*FString::Printf(TEXT("%d"), I)).GetComparisonIndex();
		FNameEntryId OldId = Ids[I];

		while (Id != OldId)
		{
			Id = FName(*FString::Printf(TEXT("%d"), I)).GetComparisonIndex();
		}
		check(Id == OldId);
	}
#endif
#endif // DO_CHECK
}


/*-----------------------------------------------------------------------------
	FNameEntry implementation.
-----------------------------------------------------------------------------*/

void FNameEntry::Write( FArchive& Ar ) const
{
	// This path should be unused - since FNameEntry structs are allocated with a dynamic size, we can only save them. Use FNameEntrySerialized to read them back into an intermediate buffer.
	checkf(!Ar.IsLoading(), TEXT("FNameEntry does not support reading from an archive. Serialize into a FNameEntrySerialized and construct a FNameEntry from that."));

	// Convert to our serialized type
	FNameEntrySerialized EntrySerialized(*this);
	Ar << EntrySerialized;
}

static_assert(PLATFORM_LITTLE_ENDIAN, "FNameEntrySerialized serialization needs updating to support big-endian platforms!");

FArchive& operator<<(FArchive& Ar, FNameEntrySerialized& E)
{
	if (Ar.IsLoading())
	{
		// for optimization reasons, we want to keep pure Ansi strings as Ansi for initializing the name entry
		// (and later the FName) to stop copying in and out of TCHARs
		int32 StringLen;
		Ar << StringLen;

		// negative stringlen means it's a wide string
		if (StringLen < 0)
		{
			// If StringLen cannot be negated due to integer overflow, Ar is corrupted.
			if (StringLen == MIN_int32)
			{
				Ar.SetCriticalError();
				UE_LOG(LogUnrealNames, Error, TEXT("Archive is corrupted"));
				return Ar;
			}

			StringLen = -StringLen;

			int64 MaxSerializeSize = Ar.GetMaxSerializeSize();
			// Protect against network packets allocating too much memory
			if ((MaxSerializeSize > 0) && (StringLen > MaxSerializeSize))
			{
				Ar.SetCriticalError();
				UE_LOG(LogUnrealNames, Error, TEXT("String is too large"));
				return Ar;
			}

			// mark the name will be wide
			E.bIsWide = true;

			// get the pointer to the wide array 
			WIDECHAR* WideName = const_cast<WIDECHAR*>(E.GetWideName());

			// read in the UCS2CHAR string
			auto Sink = StringMemoryPassthru<UCS2CHAR>(WideName, StringLen, StringLen);
			Ar.Serialize(Sink.Get(), StringLen * sizeof(UCS2CHAR));
			Sink.Apply();

#if PLATFORM_TCHAR_IS_4_BYTES
			// Inline combine any surrogate pairs in the data when loading into a UTF-32 string
			StringLen = StringConv::InlineCombineSurrogates_Buffer(WideName, StringLen);
#endif	// PLATFORM_TCHAR_IS_4_BYTES
		}
		else
		{
			int64 MaxSerializeSize = Ar.GetMaxSerializeSize();
			// Protect against network packets allocating too much memory
			if ((MaxSerializeSize > 0) && (StringLen > MaxSerializeSize))
			{
				Ar.SetCriticalError();
				UE_LOG(LogUnrealNames, Error, TEXT("String is too large"));
				return Ar;
			}

			// mark the name will be ansi
			E.bIsWide = false;

			// ansi strings can go right into the AnsiBuffer
			ANSICHAR* AnsiName = const_cast<ANSICHAR*>(E.GetAnsiName());
			Ar.Serialize(AnsiName, StringLen);
		}

		uint16 DummyHashes[2];
		uint32 SkipPastHashBytes = (Ar.UE4Ver() >= VER_UE4_NAME_HASHES_SERIALIZED) * sizeof(DummyHashes);
		Ar.Serialize(&DummyHashes, SkipPastHashBytes);
	}
	else
	{
		// These hashes are no longer used. They're only kept to maintain serialization format.
		// Please remove them if you ever change serialization format.
		FString Str = E.GetPlainNameString();
		Ar << Str;
		Ar << E.NonCasePreservingHash;
		Ar << E.CasePreservingHash;
	}

	return Ar;
}

// 从一个可用的Name中得到名字入口
FNameEntryId FNameEntryId::FromValidEName(EName Ename)
{
	return GetNamePool().Find(Ename);
}

// 从系统中拆除，并释放所有的内存
void FName::TearDown()
{
	check(IsInGameThread());

	if (bNamePoolInitialized)
	{
		GetNamePoolPostInit().~FNamePool();
		bNamePoolInitialized = false;
	}
}

FName FLazyName::Resolve() const
{
	// Make a stack copy to ensure thread-safety
	FLiteralOrName Copy = Either;

	if (Copy.IsName())
	{
		FNameEntryId Id = Copy.AsName();
		return FName(Id, Id, Number);
	}

	// Resolve to FName but throw away the number part
	FNameEntryId Id = bLiteralIsWide ? FName(Copy.AsWideLiteral()).GetComparisonIndex()
										: FName(Copy.AsAnsiLiteral()).GetComparisonIndex();

	// Deliberately unsynchronized write of word-sized int, ok if multiple threads resolve same lazy name
	Either = FLiteralOrName(Id);

	return FName(Id, Id, Number);		
}

uint32 FLazyName::ParseNumber(const ANSICHAR* Str, int32 Len)
{
	return FNameHelper::ParseNumber(Str, Len);
}

uint32 FLazyName::ParseNumber(const WIDECHAR* Str, int32 Len)
{
	return FNameHelper::ParseNumber(Str, Len);
}

bool operator==(const FLazyName& A, const FLazyName& B)
{
	// If we have started creating FNames we might as well resolve and cache both lazy names
	if (A.Either.IsName() || B.Either.IsName())
	{
		return A.Resolve() == B.Resolve();
	}

	// Literal pointer comparison, can ignore width
	if (A.Either.AsAnsiLiteral() == B.Either.AsAnsiLiteral())
	{
		return true;
	}

	if (A.bLiteralIsWide)
	{
		return B.bLiteralIsWide ? FPlatformString::Stricmp(A.Either.AsWideLiteral(), B.Either.AsWideLiteral()) == 0
								: FPlatformString::Stricmp(A.Either.AsWideLiteral(), B.Either.AsAnsiLiteral()) == 0;
	}
	else
	{
		return B.bLiteralIsWide ? FPlatformString::Stricmp(A.Either.AsAnsiLiteral(), B.Either.AsWideLiteral()) == 0
								: FPlatformString::Stricmp(A.Either.AsAnsiLiteral(), B.Either.AsAnsiLiteral()) == 0;	
	}
}

/*-----------------------------------------------------------------------------
	FName batch serialization
-----------------------------------------------------------------------------*/

/**
 * FNameStringView sibling with UTF16 Little-Endian wide strings instead of WIDECHAR 
 *
 * View into serialized data instead of how it will be stored in memory once loaded.
 */
struct FNameSerializedView
{
	FNameSerializedView(const ANSICHAR* InStr, uint32 InLen)
	: Ansi(InStr)
	, Len(InLen)
	, bIsUtf16(false)
	{}
	
	FNameSerializedView(const UTF16CHAR* InStr, uint32 InLen)
	: Utf16(InStr)
	, Len(InLen)
	, bIsUtf16(true)
	{}

	FNameSerializedView(const uint8* InData, uint32 InLen, bool bInUtf16)
	: Data(InData)
	, Len(InLen)
	, bIsUtf16(bInUtf16)
	{}

	union
	{
		const uint8* Data;
		const ANSICHAR* Ansi;
		const UTF16CHAR* Utf16;
	};

	uint32 Len;
	bool bIsUtf16;
};

static uint8* AddUninitializedBytes(TArray<uint8>& Out, uint32 Bytes)
{
	uint32 OldNum = Out.AddUninitialized(Bytes);
	return Out.GetData() + OldNum;
}

template<typename T>
static T* AddUninitializedElements(TArray<uint8>& Out, uint32 Num)
{
	check(Out.Num() %  alignof(T) == 0);
	return reinterpret_cast<T*>(AddUninitializedBytes(Out, Num * sizeof(T)));
}

template<typename T>
static void AddValue(TArray<uint8>& Out, T Value)
{
	*AddUninitializedElements<T>(Out, 1) = Value;
}

template<typename T>
static void AlignTo(TArray<uint8>& Out)
{
	if (uint32 UnpaddedBytes = Out.Num() % sizeof(T))
	{
		Out.AddZeroed(sizeof(T) - UnpaddedBytes);
	}
}

static uint32 GetRequiredUtf16Padding(const uint8* Ptr)
{
	return UPTRINT(Ptr) & 1u;
}

struct FSerializedNameHeader
{
	FSerializedNameHeader(uint32 Len, bool bIsUtf16)
	{
		static_assert(NAME_SIZE < 0x8000u, "");
		check(Len <= NAME_SIZE);

		Data[0] = uint8(bIsUtf16) << 7 | static_cast<uint8>(Len >> 8);
		Data[1] = static_cast<uint8>(Len);
	}

	uint8 IsUtf16() const
	{
		return Data[0] & 0x80u;
	}

	uint32 Len() const
	{
		return ((Data[0] & 0x7Fu) << 8) + Data[1];
	}

	uint8 Data[2];
};

FNameSerializedView LoadNameHeader(const uint8*& InOutIt)
{
	const FSerializedNameHeader& Header = *reinterpret_cast<const FSerializedNameHeader*>(InOutIt);
	const uint8* NameData = InOutIt + sizeof(FSerializedNameHeader);
	const uint32 Len = Header.Len();

	if (Header.IsUtf16())
	{
		NameData += GetRequiredUtf16Padding(NameData);
		InOutIt = NameData + Len * sizeof(UTF16CHAR);
		return FNameSerializedView(NameData, Len, /* UTF16 */ true);
	}
	else
	{
		InOutIt = NameData + Len * sizeof(ANSICHAR);
		return FNameSerializedView(NameData, Len, /* UTF16 */ false);
	}
}

#if ALLOW_NAME_BATCH_SAVING

static FNameSerializedView SaveAnsiName(TArray<uint8>& Out, const ANSICHAR* Src, uint32 Len)
{
	ANSICHAR* Dst = AddUninitializedElements<ANSICHAR>(Out, Len);
	FMemory::Memcpy(Dst, Src, Len * sizeof(ANSICHAR));

	return FNameSerializedView(Dst, Len);
}

static FNameSerializedView SaveUtf16Name(TArray<uint8>& Out, const WIDECHAR* Src, uint32 Len)
{
	// Align to UTF16CHAR after header
	AlignTo<UTF16CHAR>(Out);
	
#if !PLATFORM_LITTLE_ENDIAN
	#error TODO: Implement saving code units as Little-Endian on Big-Endian platforms
#endif

	// This is a no-op when sizeof(UTF16CHAR) == sizeof(WIDECHAR), which it usually is
	FTCHARToUTF16 Utf16String(Src, Len);

	UTF16CHAR* Dst = AddUninitializedElements<UTF16CHAR>(Out, Utf16String.Length());
	FMemory::Memcpy(Dst, Utf16String.Get(), Utf16String.Length() * sizeof(UTF16CHAR));

	return FNameSerializedView(Dst, Len);
}

static FNameSerializedView SaveAnsiOrUtf16Name(TArray<uint8>& Out, FNameStringView Name)
{
	void* HeaderData = AddUninitializedBytes(Out, sizeof(FSerializedNameHeader));
	new (HeaderData) FSerializedNameHeader(Name.Len, Name.bIsWide);

	if (Name.bIsWide)
	{
		return SaveUtf16Name(Out, Name.Wide, Name.Len);
	}
	else
	{
		return SaveAnsiName(Out, Name.Ansi, Name.Len);
	}
}

void SaveNameBatch(TArrayView<const FNameEntryId> Names, TArray<uint8>& OutNameData, TArray<uint8>& OutHashData)
{
	OutNameData.Empty(/* average bytes per name guesstimate */ 40 * Names.Num());
	OutHashData.Empty((/* hash version */ 1 + Names.Num()) * sizeof(uint64));

	// Save hash algorithm version
	AddValue(OutHashData, INTEL_ORDER64(FNameHash::AlgorithmId));

	// Save names and hashes
	FNameBuffer CustomDecodeBuffer;
	for (FNameEntryId EntryId : Names)
	{
		FNameStringView InMemoryName = GetNamePoolPostInit().Resolve(EntryId).MakeView(CustomDecodeBuffer);
		FNameSerializedView SavedName = SaveAnsiOrUtf16Name(OutNameData, InMemoryName);

		uint64 LowerHash = SavedName.bIsUtf16 ? FNameHash::GenerateLowerCaseHash(SavedName.Utf16, SavedName.Len)
											  : FNameHash::GenerateLowerCaseHash(SavedName.Ansi,  SavedName.Len);

		AddValue(OutHashData, INTEL_ORDER64(LowerHash));
	}
}

#endif // WITH_EDITOR

FORCENOINLINE void ReserveNameBatch(uint32 NameDataBytes, uint32 HashDataBytes)
{
	uint32 NumEntries = HashDataBytes / sizeof(uint64) - 1;
	// Add 20% slack to reduce probing costs
	auto AddSlack = [](uint64 In){ return static_cast<uint32>(In * 6 / 5); };
	GetNamePoolPostInit().Reserve(AddSlack(NameDataBytes), AddSlack(NumEntries));
}

static FNameEntryId BatchLoadNameWithoutHash(const UTF16CHAR* Str, uint32 Len)
{
	WIDECHAR Temp[NAME_SIZE];
	for (uint32 Idx = 0; Idx < Len; ++Idx)
	{
		Temp[Idx] = INTEL_ORDER16(Str[Idx]);
	}
	
#if PLATFORM_TCHAR_IS_4_BYTES
	// Inline combine any surrogate pairs in the data when loading into a UTF-32 string
	Len = StringConv::InlineCombineSurrogates_Buffer(Temp, Len);
#endif

	FNameStringView Name(Temp, Len);
	FNameHash Hash = HashName<ENameCase::IgnoreCase>(Name);
	return GetNamePoolPostInit().BatchStore(FNameComparisonValue(Name, Hash));
}

static FNameEntryId BatchLoadNameWithoutHash(const ANSICHAR* Str, uint32 Len)
{
	FNameStringView Name(Str, Len);
	FNameHash Hash = HashName<ENameCase::IgnoreCase>(Name);
	return GetNamePoolPostInit().BatchStore(FNameComparisonValue(Name, Hash));
}

static FNameEntryId BatchLoadNameWithoutHash(const FNameSerializedView& Name)
{
	return Name.bIsUtf16 ? BatchLoadNameWithoutHash(Name.Utf16, Name.Len)
						 : BatchLoadNameWithoutHash(Name.Ansi, Name.Len);
}

template<typename CharType>
FNameEntryId BatchLoadNameWithHash(const CharType* Str, uint32 Len, uint64 InHash)
{
	FNameStringView Name(Str, Len);
	FNameHash Hash(Str, Len, InHash);
	checkfSlow(Hash == HashName<ENameCase::IgnoreCase>(Name), TEXT("Precalculated hash was wrong"));
	return GetNamePoolPostInit().BatchStore(FNameComparisonValue(Name, Hash));
}

static FNameEntryId BatchLoadNameWithHash(const FNameSerializedView& InName, uint64 InHash)
{
	if (InName.bIsUtf16)
	{
		// Wide names and hashes are currently stored as UTF16 Little-Endian 
		// regardless of target architecture. 
#if PLATFORM_LITTLE_ENDIAN
		if (sizeof(UTF16CHAR) == sizeof(WIDECHAR))
		{
			return BatchLoadNameWithHash(reinterpret_cast<const WIDECHAR*>(InName.Utf16), InName.Len, InHash);
		}
#endif

		return BatchLoadNameWithoutHash(InName.Utf16, InName.Len);
	}
	else
	{
		return BatchLoadNameWithHash(InName.Ansi, InName.Len, InHash);
	}
}

void LoadNameBatch(TArray<FNameEntryId>& OutNames, TArrayView<const uint8> NameData, TArrayView<const uint8> HashData)
{
	check(IsAligned(NameData.GetData(), sizeof(uint64)));
	check(IsAligned(HashData.GetData(), sizeof(uint64)));
	check(IsAligned(HashData.Num(), sizeof(uint64)));
	check(HashData.Num() > 0);

	const uint8* NameIt = NameData.GetData();
	const uint8* NameEnd = NameData.GetData() + NameData.Num();

	const uint64* HashDataIt = reinterpret_cast<const uint64*>(HashData.GetData());
	uint64 HashVersion = INTEL_ORDER64(HashDataIt[0]);
	TArrayView<const uint64> Hashes = MakeArrayView(HashDataIt + 1, HashData.Num() / sizeof(uint64) - 1);

	OutNames.Empty(Hashes.Num());

	GetNamePoolPostInit().BatchLock();

	if (HashVersion == FNameHash::AlgorithmId)
	{
		for (uint64 Hash : Hashes)
		{
			check(NameIt < NameEnd);
			FNameSerializedView Name = LoadNameHeader(/* in-out */ NameIt);
			OutNames.Add(BatchLoadNameWithHash(Name, INTEL_ORDER64(Hash)));
		}
	}
	else
	{
		while (NameIt < NameEnd)
		{
			FNameSerializedView Name = LoadNameHeader(/* in-out */ NameIt);
			OutNames.Add(BatchLoadNameWithoutHash(Name));
		}
	
	}

	GetNamePoolPostInit().BatchUnlock();

	check(NameIt == NameEnd);
}

#if 0 && ALLOW_NAME_BATCH_SAVING  

FORCENOINLINE void PerfTestLoadNameBatch(TArray<FNameEntryId>& OutNames, TArrayView<const uint8> NameData, TArrayView<const uint8> HashData)
{
	LoadNameBatch(OutNames, NameData, HashData);
}

static bool WithinBlock(uint8* BlockBegin, const FNameEntry* Entry)
{
	return UPTRINT(Entry) >= UPTRINT(BlockBegin) && UPTRINT(Entry) < UPTRINT(BlockBegin) + FNameEntryAllocator::BlockSizeBytes;
}

#include "GenericPlatform\GenericPlatformFile.h"

static void WriteBlobFile(const TCHAR* FileName, const TArray<uint8>& Blob)
{
	TUniquePtr<IFileHandle> FileHandle(IPlatformFile::GetPlatformPhysical().OpenWrite(FileName));
	FileHandle->Write(Blob.GetData(), Blob.Num());
}

static TArray<uint8> ReadBlobFile(const TCHAR* FileName)
{
	TArray<uint8> Out;
	TUniquePtr<IFileHandle> FileHandle(IPlatformFile::GetPlatformPhysical().OpenRead(FileName));
	if (FileHandle)
	{
		Out.AddUninitialized(FileHandle->Size());
		FileHandle->Read(Out.GetData(), Out.Num());
	}

	return Out;
}

CORE_API int SaveNameBatchTestFiles();
CORE_API int LoadNameBatchTestFiles();

int SaveNameBatchTestFiles()
{
	uint8** Blocks = GetNamePool().GetBlocksForDebugVisualizer();
	uint32 BlockIdx = 0;
	TArray<FNameEntryId> NameEntries;
	for (const FNameEntry* Entry : FName::DebugDump())
	{
		BlockIdx += !WithinBlock(Blocks[BlockIdx], Entry);
		check(WithinBlock(Blocks[BlockIdx], Entry));

		FNameEntryHandle Handle(BlockIdx, (UPTRINT(Entry) - UPTRINT(Blocks[BlockIdx]))/FNameEntryAllocator::Stride);
		NameEntries.Add(Handle);
	}

	TArray<uint8> NameData;
	TArray<uint8> HashData;
	SaveNameBatch(MakeArrayView(NameEntries), NameData, HashData);

	WriteBlobFile(TEXT("TestNameBatch.Names"), NameData);
	WriteBlobFile(TEXT("TestNameBatch.Hashes"), HashData);

	return NameEntries.Num();
}

int LoadNameBatchTestFiles()
{
	TArray<uint8> NameData = ReadBlobFile(TEXT("TestNameBatch.Names"));
	TArray<uint8> HashData = ReadBlobFile(TEXT("TestNameBatch.Hashes"));

	TArray<FNameEntryId> NameEntries;
	if (HashData.Num())
	{
		ReserveNameBatch(NameData.Num(), HashData.Num());
		PerfTestLoadNameBatch(NameEntries, MakeArrayView(NameData), MakeArrayView(HashData));
	}
	return NameEntries.Num();
}

#endif

static void TestNameBatch()
{
#if ALLOW_NAME_BATCH_SAVING

	TArray<FNameEntryId> Names;
	TArray<uint8> NameData;
	TArray<uint8> HashData;

	// Test empty batch
	SaveNameBatch(MakeArrayView(Names), NameData, HashData);
	check(NameData.Num() == 0);
	LoadNameBatch(Names, MakeArrayView(NameData), MakeArrayView(HashData));
	check(Names.Num() == 0);

	// Test empty / "None" name and another EName
	Names.Add(FName().GetComparisonIndex());
	Names.Add(FName(NAME_Box).GetComparisonIndex());

	// Test long strings
	FString MaxLengthAnsi;
	MaxLengthAnsi.Reserve(NAME_SIZE);
	while (MaxLengthAnsi.Len() < NAME_SIZE)
	{
		MaxLengthAnsi.Append("0123456789ABCDEF");
	}
	MaxLengthAnsi = MaxLengthAnsi.Left(NAME_SIZE - 1);

	FString MaxLengthWide = MaxLengthAnsi;
	MaxLengthWide[200] = 500;

	for (const FString& MaxLength : {MaxLengthAnsi, MaxLengthWide})
	{
		Names.Add(FName(*MaxLength).GetComparisonIndex());
		Names.Add(FName(*MaxLength + NAME_SIZE - 255).GetComparisonIndex());
		Names.Add(FName(*MaxLength + NAME_SIZE - 256).GetComparisonIndex());
		Names.Add(FName(*MaxLength + NAME_SIZE - 257).GetComparisonIndex());
	}

	// Test UTF-16 alignment
	FString Wide("Wide ");
	Wide[4] = 60000;

	Names.Add(FName(*Wide).GetComparisonIndex());
	Names.Add(FName("odd").GetComparisonIndex());
	Names.Add(FName(*Wide).GetComparisonIndex());
	Names.Add(FName("even").GetComparisonIndex());
	Names.Add(FName(*Wide).GetComparisonIndex());

	// Roundtrip names
	SaveNameBatch(MakeArrayView(Names), NameData, HashData);
	check(NameData.Num() > 0);
	TArray<FNameEntryId> LoadedNames;
	LoadNameBatch(LoadedNames, MakeArrayView(NameData), MakeArrayView(HashData));
	check(LoadedNames == Names);

	// Test changing hash version
	HashData[0] = 0xba;
	HashData[1] = 0xad;
	LoadNameBatch(LoadedNames, MakeArrayView(NameData), MakeArrayView(HashData));
	check(LoadedNames == Names);

	// Test determinism
	TArray<uint8> NameData2;
	TArray<uint8> HashData2;

	auto ClearAndReserveMemoryWithBytePattern = 
		[](TArray<uint8>& Out, uint8 Pattern, uint32 Num)
	{
		Out.Init(Pattern, Num);
		Out.Empty(Out.Max());
	};
	
	ClearAndReserveMemoryWithBytePattern(NameData2, 0xaa, NameData.Num());
	ClearAndReserveMemoryWithBytePattern(HashData2, 0xaa, HashData.Num());
	ClearAndReserveMemoryWithBytePattern(NameData,	0xbb, NameData.Num());
	ClearAndReserveMemoryWithBytePattern(HashData,	0xbb, HashData.Num());
	
	SaveNameBatch(MakeArrayView(Names), NameData, HashData);
	SaveNameBatch(MakeArrayView(Names), NameData2, HashData2);
	
	check(NameData == NameData2);
	check(HashData == HashData2);

#endif // ALLOW_NAME_BATCH_SAVING
}

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST

#include "Containers/StackTracker.h"
static TAutoConsoleVariable<int32> CVarLogGameThreadFNameChurn(
	TEXT("LogGameThreadFNameChurn.Enable"),
	0,
	TEXT("If > 0, then collect sample game thread fname create, periodically print a report of the worst offenders."));

static TAutoConsoleVariable<int32> CVarLogGameThreadFNameChurn_PrintFrequency(
	TEXT("LogGameThreadFNameChurn.PrintFrequency"),
	300,
	TEXT("Number of frames between churn reports."));

static TAutoConsoleVariable<int32> CVarLogGameThreadFNameChurn_Threshhold(
	TEXT("LogGameThreadFNameChurn.Threshhold"),
	10,
	TEXT("Minimum average number of fname creations per frame to include in the report."));

static TAutoConsoleVariable<int32> CVarLogGameThreadFNameChurn_SampleFrequency(
	TEXT("LogGameThreadFNameChurn.SampleFrequency"),
	1,
	TEXT("Number of fname creates per sample. This is used to prevent churn sampling from slowing the game down too much."));

static TAutoConsoleVariable<int32> CVarLogGameThreadFNameChurn_StackIgnore(
	TEXT("LogGameThreadFNameChurn.StackIgnore"),
	4,
	TEXT("Number of items to discard from the top of a stack frame."));

static TAutoConsoleVariable<int32> CVarLogGameThreadFNameChurn_RemoveAliases(
	TEXT("LogGameThreadFNameChurn.RemoveAliases"),
	1,
	TEXT("If > 0 then remove aliases from the counting process. This essentialy merges addresses that have the same human readable string. It is slower."));

static TAutoConsoleVariable<int32> CVarLogGameThreadFNameChurn_StackLen(
	TEXT("LogGameThreadFNameChurn.StackLen"),
	3,
	TEXT("Maximum number of stack frame items to keep. This improves aggregation because calls that originate from multiple places but end up in the same place will be accounted together."));


struct FSampleFNameChurn
{
	FStackTracker GGameThreadFNameChurnTracker;
	bool bEnabled;
	int32 CountDown;
	uint64 DumpFrame;

	FSampleFNameChurn()
		: bEnabled(false)
		, CountDown(MAX_int32)
		, DumpFrame(0)
	{
	}

	void NameCreationHook()
	{
		bool bNewEnabled = CVarLogGameThreadFNameChurn.GetValueOnGameThread() > 0;
		if (bNewEnabled != bEnabled)
		{
			check(IsInGameThread());
			bEnabled = bNewEnabled;
			if (bEnabled)
			{
				CountDown = CVarLogGameThreadFNameChurn_SampleFrequency.GetValueOnGameThread();
				DumpFrame = GFrameCounter + CVarLogGameThreadFNameChurn_PrintFrequency.GetValueOnGameThread();
				GGameThreadFNameChurnTracker.ResetTracking();
				GGameThreadFNameChurnTracker.ToggleTracking(true, true);
			}
			else
			{
				GGameThreadFNameChurnTracker.ToggleTracking(false, true);
				DumpFrame = 0;
				GGameThreadFNameChurnTracker.ResetTracking();
			}
		}
		else if (bEnabled)
		{
			check(IsInGameThread());
			check(DumpFrame);
			if (--CountDown <= 0)
			{
				CountDown = CVarLogGameThreadFNameChurn_SampleFrequency.GetValueOnGameThread();
				CollectSample();
				if (GFrameCounter > DumpFrame)
				{
					PrintResultsAndReset();
				}
			}
		}
	}

	void CollectSample()
	{
		check(IsInGameThread());
		GGameThreadFNameChurnTracker.CaptureStackTrace(CVarLogGameThreadFNameChurn_StackIgnore.GetValueOnGameThread(), nullptr, CVarLogGameThreadFNameChurn_StackLen.GetValueOnGameThread(), CVarLogGameThreadFNameChurn_RemoveAliases.GetValueOnGameThread() > 0);
	}
	void PrintResultsAndReset()
	{
		DumpFrame = GFrameCounter + CVarLogGameThreadFNameChurn_PrintFrequency.GetValueOnGameThread();
		FOutputDeviceRedirector* Log = FOutputDeviceRedirector::Get();
		float SampleAndFrameCorrection = float(CVarLogGameThreadFNameChurn_SampleFrequency.GetValueOnGameThread()) / float(CVarLogGameThreadFNameChurn_PrintFrequency.GetValueOnGameThread());
		GGameThreadFNameChurnTracker.DumpStackTraces(CVarLogGameThreadFNameChurn_Threshhold.GetValueOnGameThread(), *Log, SampleAndFrameCorrection);
		GGameThreadFNameChurnTracker.ResetTracking();
	}
};

FSampleFNameChurn GGameThreadFNameChurnTracker;

void CallNameCreationHook()
{
	if (GIsRunning && IsInGameThread())
	{
		GGameThreadFNameChurnTracker.NameCreationHook();
	}
}

#endif

uint8** FNameDebugVisualizer::GetBlocks()
{
	static_assert(EntryStride == FNameEntryAllocator::Stride,	"Natvis constants out of sync with actual constants");
	static_assert(BlockBits == FNameMaxBlockBits,				"Natvis constants out of sync with actual constants");
	static_assert(OffsetBits == FNameBlockOffsetBits,			"Natvis constants out of sync with actual constants");

	return ((FNamePool*)(NamePoolData))->GetBlocksForDebugVisualizer();
}

// 将FScriptName转换为String，先将FScriptName转换成FName，然后FName转换为FString
FString FScriptName::ToString() const
{
	return ScriptNameToName(*this).ToString();
}

// 将FName序列化到Writer中
void Freeze::IntrinsicWriteMemoryImage(FMemoryImageWriter& Writer, const FName& Object, const FTypeLayoutDesc&)
{
	Writer.WriteFName(Object);
}

uint32 Freeze::IntrinsicAppendHash(const FName* DummyObject, const FTypeLayoutDesc& TypeDesc, const FPlatformTypeLayoutParameters& LayoutParams, FSHA1& Hasher)
{
	const uint32 SizeFromFields = LayoutParams.WithCasePreservingFName() ? sizeof(FScriptName) : sizeof(FMinimalName);
	return Freeze::AppendHashForNameAndSize(TypeDesc.Name, SizeFromFields, Hasher);
}

// 将FName序列化到Writer中
void Freeze::IntrinsicWriteMemoryImage(FMemoryImageWriter& Writer, const FMinimalName& Object, const FTypeLayoutDesc&)
{
	Writer.WriteFMinimalName(Object);
}

// 将FName序列化到Writer中
void Freeze::IntrinsicWriteMemoryImage(FMemoryImageWriter& Writer, const FScriptName& Object, const FTypeLayoutDesc&)
{
	Writer.WriteFScriptName(Object);
}

PRAGMA_ENABLE_UNSAFE_TYPECAST_WARNINGS
