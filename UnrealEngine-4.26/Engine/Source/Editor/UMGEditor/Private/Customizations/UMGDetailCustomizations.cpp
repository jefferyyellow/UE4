// Copyright Epic Games, Inc. All Rights Reserved.

#include "Customizations/UMGDetailCustomizations.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"

#if WITH_EDITOR
	#include "EditorStyleSet.h"
#endif // WITH_EDITOR
#include "EdGraphSchema_K2.h"
#include "EdGraphSchema_K2_Actions.h"
#include "K2Node_ComponentBoundEvent.h"
#include "Kismet2/KismetEditorUtilities.h"

#include "BlueprintModes/WidgetBlueprintApplicationModes.h"
#include "DetailWidgetRow.h"
#include "PropertyHandle.h"
#include "IDetailPropertyRow.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "ObjectEditorUtils.h"
#include "ScopedTransaction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Components/PanelSlot.h"
#include "IPropertyAccessEditor.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "IDetailsView.h"
#include "IDetailPropertyExtensionHandler.h"
#include "Binding/PropertyBinding.h"
#include "Components/WidgetComponent.h"
#include "Algo/Transform.h"

#define LOCTEXT_NAMESPACE "UMG"

class SGraphSchemaActionButton : public SCompoundWidget, public FGCObject
{
public:

	SLATE_BEGIN_ARGS(SGraphSchemaActionButton) {}
		/** Slot for this designers content (optional) */
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<FWidgetBlueprintEditor> InEditor, TSharedPtr<FEdGraphSchemaAction> InClickAction)
	{
		Editor = InEditor;
		Action = InClickAction;

		ChildSlot
		[
			SNew(SButton)
			.ButtonStyle(FEditorStyle::Get(), "FlatButton.Success")
			.TextStyle(FEditorStyle::Get(), "NormalText")
			.HAlign(HAlign_Center)
			.ForegroundColor(FSlateColor::UseForeground())
			.ToolTipText(Action->GetTooltipDescription())
			.OnClicked(this, &SGraphSchemaActionButton::AddOrViewEventBinding)
			[
				InArgs._Content.Widget
			]
		];
	}

private:
	FReply AddOrViewEventBinding()
	{
		UBlueprint* Blueprint = Editor.Pin()->GetBlueprintObj();

		UEdGraph* TargetGraph = Blueprint->GetLastEditedUberGraph();
		
		if ( TargetGraph != nullptr )
		{
			Editor.Pin()->SetCurrentMode(FWidgetBlueprintApplicationModes::GraphMode);

			// Figure out a decent place to stick the node
			const FVector2D NewNodePos = TargetGraph->GetGoodPlaceForNewNode();

			Action->PerformAction(TargetGraph, nullptr, NewNodePos);
		}

		return FReply::Handled();
	}

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		Action->AddReferencedObjects(Collector);
	}

private:
	TWeakPtr<FWidgetBlueprintEditor> Editor;

	TSharedPtr<FEdGraphSchemaAction> Action;
};

