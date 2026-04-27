// Copyright tumourlove. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

// Core / test
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"

// JSON / registry
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "MonolithToolRegistry.h"

// UMG -- build a throwaway WBP + probe the result
#include "Tests/Hoisted/MonolithUITestFixtureUtils.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"

// Asset / package
#include "Editor.h"

// Materials -- read-back
#include "Materials/MaterialInterface.h"

/**
 * MonolithUI.ApplyBoxShadow.Basic
 *
 * Architectural invariant: apply_box_shadow has ZERO compile dep on any
 * specific shadow material. The TEST discovers a canonical shadow material by
 * asset name; if that parent isn't yet present on disk, the test SKIPS.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FMonolithUIApplyBoxShadowBasicTest,
    "MonolithUI.ApplyBoxShadow.Basic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace MonolithUI::ApplyBoxShadowTests
{
    static FString FindMaterialPathByAssetName(FName AssetName)
    {
        IAssetRegistry& AssetRegistry =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(AssetRegistryConstants::ModuleName).Get();
        AssetRegistry.SearchAllAssets(/*bSynchronousSearch=*/true);

        TArray<FAssetData> Assets;
        AssetRegistry.GetAllAssets(Assets, /*bIncludeOnlyOnDiskAssets=*/true);
        for (const FAssetData& Asset : Assets)
        {
            if (Asset.AssetName != AssetName)
            {
                continue;
            }

            const FString ObjectPath = Asset.GetSoftObjectPath().ToString();
            if (LoadObject<UMaterialInterface>(nullptr, *ObjectPath))
            {
                return ObjectPath;
            }
        }

        return FString();
    }
}

bool FMonolithUIApplyBoxShadowBasicTest::RunTest(const FString& Parameters)
{
    using namespace MonolithUI::TestUtils;
    using namespace MonolithUI::ApplyBoxShadowTests;

    const FString ShadowMaterialPath = FindMaterialPathByAssetName(FName(TEXT("M_TokenShadow")));
    UMaterialInterface* ShadowParent = LoadObject<UMaterialInterface>(nullptr, *ShadowMaterialPath);
    if (!ShadowParent)
    {
        AddWarning(TEXT("Canonical shadow material not present -- skipping. Run after the parent material exists."));
        return true;
    }

    const FString WBPPath = TEXT("/Game/Tests/Monolith/UI/WBP_ShadowTest");
    FString FixtureError;
    UWidget* TargetWidget = nullptr;
    if (!CreateOrReuseTestWidgetBlueprint(WBPPath, FName(TEXT("Target")), nullptr, FixtureError, &TargetWidget))
    {
        AddError(FString::Printf(TEXT("Fixture build failed: %s"), *FixtureError));
        return false;
    }
    if (TargetWidget)
    {
        if (UCanvasPanelSlot* CSlot = Cast<UCanvasPanelSlot>(TargetWidget->Slot))
        {
            CSlot->SetPosition(FVector2D(100.0f, 100.0f));
            CSlot->SetSize(FVector2D(200.0f, 120.0f));
        }
    }

    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("asset_path"), WBPPath);
    Params->SetStringField(TEXT("widget_name"), TEXT("Target"));
    Params->SetStringField(TEXT("shadow_material_path"), ShadowMaterialPath);
    {
        TSharedPtr<FJsonObject> Shadow = MakeShared<FJsonObject>();
        Shadow->SetNumberField(TEXT("x"), 0.0);
        Shadow->SetNumberField(TEXT("y"), 4.0);
        Shadow->SetNumberField(TEXT("blur"), 8.0);
        Shadow->SetStringField(TEXT("color"), TEXT("#000000AA"));
        Params->SetObjectField(TEXT("shadow"), Shadow);
    }
    Params->SetBoolField(TEXT("compile"), true);

    const FMonolithActionResult Result = FMonolithToolRegistry::Get().ExecuteAction(
        TEXT("ui"), TEXT("apply_box_shadow"), Params);

    TestTrue(TEXT("apply_box_shadow bSuccess"), Result.bSuccess);
    if (!Result.bSuccess)
    {
        AddError(FString::Printf(TEXT("Action error: %s (code %d)"), *Result.ErrorMessage, Result.ErrorCode));
        return false;
    }

    if (!Result.Result.IsValid())
    {
        AddError(TEXT("Result payload was null on success"));
        return false;
    }
    double LayersApplied = 0.0;
    TestTrue(TEXT("result has layers_applied"),
        Result.Result->TryGetNumberField(TEXT("layers_applied"), LayersApplied));
    TestEqual(TEXT("layers_applied == 1"), (int32)LayersApplied, 1);

    const TArray<TSharedPtr<FJsonValue>>* ShadowNamesArr = nullptr;
    TestTrue(TEXT("result has shadow_widgets array"),
        Result.Result->TryGetArrayField(TEXT("shadow_widgets"), ShadowNamesArr));

    UWidgetBlueprint* WBP = LoadObject<UWidgetBlueprint>(nullptr, *WBPPath);
    if (WBP && WBP->WidgetTree)
    {
        UWidget* Shadow = WBP->WidgetTree->FindWidget(FName(TEXT("Target_Shadow0")));
        TestNotNull(TEXT("Target_Shadow0 exists in widget tree"), Shadow);

        UImage* ShadowImg = Cast<UImage>(Shadow);
        TestNotNull(TEXT("Shadow is a UImage"), ShadowImg);

        UWidget* Target = WBP->WidgetTree->FindWidget(FName(TEXT("Target")));
        if (Shadow && Target)
        {
            UPanelWidget* Parent = Target->GetParent();
            if (Parent)
            {
                const int32 ShadowIdx = Parent->GetChildIndex(Shadow);
                const int32 TargetIdx = Parent->GetChildIndex(Target);
                TestTrue(TEXT("Shadow inserted BEFORE Target in parent child list"),
                    ShadowIdx >= 0 && TargetIdx >= 0 && ShadowIdx < TargetIdx);
            }
        }

        if (ShadowImg)
        {
            UObject* Res = ShadowImg->GetBrush().GetResourceObject();
            TestNotNull(TEXT("Shadow brush has a resource object"), Res);
            TestTrue(TEXT("Shadow brush resource is a UMaterialInterface"),
                Res != nullptr && Res->IsA<UMaterialInterface>());
        }
    }
    return true;
}

