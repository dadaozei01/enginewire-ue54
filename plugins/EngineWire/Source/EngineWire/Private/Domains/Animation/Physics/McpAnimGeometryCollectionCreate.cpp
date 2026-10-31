// create_geometry_collection: Fracture Mode's New. One or more static meshes become a new Geometry Collection asset,
// one rigid piece per mesh under a single root, with the project's Fracture Mode convex and proximity defaults, and,
// on request, an actor in the level that draws it. An asset already at the path is never rebuilt: cutting a
// collection that was already cut multiplies its pieces, so every fracture starts from one made here.
#include "Domains/Animation/McpAnimActionContext.h"
#include "Domains/Animation/McpAnimActionDecls.h"
#include "Domains/Animation/Physics/McpAnimGeometryCollection.h"

#if MCP_HAS_GC_FRACTURE
#include "Domains/ControlActor/McpActorSupport.h"
#include "Foundation/BridgeHelpers/Assets/McpAssetCreation.h"
#include "Foundation/HandlerUtils/McpHandlerUtils.h"
#include "Foundation/HandlerUtils/McpHandlerUtilsTransforms.h"
#include "Foundation/McpScopedEditorTransaction.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionActor.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionConvexPropertiesInterface.h"
#include "GeometryCollection/GeometryCollectionEngineConversion.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/GeometryCollectionProximityPropertiesInterface.h"
#include "GeometryCollection/GeometryCollectionUtility.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#endif