TSharedRef<SWidget> FBlueprintWidgetCustomization::MakePropertyBindingWidget(TWeakPtr<FWidgetBlueprintEditor> InEditor, FDelegateProperty* InDelegateProperty, TSharedRef<IPropertyHandle> InPropertyHandle, bool bInGeneratePureBindings)
{
	const FName PropertyName = InPropertyHandle->GetProperty()->GetFName();

	TArray<UObject*> Objects;
	InPropertyHandle->GetOuterObjects(Objects);

	UWidget* Widget = CastChecked<UWidget>(Objects[0]);

	FString WidgetName;
	if ( Widget && !Widget->IsGeneratedName() )
	{
		WidgetName = TEXT("_") + Widget->GetName() + TEXT("_");
	}

	if(IModularFeatures::Get().IsModularFeatureAvailable("PropertyAccessEditor"))
	{
		FPropertyBindingWidgetArgs Args;
		Args.Property = InPropertyHandle->GetProperty();
		Args.BindableSignature = InDelegateProperty->SignatureFunction;
		Args.OnGenerateBindingName = FOnGenerateBindingName::CreateLambda([WidgetName]()
		{
			return WidgetName;
		});

		Args.OnGotoBinding = FOnGotoBinding::CreateLambda([InEditor, Objects](FName InPropertyName)
		{
			UWidgetBlueprint* ThisBlueprint = InEditor.Pin()->GetWidgetBlueprintObj();

			//TODO UMG O(N) Isn't good for this, needs to be map, but map isn't serialized, need cached runtime map for fast lookups.

			for ( int32 ObjectIndex = 0; ObjectIndex < Objects.Num(); ObjectIndex++ )
			{
				// Ignore null outer objects
				if ( Objects[ObjectIndex] == nullptr )
				{
					continue;
				}

				for ( const FDelegateEditorBinding& Binding : ThisBlueprint->Bindings )
				{
					if ( Binding.ObjectName == Objects[ObjectIndex]->GetName() && Binding.PropertyName == InPropertyName )
					{
						if ( Binding.Kind == EBindingKind::Function )
						{
							TArray<UEdGraph*> AllGraphs;
							ThisBlueprint->GetAllGraphs(AllGraphs);

							FGuid SearchForGuid = Binding.MemberGuid;
							if ( !Binding.SourcePath.IsEmpty() )
							{
								SearchForGuid = Binding.SourcePath.Segments.Last().GetMemberGuid();
							}

							for ( UEdGraph* Graph : AllGraphs )
							{
								if ( Graph->GraphGuid == SearchForGuid )
								{
									InEditor.Pin()->SetCurrentMode(FWidgetBlueprintApplicationModes::GraphMode);
									InEditor.Pin()->OpenDocument(Graph, FDocumentTracker::OpenNewDocument);
								}
							}

							// Either way return
							return true;
						}
					}
				}
			}

			return false;
		});

		Args.OnCanGotoBinding = FOnCanGotoBinding::CreateLambda([InEditor, Objects](FName InPropertyName)
		{
			UWidgetBlueprint* ThisBlueprint = InEditor.Pin()->GetWidgetBlueprintObj();

			for ( int32 ObjectIndex = 0; ObjectIndex < Objects.Num(); ObjectIndex++ )
			{
				// Ignore null outer objects
				if ( Objects[ObjectIndex] == nullptr )
				{
					continue;
				}

				for ( const FDelegateEditorBinding& Binding : ThisBlueprint->Bindings )
				{
					if ( Binding.ObjectName == Objects[ObjectIndex]->GetName() && Binding.PropertyName == InPropertyName )
					{
						if ( Binding.Kind == EBindingKind::Function )
						{
							return true;
						}
					}
				}
			}

			return false;
		});

		Args.OnCanBindProperty = FOnCanBindProperty::CreateLambda([InDelegateProperty](FProperty* InProperty)
		{
			if ( FProperty* ReturnProperty = InDelegateProperty->SignatureFunction->GetReturnProperty() )
			{
				// Find the binder that can handle the delegate return type.
				TSubclassOf<UPropertyBinding> Binder = UWidget::FindBinderClassForDestination(ReturnProperty);
				if ( Binder != nullptr )
				{
					// Ensure that the binder also can handle binding from the property we care about.
					return ( Binder->GetDefaultObject<UPropertyBinding>()->IsSupportedSource(InProperty) );
				}
			}

			return false;
		});
	
		Args.OnCanBindFunction = FOnCanBindFunction::CreateLambda([InDelegateProperty](UFunction* InFunction)
		{
			auto HasFunctionBinder = [InFunction](UFunction* InBindableSignature)
			{
				if ( InFunction->NumParms == 1 && InBindableSignature->NumParms == 1 )
				{
					if ( FProperty* FunctionReturn = InFunction->GetReturnProperty() )
					{
						if ( FProperty* DelegateReturn = InBindableSignature->GetReturnProperty() )
						{
							// Find the binder that can handle the delegate return type.
							TSubclassOf<UPropertyBinding> Binder = UWidget::FindBinderClassForDestination(DelegateReturn);
							if ( Binder != nullptr )
							{
								// Ensure that the binder also can handle binding from the property we care about.
								if ( Binder->GetDefaultObject<UPropertyBinding>()->IsSupportedSource(FunctionReturn) )
								{
									return true;
								}
							}
						}
					}
				}

				return false;
			};

			// We ignore CPF_ReturnParm because all that matters for binding to script functions is that the number of out parameters match.
			return ( InFunction->IsSignatureCompatibleWith(InDelegateProperty->SignatureFunction, UFunction::GetDefaultIgnoredSignatureCompatibilityFlags() | CPF_ReturnParm) ||
				 HasFunctionBinder(InDelegateProperty->SignatureFunction) );
		});

		Args.OnCanBindToClass = FOnCanBindToClass::CreateLambda([](UClass* InClass)
		{
			if (InClass == UUserWidget::StaticClass() ||
				InClass == AActor::StaticClass() ||
				InClass == APawn::StaticClass() ||
				InClass == UObject::StaticClass() ||
				InClass == UPrimitiveComponent::StaticClass() ||
				InClass == USceneComponent::StaticClass() ||
				InClass == UActorComponent::StaticClass() ||
				InClass == UWidgetComponent::StaticClass() ||
				InClass == UStaticMeshComponent::StaticClass() ||
				InClass == UWidgetAnimation::StaticClass() )
			{
				return false;
			}
		
			return true;
		});

		Args.OnCanBindToSubObjectClass = FOnCanBindToSubObjectClass::CreateLambda([](UClass* InClass)
		{
			// Ignore any properties that are widgets, we don't want users binding widgets to other widgets.
			return InClass->IsChildOf(UWidget::StaticClass());
		});

		Args.OnAddBinding = FOnAddBinding::CreateLambda([InEditor, Objects](FName InPropertyName, const TArray<FBindingChainElement>& InBindingChain)
		{
			UWidgetBlueprint* ThisBlueprint = InEditor.Pin()->GetWidgetBlueprintObj();
			UBlueprintGeneratedClass* SkeletonClass = Cast<UBlueprintGeneratedClass>(ThisBlueprint->SkeletonGeneratedClass);

			ThisBlueprint->Modify();

			TArray<FFieldVariant> FieldChain;
			Algo::Transform(InBindingChain, FieldChain, [](const FBindingChainElement& InElement)
			{
				return InElement.Field;
			});

			UFunction* Function = FieldChain.Last().Get<UFunction>();
			FProperty* Property = FieldChain.Last().Get<FProperty>();

			check(Function != nullptr || Property != nullptr);

			for ( UObject* SelectedObject : Objects )
			{
				FDelegateEditorBinding Binding;
				Binding.ObjectName = SelectedObject->GetName();
				Binding.PropertyName = InPropertyName;
				Binding.SourcePath = FEditorPropertyPath(FieldChain);

				if ( Function != nullptr)
				{
					Binding.FunctionName = Function->GetFName();

					UBlueprint::GetGuidFromClassByFieldName<UFunction>(
						Function->GetOwnerClass(),
						Function->GetFName(),
						Binding.MemberGuid);

					Binding.Kind = EBindingKind::Function;
				}
				else if( Property != nullptr )
				{
					Binding.SourceProperty = Property->GetFName();

					UBlueprint::GetGuidFromClassByFieldName<FProperty>(
						SkeletonClass,
						Property->GetFName(),
						Binding.MemberGuid);

					Binding.Kind = EBindingKind::Property;
				}

				ThisBlueprint->Bindings.Remove(Binding);
				ThisBlueprint->Bindings.AddUnique(Binding);
			}

			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(ThisBlueprint);	
		});

		Args.OnRemoveBinding = FOnRemoveBinding::CreateLambda([InEditor, Objects](FName InPropertyName)
		{
			UWidgetBlueprint* ThisBlueprint = InEditor.Pin()->GetWidgetBlueprintObj();

			ThisBlueprint->Modify();

			for ( UObject* SelectedObject : Objects )
			{
				FDelegateEditorBinding Binding;
				Binding.ObjectName = SelectedObject->GetName();
				Binding.PropertyName = InPropertyName;

				ThisBlueprint->Bindings.Remove(Binding);
			}

			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(ThisBlueprint);
		});

		Args.OnCanRemoveBinding = FOnCanRemoveBinding::CreateLambda([InEditor, Objects](FName InPropertyName)
		{
			UWidgetBlueprint* ThisBlueprint = InEditor.Pin()->GetWidgetBlueprintObj();

			for ( UObject* SelectedObject : Objects )
			{
				for ( const FDelegateEditorBinding& Binding : ThisBlueprint->Bindings )
				{
					if ( Binding.ObjectName == SelectedObject->GetName() && Binding.PropertyName == InPropertyName )
					{
						return true;
					}
				}
			}

			return false;
		});

		Args.CurrentBindingText = MakeAttributeLambda([InEditor, Objects, PropertyName]()
		{
			UWidgetBlueprint* ThisBlueprint = InEditor.Pin()->GetWidgetBlueprintObj();

			//TODO UMG O(N) Isn't good for this, needs to be map, but map isn't serialized, need cached runtime map for fast lookups.

			for ( int32 ObjectIndex = 0; ObjectIndex < Objects.Num(); ObjectIndex++ )
			{
				// Ignore null outer objects
				if ( Objects[ObjectIndex] == nullptr )
				{
					continue;
				}

				//TODO UMG handle multiple things selected

				for ( const FDelegateEditorBinding& Binding : ThisBlueprint->Bindings )
				{
					if ( Binding.ObjectName == Objects[ObjectIndex]->GetName() && Binding.PropertyName == PropertyName )
					{
						if ( !Binding.SourcePath.IsEmpty() )
						{
							return Binding.SourcePath.GetDisplayText();
						}
						else
						{
							if ( Binding.Kind == EBindingKind::Function )
							{
								if ( Binding.MemberGuid.IsValid() )
								{
									// Graph function, look up by Guid
									FName FoundName = ThisBlueprint->GetFieldNameFromClassByGuid<UFunction>(ThisBlueprint->GeneratedClass, Binding.MemberGuid);
									return FText::FromString(FName::NameToDisplayString(FoundName.ToString(), false));
								}
								else
								{
									// No GUID, native function, return function name.
									return FText::FromName(Binding.FunctionName);
								}
							}
							else // Property
							{
								if ( Binding.MemberGuid.IsValid() )
								{
									FName FoundName = ThisBlueprint->GetFieldNameFromClassByGuid<FProperty>(ThisBlueprint->GeneratedClass, Binding.MemberGuid);
									return FText::FromString(FName::NameToDisplayString(FoundName.ToString(), false));
								}
								else
								{
									// No GUID, native property, return source property.
									return FText::FromName(Binding.SourceProperty);
								}
							}
						}
					}
				}

				//TODO UMG Do something about missing functions, little exclamation points if they're missing and such.

				break;
			}

			return LOCTEXT("Bind", "Bind");
		});

		Args.CurrentBindingImage = MakeAttributeLambda([InEditor, Objects, PropertyName]() -> const FSlateBrush*
		{
			static FName PropertyIcon(TEXT("Kismet.Tabs.Variables"));
			static FName FunctionIcon(TEXT("GraphEditor.Function_16x"));

			UWidgetBlueprint* ThisBlueprint = InEditor.Pin()->GetWidgetBlueprintObj();

			//TODO UMG O(N) Isn't good for this, needs to be map, but map isn't serialized, need cached runtime map for fast lookups.

			for ( int32 ObjectIndex = 0; ObjectIndex < Objects.Num(); ObjectIndex++ )
			{
				// Ignore null outer objects
				if ( Objects[ObjectIndex] == NULL )
				{
					continue;
				}

				//TODO UMG handle multiple things selected

				for ( const FDelegateEditorBinding& Binding : ThisBlueprint->Bindings )
				{
					if ( Binding.ObjectName == Objects[ObjectIndex]->GetName() && Binding.PropertyName == PropertyName )
					{
						if ( Binding.Kind == EBindingKind::Function )
						{
							return FEditorStyle::GetBrush(FunctionIcon);
						}
						else // Property
						{
							return FEditorStyle::GetBrush(PropertyIcon);
						}
					}
				}
			}

			return nullptr;
		});

		Args.bGeneratePureBindings = bInGeneratePureBindings;

		IPropertyAccessEditor& PropertyAccessEditor = IModularFeatures::Get().GetModularFeature<IPropertyAccessEditor>("PropertyAccessEditor");
		return PropertyAccessEditor.MakePropertyBindingWidget(InEditor.Pin()->GetBlueprintObj(), Args);
	}
	else
	{
		return SNullWidget::NullWidget;
	}
}

