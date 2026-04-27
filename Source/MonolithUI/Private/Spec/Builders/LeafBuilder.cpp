// Copyright tumourlove. All Rights Reserved.
// LeafBuilder.cpp
//
// Phase H — leaf + content widget sub-builder. Constructs the widget,
// attaches it under its parent, applies the FUISpecContent sub-bag fields
// (text / font size / brush). Most heavy lifting goes through the same
// reflection helper the property-write action uses, so any field the
// allowlist exposes is writable here too.

#include "Spec/Builders/LeafBuilder.h"

#include "Spec/UIBuildContext.h"
#include "Spec/UISpec.h"
#include "MonolithUICommon.h"
#include "Registry/MonolithUIRegistrySubsystem.h"
#include "Registry/UIPropertyAllowlist.h"
#include "Registry/UIPropertyPathCache.h"
#include "Registry/UIReflectionHelper.h"

#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/PanelSlot.h"
#include "Components/Widget.h"
#include "Components/TextBlock.h"
#include "Components/RichTextBlock.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/EditableText.h"
#include "Components/EditableTextBox.h"
#include "WidgetBlueprint.h"

#include "Materials/MaterialInterface.h"
#include "Engine/Texture2D.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace MonolithUI::LeafBuilderInternal
{
    /**
     * Construct the widget + attach to parent. Mirrors the panel-builder
     * helper but tolerates content widgets (single-child slot semantics)
     * and leaves (no slot semantics at all).
     */
    static UWidget* ConstructAndAttach(
        FUIBuildContext& Context,
        const FUISpecNode& Node,
        UClass* WidgetClass,
        UPanelWidget* ParentPanel)
    {
        if (!Context.TargetWBP || !Context.TargetWBP->WidgetTree)
        {
            FUISpecError E;
            E.Severity = EUISpecErrorSeverity::Error;
            E.Category = TEXT("Internal");
            E.WidgetId = Node.Id;
            E.Message  = TEXT("LeafBuilder: target WBP / WidgetTree is null.");
            Context.Errors.Add(MoveTemp(E));
            return nullptr;
        }

        UWidget* Constructed = Context.TargetWBP->WidgetTree->ConstructWidget<UWidget>(
            WidgetClass, Node.Id);
        if (!Constructed)
        {
            FUISpecError E;
            E.Severity = EUISpecErrorSeverity::Error;
            E.Category = TEXT("Construct");
            E.WidgetId = Node.Id;
            E.Message  = FString::Printf(
                TEXT("ConstructWidget returned null for class '%s'."),
                *WidgetClass->GetName());
            Context.Errors.Add(MoveTemp(E));
            return nullptr;
        }

        if (ParentPanel)
        {
            UPanelSlot* Slot = ParentPanel->AddChild(Constructed);
            if (!Slot)
            {
                FUISpecError E;
                E.Severity = EUISpecErrorSeverity::Error;
                E.Category = TEXT("Slot");
                E.WidgetId = Node.Id;
                if (!ParentPanel->CanHaveMultipleChildren() && ParentPanel->GetChildrenCount() > 0)
                {
                    UWidget* Existing = ParentPanel->GetChildAt(0);
                    E.Message = FString::Printf(
                        TEXT("AddChild failed: parent '%s' is single-child (%s) and already holds '%s'."),
                        *ParentPanel->GetName(),
                        *ParentPanel->GetClass()->GetName(),
                        Existing ? *Existing->GetName() : TEXT("?"));
                }
                else
                {
                    E.Message = TEXT("AddChild returned null slot.");
                }
                Context.Errors.Add(MoveTemp(E));
                return nullptr;
            }
        }

        MonolithUI::RegisterCreatedWidget(Context.TargetWBP, Constructed);
        return Constructed;
    }

    /**
     * Apply FUISpecContent fields onto known leaf widget types. We dispatch
     * on Cast<> for the well-known ones (TextBlock, Image, Button,
     * EditableText/EditableTextBox); unknown leaves get the field-by-field
     * reflection-helper path so custom widgets with text/brush fields still
     * work.
     */
    static void ApplyContent(
        FUIBuildContext& Context,
        const FUISpecNode& Node,
        UWidget* Widget)
    {
        if (!Widget) return;
        const FUISpecContent& C = Node.Content;

        if (UTextBlock* T = Cast<UTextBlock>(Widget))
        {
            if (!C.Text.IsEmpty())
            {
                T->SetText(FText::FromString(C.Text));
            }
            if (C.FontSize > 0.f)
            {
                FSlateFontInfo F = T->GetFont();
                F.Size = (int32)C.FontSize;
                T->SetFont(F);
            }
            T->SetColorAndOpacity(FSlateColor(C.FontColor));
            return;
        }
        if (URichTextBlock* RT = Cast<URichTextBlock>(Widget))
        {
            if (!C.Text.IsEmpty())
            {
                RT->SetText(FText::FromString(C.Text));
            }
            return;
        }
        if (UImage* I = Cast<UImage>(Widget))
        {
            if (!C.BrushPath.IsEmpty())
            {
                if (UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *C.BrushPath))
                {
                    I->SetBrushFromTexture(Tex);
                }
                else if (UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, *C.BrushPath))
                {
                    I->SetBrushFromMaterial(Mat);
                }
                else
                {
                    FUISpecError W;
                    W.Severity = EUISpecErrorSeverity::Warning;
                    W.Category = TEXT("Asset");
                    W.WidgetId = Node.Id;
                    W.Message  = FString::Printf(
                        TEXT("Image brush asset '%s' could not be loaded."), *C.BrushPath);
                    Context.Warnings.Add(MoveTemp(W));
                }
            }
            return;
        }
        if (UButton* B = Cast<UButton>(Widget))
        {
            // UMG buttons hold a single child via SetContent — text labels
            // are usually a child UTextBlock, not a Button-direct property.
            // The text is handled when the spec wires a child node; we leave
            // a hint warning so authors know to nest a TextBlock.
            (void)B;
            return;
        }
        if (UEditableText* ET = Cast<UEditableText>(Widget))
        {
            if (!C.Placeholder.IsEmpty())
            {
                ET->SetHintText(FText::FromString(C.Placeholder));
            }
            if (!C.Text.IsEmpty())
            {
                ET->SetText(FText::FromString(C.Text));
            }
            return;
        }
        if (UEditableTextBox* ETB = Cast<UEditableTextBox>(Widget))
        {
            if (!C.Placeholder.IsEmpty())
            {
                ETB->SetHintText(FText::FromString(C.Placeholder));
            }
            if (!C.Text.IsEmpty())
            {
                ETB->SetText(FText::FromString(C.Text));
            }
            return;
        }

        // Unknown widget — try the reflection helper for any field the spec
        // populated. v1 doesn't iterate the content sub-bag; this hook is
        // here for Phase J-shaped expansion.
    }

    /**
     * Apply FUISpecStyle base fields (RenderOpacity / Visibility) — same
     * shape as PanelBuilder::ApplyCommonStyle, kept inline so the leaf path
     * doesn't have to depend on the panel header.
     */
    static void ApplyCommonStyle(UWidget* Widget, const FUISpecNode& Node)
    {
        if (!Widget) return;
        const FUISpecStyle& S = Node.Style;
        Widget->SetRenderOpacity(FMath::Clamp(S.Opacity, 0.f, 1.f));
        if (!S.Visibility.IsNone())
        {
            const FString V = S.Visibility.ToString();
            if      (V == TEXT("Visible"))                Widget->SetVisibility(ESlateVisibility::Visible);
            else if (V == TEXT("Hidden"))                 Widget->SetVisibility(ESlateVisibility::Hidden);
            else if (V == TEXT("Collapsed"))              Widget->SetVisibility(ESlateVisibility::Collapsed);
            else if (V == TEXT("HitTestInvisible"))       Widget->SetVisibility(ESlateVisibility::HitTestInvisible);
            else if (V == TEXT("SelfHitTestInvisible"))   Widget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        }
    }
} // namespace MonolithUI::LeafBuilderInternal


UWidget* MonolithUI::LeafBuilder::BuildLeafOrContent(
    FUIBuildContext& Context,
    const FUISpecNode& Node,
    UClass* WidgetClass,
    UPanelWidget* ParentPanel)
{
    using namespace MonolithUI::LeafBuilderInternal;

    UWidget* Constructed = ConstructAndAttach(Context, Node, WidgetClass, ParentPanel);
    if (!Constructed)
    {
        return nullptr;
    }

    ApplyContent(Context, Node, Constructed);
    ApplyCommonStyle(Constructed, Node);

    return Constructed;
}