namespace McpAnimationHandlers {
#if MCP_HAS_GC_FRACTURE
namespace McpGeometryCollection {
namespace CreateDetail {
// Fracture Mode's New prompts before converting more than this many triangles; here the caller opts in instead.
constexpr int32 LargeInputTriangles = 1000000;

// The Fracture Mode project settings (Project Settings > Plugins > Fracture Mode) that New copies onto every new
// collection. They live in the FractureEditor module, read here by reflection so the bridge links nothing of it; a
// missing setting keeps the engine's default. Numbers, bools and enums all read as a number.
double ModeSetting(const TCHAR *Name, double Default)
{
    const UClass *Class = FindObject<UClass>(nullptr, TEXT("/Script/FractureEditor.FractureModeSettings"));
    const UObject *Settings = Class ? Class->GetDefaultObject() : nullptr;
    const FProperty *Property = Settings ? Class->FindPropertyByName(Name) : nullptr;
    const FEnumProperty *Enum = CastField<FEnumProperty>(Property);
    const FNumericProperty *Numeric = Enum ? Enum->GetUnderlyingProperty() : CastField<FNumericProperty>(Property);
    if (Numeric)
    {
        const void *Value = Property->ContainerPtrToValuePtr<void>(Settings);
        return Numeric->IsFloatingPoint() ? Numeric->GetFloatingPointPropertyValue(Value)
                                          : static_cast<double>(Numeric->GetSignedIntPropertyValue(Value));
    }
    if (const FBoolProperty *Bool = CastField<FBoolProperty>(Property))
    {
        return Bool->GetPropertyValue_InContainer(Settings) ? 1.0 : 0.0;
    }
    return Default;
}

template <typename TEnum> TEnum ModeEnum(const TCHAR *Name, TEnum Default)
{
    return static_cast<TEnum>(static_cast<int64>(ModeSetting(Name, static_cast<double>(static_cast<int64>(Default)))));
}

void ApplyModeDefaults(FGeometryCollection &Collection)
{
    FGeometryCollectionConvexPropertiesInterface::FConvexCreationProperties Convex = Collection.GetConvexProperties();
    Convex.FractionRemove = static_cast<float>(ModeSetting(TEXT("ConvexFractionAllowRemove"), .5));
    Convex.SimplificationThreshold = static_cast<float>(ModeSetting(TEXT("ConvexSimplificationDistanceThreshold"), 10.0));
    Convex.CanExceedFraction = static_cast<float>(ModeSetting(TEXT("ConvexCanExceedFraction"), .5));
    Convex.RemoveOverlaps = ModeEnum(TEXT("ConvexRemoveOverlaps"), EConvexOverlapRemoval::All);
    Convex.OverlapRemovalShrinkPercent = static_cast<float>(ModeSetting(TEXT("ConvexOverlapRemovalShrinkPercent"), 0.0));
    Collection.SetConvexProperties(Convex);

    FGeometryCollectionProximityPropertiesInterface::FProximityProperties Proximity = Collection.GetProximityProperties();
    Proximity.bUseAsConnectionGraph = ModeSetting(TEXT("bProximityUseAsConnectionGraph"), 0.0) != 0.0;
    Proximity.DistanceThreshold = static_cast<float>(ModeSetting(TEXT("ProximityDistanceThreshold"), 1.0));
    Proximity.Method = ModeEnum(TEXT("ProximityMethod"), EProximityMethod::Precise);
    Proximity.ContactMethod =
        ModeEnum(TEXT("ProximityContactMethod"), EProximityContactMethod::MinOverlapInProjectionToMajorAxes);
    Proximity.ContactAreaMethod = ModeEnum(TEXT("ProximityConnectionContactAreaMethod"), EConnectionContactMethod::None);
    Proximity.RequireContactAmount = static_cast<float>(ModeSetting(TEXT("ProximityContactThreshold"), 0.0));
    Collection.SetProximityProperties(Proximity);
}

bool ReadVector(const TSharedPtr<FJsonObject> &Payload, const TCHAR *Field, FVector &Out, const TCHAR *X,
                const TCHAR *Y, const TCHAR *Z, FString &OutError)
{
    const TSharedPtr<FJsonObject> *Object = nullptr;
    if (!Payload->TryGetObjectField(Field, Object) || !Object)
    {
        return true;
    }
    double Values[3] = {0, 0, 0};
    const TCHAR *Names[3] = {X, Y, Z};
    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
        if ((*Object)->HasField(Names[Axis]) &&
            (!(*Object)->TryGetNumberField(Names[Axis], Values[Axis]) || !FMath::IsFinite(Values[Axis])))
        {
            OutError = FString::Printf(TEXT("%s.%s must be a number"), Field, Names[Axis]);
            return false;
        }
    }
    Out = FVector(Values[0], Values[1], Values[2]);
    return true;
}

// Spawns an actor that draws Asset, as New does, as one undo step. Returns its label, or empty with Context failed.
FString SpawnCollectionActor(FActionContext &Context, const TSharedPtr<FJsonObject> &Payload, UGeometryCollection *Asset)
{
    FVector Location = FVector::ZeroVector;
    FVector Euler = FVector::ZeroVector;
    FString Error;
    if (!ReadVector(Payload, TEXT("location"), Location, TEXT("x"), TEXT("y"), TEXT("z"), Error) ||
        !ReadVector(Payload, TEXT("rotation"), Euler, TEXT("pitch"), TEXT("yaw"), TEXT("roll"), Error))
    {
        Context.Fail(TEXT("INVALID_ARGUMENT"), Error + TEXT(". The asset was created; no actor was placed."));
        return FString();
    }
    UWorld *World = McpHandlerUtils::GetEditorWorld();
    if (!World)
    {
        Context.Fail(TEXT("NO_WORLD"), TEXT("No editor world is open. The asset was created; no actor was placed."));
        return FString();
    }
    FString Label;
    Payload->TryGetStringField(TEXT("actorLabel"), Label);
    Label.TrimStartAndEndInline();
    if (Label.IsEmpty())
    {
        Label = Asset->GetName();
    }
    bool bSimulate = true;
    const bool bHasSimulate = Payload->TryGetBoolField(TEXT("simulatePhysics"), bSimulate);

    FMcpScopedEditorTransaction Transaction(FText::FromString(TEXT("Add Geometry Collection Actor")),
                                            EMcpMutationDurability::EditorStateOnly, TArray<UObject *>());
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Params.ObjectFlags = RF_Transactional;
    bool bStarted = false;
    AGeometryCollectionActor *Actor = Cast<AGeometryCollectionActor>(McpSpawnActorNoting(
        World, AGeometryCollectionActor::StaticClass(), Location, FRotator(Euler.X, Euler.Y, Euler.Z), Params, bStarted));
    UGeometryCollectionComponent *Component = Actor ? Actor->GetGeometryCollectionComponent() : nullptr;
    if (!Component)
    {
        Context.Fail(*McpSpawnFailureCode(bStarted),
                     McpSpawnFailureMessage(bStarted, TEXT("The Geometry Collection actor could not be spawned")) +
                         TEXT(". The asset was created; no actor was placed."));
        return FString();
    }
    Component->SetRestCollection(Asset);
    Component->SetPhysMaterialOverride(GEngine->DefaultDestructiblePhysMaterial);
    // The flag only, as the details panel's Simulate Physics box sets it; Play starts the simulation. In the editor
    // world SetSimulatePhysics(true) registers a physics proxy at once, which crashed the editor (SIGSEGV in
    // FGeometryCollectionPhysicsProxy::InitializeDynamicCollection) on a component that had just been given its asset.
    if (bHasSimulate)
    {
        Component->BodyInstance.bSimulatePhysics = bSimulate;
    }
    Actor->SetActorLabel(Label);
    Actor->InvalidateLightingCache();
    Actor->PostEditMove(true);
    Actor->MarkPackageDirty();
    Transaction.DescribeInto(Context.Resp);
    Context.Resp->SetBoolField(TEXT("simulatePhysics"), Component->BodyInstance.bSimulatePhysics);
    return Actor->GetActorLabel();
}
} // namespace CreateDetail
} // namespace McpGeometryCollection
#endif