void FBlueprintWidgetCustomization::CreateEventCustomization( IDetailLayoutBuilder& DetailLayout, FDelegateProperty* Property, UWidget* Widget )
{
	TSharedRef<IPropertyHandle> DelegatePropertyHandle = DetailLayout.GetProperty(Property->GetFName(), Property->GetOwnerChecked<UClass>());

	const bool bHasValidHandle = DelegatePropertyHandle->IsValidHandle();
	if(!bHasValidHandle)
	{
		return;
	}

	IDetailCategoryBuilder& PropertyCategory = DetailLayout.EditCategory(FObjectEditorUtils::GetCategoryFName(Property), FText::GetEmpty(), ECategoryPriority::Uncommon);

	IDetailPropertyRow& PropertyRow = PropertyCategory.AddProperty(DelegatePropertyHandle);
	PropertyRow.OverrideResetToDefault(FResetToDefaultOverride::Create(FResetToDefaultHandler::CreateSP(this, &FBlueprintWidgetCustomization::ResetToDefault_RemoveBinding)));

	FString LabelStr = Property->GetDisplayNameText().ToString();
	LabelStr.RemoveFromEnd(TEXT("Event"));

	FText Label = FText::FromString(LabelStr);

	const bool bShowChildren = true;
	PropertyRow.CustomWidget(bShowChildren)
		.NameContent()
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0,0,5,0)
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush("GraphEditor.Event_16x"))
			]

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Label)
			]
		]
		.ValueContent()
		.MinDesiredWidth(200)
		.MaxDesiredWidth(250)
		[
			MakePropertyBindingWidget(Editor.Pin(), Property, DelegatePropertyHandle, false)
		];
}

