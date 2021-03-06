// Copyright Epic Games, Inc. All Rights Reserved.

#include "DNAUtils.h"

#include "CoreMinimal.h"
#include "DNAReaderAdapter.h"
#include "FMemoryResource.h"
#include "RigLogicMemoryStream.h"

#include "riglogic/RigLogic.h"

static TSharedPtr<IDNAReader> ReadDNAStream(rl4::ScopedPtr<dna::StreamReader> DNAStreamReader)
{
	DNAStreamReader->read();
	if (!rl4::Status::isOk())
	{
		UE_LOG(LogDNAReader, Error, TEXT("%s"), ANSI_TO_TCHAR(rl4::Status::get().message));
		return nullptr;
	}
	return MakeShared<FDNAReader<dna::StreamReader>>(DNAStreamReader.release());
}

TSharedPtr<IDNAReader> ReadDNAFromFile(const FString& Path, EDNADataLayer Layer, uint16_t MaxLOD)
{
	LLM_SCOPE(ELLMTag::SkeletalMesh);
	auto DNAFileStream = rl4::makeScoped<rl4::FileStream>(TCHAR_TO_ANSI(*Path), rl4::FileStream::AccessMode::Read, rl4::FileStream::OpenMode::Binary, FMemoryResource::Instance());
	auto DNAStreamReader = rl4::makeScoped<dna::StreamReader>(DNAFileStream.get(), static_cast<dna::DataLayer>(Layer), MaxLOD, FMemoryResource::Instance());
	return ReadDNAStream(MoveTemp(DNAStreamReader));
}

TSharedPtr<IDNAReader> ReadDNAFromFile(const FString& Path, EDNADataLayer Layer, TArrayView<uint16_t> LODs)
{
	LLM_SCOPE(ELLMTag::SkeletalMesh);
	auto DNAFileStream = rl4::makeScoped<rl4::FileStream>(TCHAR_TO_ANSI(*Path), rl4::FileStream::AccessMode::Read, rl4::FileStream::OpenMode::Binary, FMemoryResource::Instance());
	auto DNAStreamReader = rl4::makeScoped<dna::StreamReader>(DNAFileStream.get(), static_cast<dna::DataLayer>(Layer), LODs.GetData(), static_cast<uint16>(LODs.Num()), FMemoryResource::Instance());
	return ReadDNAStream(MoveTemp(DNAStreamReader));
}

TSharedPtr<IDNAReader> ReadDNAFromBuffer(TArray<uint8>* DNABuffer, EDNADataLayer Layer, uint16_t MaxLOD)
{
	LLM_SCOPE(ELLMTag::SkeletalMesh);
	FRigLogicMemoryStream DNAMemoryStream(DNABuffer);
	auto DNAStreamReader = rl4::makeScoped<dna::StreamReader>(&DNAMemoryStream, static_cast<dna::DataLayer>(Layer), MaxLOD, FMemoryResource::Instance());
	return ReadDNAStream(MoveTemp(DNAStreamReader));
}

TSharedPtr<IDNAReader> ReadDNAFromBuffer(TArray<uint8>* DNABuffer, EDNADataLayer Layer, TArrayView<uint16_t> LODs)
{
	LLM_SCOPE(ELLMTag::SkeletalMesh);
	FRigLogicMemoryStream DNAMemoryStream(DNABuffer);
	auto DNAStreamReader = rl4::makeScoped<dna::StreamReader>(&DNAMemoryStream, static_cast<dna::DataLayer>(Layer), LODs.GetData(), static_cast<uint16>(LODs.Num()), FMemoryResource::Instance());
	return ReadDNAStream(MoveTemp(DNAStreamReader));
}

void WriteDNAToFile(const IDNAReader* Reader, EDNADataLayer Layer, const FString& Path)
{
	LLM_SCOPE(ELLMTag::SkeletalMesh);
	auto DNAFileStream = rl4::makeScoped<rl4::FileStream>(TCHAR_TO_ANSI(*Path), rl4::FileStream::AccessMode::Write, rl4::FileStream::OpenMode::Binary, FMemoryResource::Instance());
	auto DNAStreamWriter = rl4::makeScoped<dna::StreamWriter>(DNAFileStream.get(), FMemoryResource::Instance());
	DNAStreamWriter->setFrom(Reader->Unwrap(), static_cast<dna::DataLayer>(Layer), FMemoryResource::Instance());
	DNAStreamWriter->write();
}