bool HandleAnimationCreateGeometryCollectionAction(FActionContext &Context, const TSharedPtr<FJsonObject> &Payload)
{
#if MCP_HAS_GC_FRACTURE
    using namespace McpGeometryCollection::CreateDetail;
    const TArray<TSharedPtr<FJsonValue>> *MeshValues = nullptr;
    if (!Payload->TryGetArrayField(TEXT("staticMeshPaths"), MeshValues) || !MeshValues || MeshValues->Num() == 0)
    {
        Context.Fail(TEXT("INVALID_ARGUMENT"), TEXT("staticMeshPaths needs at least one static mesh path. Nothing was created."));
        return false;
    }
    TArray<UStaticMesh *> Meshes;
    int64 Triangles = 0;
    for (const TSharedPtr<FJsonValue> &Value : *MeshValues)
    {
        FString MeshPath;
        UStaticMesh *Mesh = Value.IsValid() && Value->TryGetString(MeshPath)
                                ? LoadObject<UStaticMesh>(nullptr, *MeshPath, nullptr, LOAD_NoWarn | LOAD_Quiet)
                                : nullptr;
        if (!Mesh)
        {
            Context.Fail(TEXT("ASSET_NOT_FOUND"),
                         FString::Printf(TEXT("'%s' is not a static mesh. Nothing was created."), *MeshPath));
            return false;
        }
        Meshes.Add(Mesh);
        Triangles += Mesh->GetNumTriangles(0);
    }
    bool bAllowLarge = false;
    Payload->TryGetBoolField(TEXT("allowLargeInput"), bAllowLarge);
    if (Triangles > LargeInputTriangles && !bAllowLarge)
    {
        Context.Fail(TEXT("INPUT_TOO_LARGE"),
                     FString::Printf(TEXT("The meshes hold %lld triangles; converting more than %d can stall the "
                                          "editor. Send allowLargeInput true to go ahead. Nothing was created."),
                                     Triangles, LargeInputTriangles));
        return false;
    }

    FString Name, Folder, PackagePath, Error, Code;
    Payload->TryGetStringField(TEXT("name"), Name);
    Payload->TryGetStringField(TEXT("path"), Folder);
    Name.TrimStartAndEndInline();
    Folder.TrimStartAndEndInline();
    if (Name.IsEmpty())
    {
        Name = TEXT("GC_") + Meshes[0]->GetName();
    }
    if (Folder.IsEmpty())
    {
        // New's default asset location is the source mesh's folder.
        Folder = FPackageName::GetLongPackagePath(Meshes[0]->GetOutermost()->GetName());
    }
    if (!ValidateAssetCreationPath(Folder, Name, PackagePath, Error))
    {
        Context.Fail(TEXT("INVALID_PATH"), Error);
        return false;
    }
    UPackage *Package = McpCreatePackageForNewAsset(PackagePath, Error, Code);
    if (!Package)
    {
        Context.Fail(*Code, Error);
        return false;
    }
    bool bSplitComponents = false;
    Payload->TryGetBoolField(TEXT("splitComponents"), bSplitComponents);

    UGeometryCollection *Asset = NewObject<UGeometryCollection>(Package, FName(*FPackageName::GetShortName(PackagePath)),
                                                                RF_Transactional | RF_Public | RF_Standalone);
    if (Asset->SizeSpecificData.Num() == 0)
    {
        Asset->SizeSpecificData.Add(FGeometryCollectionSizeSpecificData());
    }
    TArray<TSharedPtr<FJsonValue>> Sources;
    for (UStaticMesh *Mesh : Meshes)
    {
        TArray<TObjectPtr<UMaterialInterface>> SourceMaterials;
        TArray<UMaterialInterface *> Materials;
        for (const FStaticMaterial &Slot : Mesh->GetStaticMaterials())
        {
            SourceMaterials.Add(Slot.MaterialInterface);
            Materials.Add(Slot.MaterialInterface);
        }
        // Recorded on the asset so Fracture Mode's Reset can rebuild it from its sources.
        Asset->GeometrySource.Emplace(FSoftObjectPath(Mesh), FTransform::Identity, SourceMaterials, bSplitComponents, false);
        FGeometryCollectionEngineConversion::AppendStaticMesh(Mesh, Materials, FTransform::Identity, Asset, false, false,
                                                             bSplitComponents, false);
        Asset->EnableNanite |= Mesh->IsNaniteEnabled();
        Sources.Add(MakeShared<FJsonValueString>(Mesh->GetPathName()));
    }
    Asset->InitializeMaterials(false);
    if (FGeometryCollection *Collection = Asset->GetGeometryCollection().Get())
    {
        if (FGeometryCollectionClusteringUtility::ContainsMultipleRootBones(Collection))
        {
            FGeometryCollectionClusteringUtility::ClusterAllBonesUnderNewRoot(Collection);
        }
        ::GeometryCollection::GenerateTemporaryGuids(Collection, 0, true);
        ApplyModeDefaults(*Collection);
    }
    // What a component's edit scope does on leaving: convex hulls, proximity and volumes from the new shape.
    Asset->UpdateGeometryDependentProperties();
    Asset->InvalidateCollection();
    Asset->RebuildRenderData();
    FAssetRegistryModule::AssetCreated(Asset);
    Asset->MarkPackageDirty();
    McpGeometryCollection::SaveInto(Context, Asset);

    TSharedPtr<FJsonObject> &Resp = Context.Resp;
    Resp->SetStringField(TEXT("geometryCollectionPath"), Asset->GetPathName());
    Resp->SetArrayField(TEXT("sourceMeshes"), Sources);
    McpGeometryCollection::DescribeCollectionInto(Asset, Resp);
    McpHandlerUtils::AddVerification(Resp, Asset);

    bool bSpawn = false;
    Payload->TryGetBoolField(TEXT("spawnActor"), bSpawn);
    if (bSpawn)
    {
        const FString Label = SpawnCollectionActor(Context, Payload, Asset);
        if (Label.IsEmpty())
        {
            return false;
        }
        Resp->SetStringField(TEXT("actorName"), Label);
    }
    Context.bSuccess = true;
    Context.Message = FString::Printf(TEXT("Created %s from %d static mesh(es): %d piece(s)%s."), *Asset->GetName(),
                                      Meshes.Num(), static_cast<int32>(Resp->GetNumberField(TEXT("pieceCount"))),
                                      bSpawn ? TEXT(", placed in the level") : TEXT(""));
    return false;
#else
    return McpGeometryCollection::RefuseUnsupported(Context);
#endif
}
} // namespace McpAnimationHandlers