void FBlueprintWidgetCustomization::ResetToDefault_RemoveBinding(TSharedPtr<IPropertyHandle> PropertyHandle)
{
	const FScopedTransaction Transaction(LOCTEXT("UnbindDelegate", "Remove Binding"));

	Blueprint->Modify();

	TArray<UObject*> OuterObjects;
	PropertyHandle->GetOuterObjects(OuterObjects);
	for ( UObject* SelectedObject : OuterObjects )
	{
		FDelegateEditorBinding Binding;
		Binding.ObjectName = SelectedObject->GetName();
		Binding.PropertyName = PropertyHandle->GetProperty()->GetFName();

		Blueprint->Bindings.Remove(Binding);
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
}


FReply FBlueprintWidgetCustomization::HandleAddOrViewEventForVariable(const FName EventName, FName PropertyName, TWeakObjectPtr<UClass> PropertyClass)
{
	UBlueprint* BlueprintObj = Blueprint;

	// Find the corresponding variable property in the Blueprint
	FObjectProperty* VariableProperty = FindFProperty<FObjectProperty>(BlueprintObj->SkeletonGeneratedClass, PropertyName);

	if (VariableProperty)
	{
		if (!FKismetEditorUtilities::FindBoundEventForComponent(BlueprintObj, EventName, VariableProperty->GetFName()))
		{
			FKismetEditorUtilities::CreateNewBoundEventForClass(PropertyClass.Get(), EventName, BlueprintObj, VariableProperty);
		}
		else
		{
			const UK2Node_ComponentBoundEvent* ExistingNode = FKismetEditorUtilities::FindBoundEventForComponent(BlueprintObj, EventName, VariableProperty->GetFName());
			if (ExistingNode)
			{
				FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(ExistingNode);
			}
		}
	}

	return FReply::Handled();
}

int32 FBlueprintWidgetCustomization::HandleAddOrViewIndexForButton(const FName EventName, FName PropertyName) const
{
	UBlueprint* BlueprintObj = Blueprint;

	if (FKismetEditorUtilities::FindBoundEventForComponent(BlueprintObj, EventName, PropertyName))
	{
		return 0; // View
	}

	return 1; // Add
}

void FBlueprintWidgetCustomization::CreateMulticastEventCustomization(IDetailLayoutBuilder& DetailLayout, FName ThisComponentName, UClass* PropertyClass, FMulticastDelegateProperty* DelegateProperty)
{
	const FString AddString = FString(TEXT("Add "));
	const FString ViewString = FString(TEXT("View "));

	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

	if ( !K2Schema->CanUserKismetAccessVariable(DelegateProperty, PropertyClass, UEdGraphSchema_K2::MustBeDelegate) )
	{
		return;
	}

	FText PropertyTooltip = DelegateProperty->GetToolTipText();
	if ( PropertyTooltip.IsEmpty() )
	{
		PropertyTooltip = FText::FromString(DelegateProperty->GetName());
	}

	FObjectProperty* ComponentProperty = FindFProperty<FObjectProperty>(Blueprint->SkeletonGeneratedClass, ThisComponentName);

	if ( !ComponentProperty )
	{
		return;
	}

	FName PropertyName = ComponentProperty->GetFName();
	FName EventName = DelegateProperty->GetFName();
	FText EventText = DelegateProperty->GetDisplayNameText();

	IDetailCategoryBuilder& EventCategory = DetailLayout.EditCategory(TEXT("Events"), LOCTEXT("Events", "Events"), ECategoryPriority::Uncommon);

	EventCategory.AddCustomRow(EventText)
		.NameContent()
		[
			SNew(SHorizontalBox)
			.ToolTipText(DelegateProperty->GetToolTipText())
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 5, 0)
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush("GraphEditor.Event_16x"))
			]

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(EventText)
			]
		]
		.ValueContent()
		.MinDesiredWidth(150)
		.MaxDesiredWidth(200)
		[
			SNew(SButton)
			.ButtonStyle(FEditorStyle::Get(), "FlatButton.Success")
			.HAlign(HAlign_Center)
			.OnClicked(this, &FBlueprintWidgetCustomization::HandleAddOrViewEventForVariable, EventName, PropertyName, MakeWeakObjectPtr(PropertyClass))
			.ForegroundColor(FSlateColor::UseForeground())
			[
				SNew(SWidgetSwitcher)
				.WidgetIndex(this, &FBlueprintWidgetCustomization::HandleAddOrViewIndexForButton, EventName, PropertyName)
				+ SWidgetSwitcher::Slot()
				[
					SNew(STextBlock)
					.Font(FEditorStyle::GetFontStyle(TEXT("BoldFont")))
					.Text(LOCTEXT("ViewEvent", "View"))
				]
				+ SWidgetSwitcher::Slot()
				[
					SNew(SImage)
					.Image(FEditorStyle::GetBrush("Plus"))
				]
			]
		];
}

