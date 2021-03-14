// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithFacadeElement.h"


FDatasmithFacadeElement::ECoordinateSystemType FDatasmithFacadeElement::WorldCoordinateSystemType = FDatasmithFacadeElement::ECoordinateSystemType::LeftHandedZup;

float FDatasmithFacadeElement::WorldUnitScale = 1.0;

FDatasmithFacadeElement::ConvertVertexMethod FDatasmithFacadeElement::ConvertPosition  = nullptr;
FDatasmithFacadeElement::ConvertVertexMethod FDatasmithFacadeElement::ConvertDirection = nullptr;

void FDatasmithFacadeElement::SetCoordinateSystemType(
	ECoordinateSystemType InWorldCoordinateSystemType
)
{
	WorldCoordinateSystemType = InWorldCoordinateSystemType;

	switch (WorldCoordinateSystemType)
	{
		case ECoordinateSystemType::LeftHandedYup:
		{
			ConvertPosition  = ConvertPositionLeftHandedYup;
			ConvertDirection = ConvertDirectionLeftHandedYup;
			break;
		}
		case ECoordinateSystemType::LeftHandedZup:
		{
			ConvertPosition  = ConvertPositionLeftHandedZup;
			ConvertDirection = ConvertDirectionLeftHandedZup;
			break;
		}
		case ECoordinateSystemType::RightHandedZup:
		{
			ConvertPosition  = ConvertPositionRightHandedZup;
			ConvertDirection = ConvertDirectionRightHandedZup;
			break;
		}
	}
}

void FDatasmithFacadeElement::SetWorldUnitScale(
	float InWorldUnitScale
)
{
	WorldUnitScale = FMath::IsNearlyZero(InWorldUnitScale) ? SMALL_NUMBER : InWorldUnitScale;
}

FDatasmithFacadeElement::FDatasmithFacadeElement(
	const TSharedRef<IDatasmithElement>& InElement
)
	: InternalDatasmithElement(InElement)
{}

void FDatasmithFacadeElement::GetStringHash(const TCHAR* InString, TCHAR OutBuffer[33], size_t BufferSize)
{
	FString HashedName = FMD5::HashAnsiString(InString);
	FCString::Strncpy(OutBuffer, *HashedName, BufferSize);
}

void FDatasmithFacadeElement::SetName(
	const TCHAR* InElementName
)
{
	InternalDatasmithElement->SetName(InElementName);
}

const TCHAR* FDatasmithFacadeElement::GetName() const
{
	return InternalDatasmithElement->GetName();
}

void FDatasmithFacadeElement::SetLabel(
	const TCHAR* InElementLabel
)
{
	InternalDatasmithElement->SetLabel(InElementLabel);
}

const TCHAR* FDatasmithFacadeElement::GetLabel() const
{
	return InternalDatasmithElement->GetLabel();
}

FVector FDatasmithFacadeElement::ConvertTranslation(
	FVector const& InVertex
)
{
	return ConvertPosition(InVertex.X, InVertex.Y, InVertex.Z);
}

void FDatasmithFacadeElement::ExportAsset(
	FString const& InAssetFolder
)
{
	// By default, there is no Datasmith scene element asset to build and export.
}

FVector FDatasmithFacadeElement::ConvertPositionLeftHandedYup(
	float InX,
	float InY,
	float InZ
)
{
	return FVector(InX * WorldUnitScale, -InZ * WorldUnitScale, InY * WorldUnitScale);
}

FVector FDatasmithFacadeElement::ConvertPositionLeftHandedZup(
	float InX,
	float InY,
	float InZ
)
{
	return FVector(InX * WorldUnitScale, InY * WorldUnitScale, InZ * WorldUnitScale);
}

FVector FDatasmithFacadeElement::ConvertPositionRightHandedZup(
	float InX,
	float InY,
	float InZ
)
{
	// Convert the position from the source right-hand Z-up coordinate system to the Unreal left-hand Z-up coordinate system.
	// To avoid perturbating X, which is forward in Unreal, the handedness conversion is done by flipping the side vector Y.
	return FVector(InX * WorldUnitScale, -InY * WorldUnitScale, InZ * WorldUnitScale);
}

FVector FDatasmithFacadeElement::ConvertDirectionLeftHandedYup(
	float InX,
	float InY,
	float InZ
)
{
	return FVector(InX, -InZ, InY);
}

FVector FDatasmithFacadeElement::ConvertDirectionLeftHandedZup(
	float InX,
	float InY,
	float InZ
)
{
	return FVector(InX, InY, InZ);
}

FVector FDatasmithFacadeElement::ConvertDirectionRightHandedZup(
	float InX,
	float InY,
	float InZ
)
{
	// Convert the direction from the source right-hand Z-up coordinate system to the Unreal left-hand Z-up coordinate system.
	// To avoid perturbating X, which is forward in Unreal, the handedness conversion is done by flipping the side vector Y.
	return FVector(InX, -InY, InZ);
}