/**
 * MonolithUI.ApplyBoxShadow.MultiLayerCap
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FMonolithUIApplyBoxShadowMultiLayerCapTest,
    "MonolithUI.ApplyBoxShadow.MultiLayerCap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMonolithUIApplyBoxShadowMultiLayerCapTest::RunTest(const FString& Parameters)
{
    using namespace MonolithUI::TestUtils;
    using namespace MonolithUI::ApplyBoxShadowTests;

    const FString ShadowMaterialPath = FindMaterialPathByAssetName(FName(TEXT("M_TokenShadow")));
    UMaterialInterface* ShadowParent = LoadObject<UMaterialInterface>(nullptr, *ShadowMaterialPath);
    if (!ShadowParent)
    {
        AddWarning(TEXT("Shadow material not present -- skipping multi-layer-cap test."));
        return true;
    }

    const FString WBPPath = TEXT("/Game/Tests/Monolith/UI/WBP_ShadowMultiTest");
    FString FixtureError;
    if (!CreateOrReuseTestWidgetBlueprint(WBPPath, FName(TEXT("Target")), nullptr, FixtureError))
    {
        AddError(FString::Printf(TEXT("Fixture build failed: %s"), *FixtureError));
        return false;
    }

    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("asset_path"), WBPPath);
    Params->SetStringField(TEXT("widget_name"), TEXT("Target"));
    Params->SetStringField(TEXT("shadow_material_path"), ShadowMaterialPath);
    {
        TArray<TSharedPtr<FJsonValue>> Shadows;
        for (int32 i = 0; i < 3; ++i)
        {
            TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
            S->SetNumberField(TEXT("x"), 0.0);
            S->SetNumberField(TEXT("y"), (double)(i + 1) * 2.0);
            S->SetNumberField(TEXT("blur"), (double)(i + 1) * 4.0);
            S->SetStringField(TEXT("color"), TEXT("#00000033"));
            Shadows.Add(MakeShared<FJsonValueObject>(S));
        }
        Params->SetArrayField(TEXT("shadows"), Shadows);
    }
    Params->SetBoolField(TEXT("compile"), false);

    const FMonolithActionResult Result = FMonolithToolRegistry::Get().ExecuteAction(
        TEXT("ui"), TEXT("apply_box_shadow"), Params);

    TestTrue(TEXT("apply_box_shadow bSuccess"), Result.bSuccess);
    if (!Result.bSuccess || !Result.Result.IsValid())
    {
        AddError(TEXT("Expected success payload for 3-layer shadow input"));
        return false;
    }

    double LayersApplied = 0.0;
    Result.Result->TryGetNumberField(TEXT("layers_applied"), LayersApplied);
    TestEqual(TEXT("layers_applied clamped to 2"), (int32)LayersApplied, 2);

    const TArray<TSharedPtr<FJsonValue>>* WarningsArr = nullptr;
    if (Result.Result->TryGetArrayField(TEXT("warnings"), WarningsArr) && WarningsArr)
    {
        bool bFoundCapWarning = false;
        for (const TSharedPtr<FJsonValue>& V : *WarningsArr)
        {
            FString S;
            if (V.IsValid() && V->TryGetString(S) && S.Contains(TEXT("only first 2 shadow layers applied")))
            {
                bFoundCapWarning = true;
                break;
            }
        }
        TestTrue(TEXT("warning 'only first 2 shadow layers applied' emitted"), bFoundCapWarning);
    }
    else
    {
        AddError(TEXT("Expected warnings array in result payload"));
    }
    return true;
}

/**
 * MonolithUI.ApplyBoxShadow.TargetIsRoot
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FMonolithUIApplyBoxShadowTargetIsRootTest,
    "MonolithUI.ApplyBoxShadow.TargetIsRoot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMonolithUIApplyBoxShadowTargetIsRootTest::RunTest(const FString& Parameters)
{
    using namespace MonolithUI::TestUtils;

    const FString WBPPath = TEXT("/Game/Tests/Monolith/UI/WBP_ShadowRootTest");
    FString FixtureError;
    if (!CreateOrReuseTestWidgetBlueprint(WBPPath, FName(TEXT("Target")), nullptr, FixtureError))
    {
        AddError(FString::Printf(TEXT("Fixture build failed: %s"), *FixtureError));
        return false;
    }

    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("asset_path"), WBPPath);
    Params->SetStringField(TEXT("widget_name"), TEXT("RootCanvas")); // root has no parent
    Params->SetStringField(TEXT("shadow_material_path"),
        TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial"));
    {
        TSharedPtr<FJsonObject> Shadow = MakeShared<FJsonObject>();
        Shadow->SetStringField(TEXT("color"), TEXT("#000000"));
        Params->SetObjectField(TEXT("shadow"), Shadow);
    }
    Params->SetBoolField(TEXT("compile"), false);

    const FMonolithActionResult Result = FMonolithToolRegistry::Get().ExecuteAction(
        TEXT("ui"), TEXT("apply_box_shadow"), Params);

    TestFalse(TEXT("root-widget target -> failure"), Result.bSuccess);
    TestEqual(TEXT("root-widget target -> -32603"), Result.ErrorCode, -32603);
    TestTrue(TEXT("error message mentions 'no parent panel'"),
        Result.ErrorMessage.Contains(TEXT("no parent panel")));
    return true;
}

/**
 * MonolithUI.ApplyBoxShadow.InvalidParams
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FMonolithUIApplyBoxShadowInvalidParamsTest,
    "MonolithUI.ApplyBoxShadow.InvalidParams",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMonolithUIApplyBoxShadowInvalidParamsTest::RunTest(const FString& Parameters)
{
    {
        TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
        const FMonolithActionResult R = FMonolithToolRegistry::Get().ExecuteAction(
            TEXT("ui"), TEXT("apply_box_shadow"), P);
        TestFalse(TEXT("empty params -> failure"), R.bSuccess);
        TestEqual(TEXT("empty params -> -32602"), R.ErrorCode, -32602);
    }

    {
        TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
        P->SetStringField(TEXT("asset_path"), TEXT("/Game/Foo/WBP_Bar"));
        const FMonolithActionResult R = FMonolithToolRegistry::Get().ExecuteAction(
            TEXT("ui"), TEXT("apply_box_shadow"), P);
        TestFalse(TEXT("missing widget_name -> failure"), R.bSuccess);
        TestEqual(TEXT("missing widget_name -> -32602"), R.ErrorCode, -32602);
    }

    {
        TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
        P->SetStringField(TEXT("asset_path"), TEXT("/Game/Foo/WBP_Bar"));
        P->SetStringField(TEXT("widget_name"), TEXT("Target"));
        const FMonolithActionResult R = FMonolithToolRegistry::Get().ExecuteAction(
            TEXT("ui"), TEXT("apply_box_shadow"), P);
        TestFalse(TEXT("missing shadow_material_path -> failure"), R.bSuccess);
        TestEqual(TEXT("missing shadow_material_path -> -32602"), R.ErrorCode, -32602);
    }

    {
        TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
        P->SetStringField(TEXT("asset_path"), TEXT("/Game/Foo/WBP_Bar"));
        P->SetStringField(TEXT("widget_name"), TEXT("Target"));
        P->SetStringField(TEXT("shadow_material_path"), TEXT("/Some/Mat"));
        TSharedPtr<FJsonObject> Single = MakeShared<FJsonObject>();
        Single->SetStringField(TEXT("color"), TEXT("#000"));
        P->SetObjectField(TEXT("shadow"), Single);
        TArray<TSharedPtr<FJsonValue>> Multi;
        TSharedPtr<FJsonObject> M0 = MakeShared<FJsonObject>();
        M0->SetStringField(TEXT("color"), TEXT("#FFF"));
        Multi.Add(MakeShared<FJsonValueObject>(M0));
        P->SetArrayField(TEXT("shadows"), Multi);
        const FMonolithActionResult R = FMonolithToolRegistry::Get().ExecuteAction(
            TEXT("ui"), TEXT("apply_box_shadow"), P);
        TestFalse(TEXT("both shadow+shadows -> failure"), R.bSuccess);
        TestEqual(TEXT("both shadow+shadows -> -32602"), R.ErrorCode, -32602);
    }

    {
        TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
        P->SetStringField(TEXT("asset_path"), TEXT("/Game/Foo/WBP_Bar"));
        P->SetStringField(TEXT("widget_name"), TEXT("Target"));
        P->SetStringField(TEXT("shadow_material_path"), TEXT("/Some/Mat"));
        const FMonolithActionResult R = FMonolithToolRegistry::Get().ExecuteAction(
            TEXT("ui"), TEXT("apply_box_shadow"), P);
        TestFalse(TEXT("no shadow spec -> failure"), R.bSuccess);
        TestEqual(TEXT("no shadow spec -> -32602"), R.ErrorCode, -32602);
    }

    {
        TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
        P->SetStringField(TEXT("asset_path"), TEXT("/Game/Foo/WBP_Bar"));
        P->SetStringField(TEXT("widget_name"), TEXT("Target"));
        P->SetStringField(TEXT("shadow_material_path"), TEXT("/Some/Mat"));
        TSharedPtr<FJsonObject> Shadow = MakeShared<FJsonObject>();
        Shadow->SetNumberField(TEXT("x"), 0.0);
        Shadow->SetNumberField(TEXT("blur"), 4.0);
        P->SetObjectField(TEXT("shadow"), Shadow);
        const FMonolithActionResult R = FMonolithToolRegistry::Get().ExecuteAction(
            TEXT("ui"), TEXT("apply_box_shadow"), P);
        TestFalse(TEXT("shadow missing color -> failure"), R.bSuccess);
        TestEqual(TEXT("shadow missing color -> -32602"), R.ErrorCode, -32602);
    }

    {
        TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
        P->SetStringField(TEXT("asset_path"), TEXT("/Game/Foo/WBP_Bar"));
        P->SetStringField(TEXT("widget_name"), TEXT("Target"));
        P->SetStringField(TEXT("shadow_material_path"), TEXT("/Some/Mat"));
        TArray<TSharedPtr<FJsonValue>> Empty;
        P->SetArrayField(TEXT("shadows"), Empty);
        const FMonolithActionResult R = FMonolithToolRegistry::Get().ExecuteAction(
            TEXT("ui"), TEXT("apply_box_shadow"), P);
        TestFalse(TEXT("empty shadows array -> failure"), R.bSuccess);
        TestEqual(TEXT("empty shadows array -> -32602"), R.ErrorCode, -32602);
    }
    return true;
}

/**
 * MonolithUI.ApplyBoxShadow.IncompatibleParent
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FMonolithUIApplyBoxShadowIncompatibleParentTest,
    "MonolithUI.ApplyBoxShadow.IncompatibleParent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMonolithUIApplyBoxShadowIncompatibleParentTest::RunTest(const FString& Parameters)
{
    using namespace MonolithUI::TestUtils;

    const FString ProbePath = TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial");
    UMaterialInterface* Probe = LoadObject<UMaterialInterface>(nullptr, *ProbePath);
    if (!Probe)
    {
        AddWarning(FString::Printf(
            TEXT("Skipping: probe engine material '%s' not loadable"), *ProbePath));
        return true;
    }

    const FString WBPPath = TEXT("/Game/Tests/Monolith/UI/WBP_ShadowIncompatTest");
    FString FixtureError;
    if (!CreateOrReuseTestWidgetBlueprint(WBPPath, FName(TEXT("Target")), nullptr, FixtureError))
    {
        AddError(FString::Printf(TEXT("Fixture build failed: %s"), *FixtureError));
        return false;
    }

    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("asset_path"), WBPPath);
    Params->SetStringField(TEXT("widget_name"), TEXT("Target"));
    Params->SetStringField(TEXT("shadow_material_path"), ProbePath);
    {
        TSharedPtr<FJsonObject> Shadow = MakeShared<FJsonObject>();
        Shadow->SetStringField(TEXT("color"), TEXT("#000"));
        Params->SetObjectField(TEXT("shadow"), Shadow);
    }
    Params->SetBoolField(TEXT("compile"), false);

    const FMonolithActionResult R = FMonolithToolRegistry::Get().ExecuteAction(
        TEXT("ui"), TEXT("apply_box_shadow"), Params);
    TestFalse(TEXT("incompatible parent -> failure"), R.bSuccess);
    TestEqual(TEXT("incompatible parent -> -32602"), R.ErrorCode, -32602);
    TestTrue(TEXT("error names ShadowColor or BlurRadius"),
        R.ErrorMessage.Contains(TEXT("ShadowColor")) || R.ErrorMessage.Contains(TEXT("BlurRadius")));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