void FBlueprintWidgetCustomization::CustomizeDetails( IDetailLayoutBuilder& DetailLayout )
{
	static const FName LayoutCategoryKey(TEXT("Layout"));
	static const FName LocalizationCategoryKey(TEXT("Localization"));

	DetailLayout.EditCategory(LocalizationCategoryKey, FText::GetEmpty(), ECategoryPriority::Uncommon);

	TArray< TWeakObjectPtr<UObject> > OutObjects;
	DetailLayout.GetObjectsBeingCustomized(OutObjects);
	
	UClass* SlotBaseClasses = nullptr;
	for (const TWeakObjectPtr<UObject>& Obj : OutObjects)
	{
		if (UWidget* Widget = Cast<UWidget>(Obj.Get()))
		{
			if (Widget->Slot)
			{
				UClass* SlotClass = Widget->Slot->GetClass();
				if (!SlotBaseClasses)
				{
					SlotBaseClasses = SlotClass;
				}
				else if (SlotBaseClasses != SlotClass)
				{
					SlotBaseClasses = nullptr;
					break;
				}
			}
			else
			{
				SlotBaseClasses = nullptr;
				break;
			}
		}
		else
		{
			SlotBaseClasses = nullptr;
			break;
		}
	}
	
	if (SlotBaseClasses)
	{
		FText LayoutCatName = FText::Format(LOCTEXT("SlotNameFmt", "Slot ({0})"), SlotBaseClasses->GetDisplayNameText());
		DetailLayout.EditCategory(LayoutCategoryKey, LayoutCatName, ECategoryPriority::TypeSpecific);
	}
	else
	{
		DetailLayout.EditCategory(LayoutCategoryKey, FText(), ECategoryPriority::TypeSpecific);
	}

	PerformAccessibilityCustomization(DetailLayout);
	PerformBindingCustomization(DetailLayout);
}

void FBlueprintWidgetCustomization::PerformBindingCustomization(IDetailLayoutBuilder& DetailLayout)
{
	static const FName IsBindableEventName(TEXT("IsBindableEvent"));

	TArray< TWeakObjectPtr<UObject> > OutObjects;
	DetailLayout.GetObjectsBeingCustomized(OutObjects);

	if ( OutObjects.Num() == 1 )
	{
		UWidget* Widget = Cast<UWidget>(OutObjects[0].Get());
		UClass* PropertyClass = OutObjects[0].Get()->GetClass();

		for ( TFieldIterator<FProperty> PropertyIt(PropertyClass, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt )
		{
			FProperty* Property = *PropertyIt;

			if ( FDelegateProperty* DelegateProperty = CastField<FDelegateProperty>(*PropertyIt) )
			{
				//TODO Remove the code to use ones that end with "Event".  Prefer metadata flag.
				if ( DelegateProperty->HasMetaData(IsBindableEventName) || DelegateProperty->GetName().EndsWith(TEXT("Event")) )
				{
					CreateEventCustomization(DetailLayout, DelegateProperty, Widget);
				}
			}
			else if ( FMulticastDelegateProperty* MulticastDelegateProperty = CastField<FMulticastDelegateProperty>(Property) )
			{
				CreateMulticastEventCustomization(DetailLayout, OutObjects[0].Get()->GetFName(), PropertyClass, MulticastDelegateProperty);
			}
		}
	}
}

void FBlueprintWidgetCustomization::PerformAccessibilityCustomization(IDetailLayoutBuilder& DetailLayout)
{
	// We have to add these properties even though we're not customizing to preserve UI ordering
	DetailLayout.EditCategory("Accessibility").AddProperty("bOverrideAccessibleDefaults");
	DetailLayout.EditCategory("Accessibility").AddProperty("bCanChildrenBeAccessible");
	CustomizeAccessibilityProperty(DetailLayout, "AccessibleBehavior", "AccessibleText");
	CustomizeAccessibilityProperty(DetailLayout, "AccessibleSummaryBehavior", "AccessibleSummaryText");
}

void FBlueprintWidgetCustomization::CustomizeAccessibilityProperty(IDetailLayoutBuilder& DetailLayout, const FName& BehaviorPropertyName, const FName& TextPropertyName)
{
	// Treat AccessibleBehavior as the "base" property for the row, and then add the AccessibleText binding to the end of it.
	TSharedRef<IPropertyHandle> AccessibleBehaviorPropertyHandle = DetailLayout.GetProperty(BehaviorPropertyName);
	IDetailPropertyRow& AccessibilityRow = DetailLayout.EditCategory("Accessibility").AddProperty(AccessibleBehaviorPropertyHandle);

	TSharedRef<IPropertyHandle> AccessibleTextPropertyHandle = DetailLayout.GetProperty(TextPropertyName);
	const FName DelegateName(*(TextPropertyName.ToString() + "Delegate"));
	FDelegateProperty* AccessibleTextDelegateProperty = FindFieldChecked<FDelegateProperty>(CastChecked<UClass>(AccessibleTextPropertyHandle->GetProperty()->GetOwner<UObject>()), DelegateName);
	// Make sure the old AccessibleText properties are hidden so we don't get duplicate widgets
	DetailLayout.HideProperty(AccessibleTextPropertyHandle);

	TSharedRef<SWidget> BindingWidget = MakePropertyBindingWidget(Editor, AccessibleTextDelegateProperty, AccessibleTextPropertyHandle, false);
	TSharedRef<SHorizontalBox> CustomTextLayout = SNew(SHorizontalBox)
	.Visibility(TAttribute<EVisibility>::Create([AccessibleBehaviorPropertyHandle]() -> EVisibility
	{
		uint8 Behavior = 0;
		AccessibleBehaviorPropertyHandle->GetValue(Behavior);
		return (ESlateAccessibleBehavior)Behavior == ESlateAccessibleBehavior::Custom ? EVisibility::Visible : EVisibility::Hidden;
	}))
	+ SHorizontalBox::Slot()
	.Padding(FMargin(4.0f, 0.0f))
	[
		AccessibleTextPropertyHandle->CreatePropertyValueWidget()
	]
	+SHorizontalBox::Slot()
	.AutoWidth()
	[
		BindingWidget
	];

	TSharedPtr<SWidget> AccessibleBehaviorNameWidget, AccessibleBehaviorValueWidget;
	AccessibilityRow.GetDefaultWidgets(AccessibleBehaviorNameWidget, AccessibleBehaviorValueWidget);

	AccessibilityRow.CustomWidget()
	.NameContent()
	[
		AccessibleBehaviorNameWidget.ToSharedRef()
	]
	.ValueContent()
	.HAlign(HAlign_Fill)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			AccessibleBehaviorValueWidget.ToSharedRef()
		]
		+ SHorizontalBox::Slot()
		[
			CustomTextLayout
		]
	];
}

#undef LOCTEXT_NAMESPACE
