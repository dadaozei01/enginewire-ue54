// get_geometry_collection_info and configure_geometry_collection: what a Geometry Collection holds (pieces, clusters,
// levels, bounds) and the asset-level physics settings that UE 5.4 rebuilds safely from the details panel's own
// change path: damage thresholds and model, clustering, the collision shape of the first size bucket, convex overlap
// removal and simplification, mass, physical material and Nanite. Every value is checked before any is written, and
// one call is one undo step.
#include "Domains/Animation/McpAnimActionContext.h"
#include "Domains/Animation/McpAnimActionDecls.h"
#include "Domains/Animation/Physics/McpAnimGeometryCollection.h"

#if MCP_HAS_GC_FRACTURE
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionConvexPropertiesInterface.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "GameFramework/Actor.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "UObject/UnrealType.h"
#endif

namespace McpAnimationHandlers {
#if MCP_HAS_GC_FRACTURE
namespace McpGeometryCollection {
namespace SettingsDetail {
template <typename TEnum> struct TEnumName
{
    const TCHAR *Name;
    TEnum Value;
};

const TEnumName<EDamageModelTypeEnum> DamageModels[] = {
    {TEXT("user_defined"), EDamageModelTypeEnum::Chaos_Damage_Model_UserDefined_Damage_Threshold},
    {TEXT("material_strength"), EDamageModelTypeEnum::Chaos_Damage_Model_Material_Strength_And_Connectivity_DamageThreshold}};
const TEnumName<ECollisionTypeEnum> CollisionTypes[] = {{TEXT("implicit_implicit"), ECollisionTypeEnum::Chaos_Volumetric},
                                                        {TEXT("particle_implicit"), ECollisionTypeEnum::Chaos_Surface_Volumetric}};
const TEnumName<EImplicitTypeEnum> ImplicitTypes[] = {{TEXT("box"), EImplicitTypeEnum::Chaos_Implicit_Box},
                                                      {TEXT("sphere"), EImplicitTypeEnum::Chaos_Implicit_Sphere},
                                                      {TEXT("capsule"), EImplicitTypeEnum::Chaos_Implicit_Capsule},
                                                      {TEXT("level_set"), EImplicitTypeEnum::Chaos_Implicit_LevelSet},
                                                      {TEXT("none"), EImplicitTypeEnum::Chaos_Implicit_None},
                                                      {TEXT("convex"), EImplicitTypeEnum::Chaos_Implicit_Convex}};
const TEnumName<EConvexOverlapRemoval> OverlapRemovals[] = {{TEXT("none"), EConvexOverlapRemoval::None},
                                                            {TEXT("all"), EConvexOverlapRemoval::All},
                                                            {TEXT("only_clusters"), EConvexOverlapRemoval::OnlyClusters},
                                                            {TEXT("only_clusters_vs_clusters"),
                                                             EConvexOverlapRemoval::OnlyClustersVsClusters}};

template <typename TEnum, int32 N> const TCHAR *NameOf(const TEnumName<TEnum> (&Table)[N], TEnum Value)
{
    for (const TEnumName<TEnum> &Entry : Table)
    {
        if (Entry.Value == Value)
        {
            return Entry.Name;
        }
    }
    return TEXT("other");
}

template <typename TEnum, int32 N>
bool ReadEnum(const TSharedPtr<FJsonObject> &Payload, const TCHAR *Field, const TEnumName<TEnum> (&Table)[N],
              TOptional<TEnum> &Out, FString &OutError)
{
    FString Text;
    if (!Payload->TryGetStringField(Field, Text))
    {
        return true;
    }
    TArray<FString> Names;
    for (const TEnumName<TEnum> &Entry : Table)
    {
        if (Text.Equals(Entry.Name, ESearchCase::IgnoreCase))
        {
            Out = Entry.Value;
            return true;
        }
        Names.Add(Entry.Name);
    }
    OutError = FString::Printf(TEXT("%s '%s' is not one of %s"), Field, *Text, *FString::Join(Names, TEXT(", ")));
    return false;
}

bool ReadOptional(const TSharedPtr<FJsonObject> &Payload, const TCHAR *Field, TOptional<double> &Out, double Min,
                  double Max, FString &OutError)
{
    double Value = 0.0;
    if (!Payload->HasField(Field))
    {
        return true;
    }
    if (!ReadRange(Payload, Field, Value, Min, Max, OutError))
    {
        return false;
    }
    Out = Value;
    return true;
}

TOptional<bool> ReadFlag(const TSharedPtr<FJsonObject> &Payload, const TCHAR *Field)
{
    bool Value = false;
    return Payload->TryGetBoolField(Field, Value) ? TOptional<bool>(Value) : TOptional<bool>();
}

void SettingsInto(const UGeometryCollection *Asset, const TSharedPtr<FJsonObject> &Out)
{
    TArray<TSharedPtr<FJsonValue>> Thresholds;
    for (float Threshold : Asset->DamageThreshold)
    {
        Thresholds.Add(MakeShared<FJsonValueNumber>(Threshold));
    }
    Out->SetArrayField(TEXT("damageThresholds"), Thresholds);
    Out->SetBoolField(TEXT("useSizeSpecificDamageThreshold"), Asset->bUseSizeSpecificDamageThreshold);
    Out->SetStringField(TEXT("damageModel"), NameOf(DamageModels, Asset->DamageModel));
    Out->SetBoolField(TEXT("enableClustering"), Asset->EnableClustering);
    if (Asset->SizeSpecificData.Num() > 0 && Asset->SizeSpecificData[0].CollisionShapes.Num() > 0)
    {
        const FGeometryCollectionCollisionTypeData &Shape = Asset->SizeSpecificData[0].CollisionShapes[0];
        Out->SetStringField(TEXT("collisionType"), NameOf(CollisionTypes, Shape.CollisionType));
        Out->SetStringField(TEXT("implicitType"), NameOf(ImplicitTypes, Shape.ImplicitType));
        Out->SetNumberField(TEXT("collisionMarginFraction"), Shape.CollisionMarginFraction);
        Out->SetNumberField(TEXT("collisionObjectReductionPercentage"), Shape.CollisionObjectReductionPercentage);
    }
    if (const TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> Shared = Asset->GetGeometryCollection())
    {
        const FGeometryCollectionConvexPropertiesInterface::FConvexCreationProperties Convex = Shared->GetConvexProperties();
        Out->SetStringField(TEXT("convexRemoveOverlaps"), NameOf(OverlapRemovals, Convex.RemoveOverlaps));
        Out->SetNumberField(TEXT("convexOverlapShrinkPercent"), Convex.OverlapRemovalShrinkPercent);
        Out->SetNumberField(TEXT("convexFractionAllowRemove"), Convex.FractionRemove);
        Out->SetNumberField(TEXT("convexSimplificationThreshold"), Convex.SimplificationThreshold);
        Out->SetNumberField(TEXT("convexCanExceedFraction"), Convex.CanExceedFraction);
    }
    Out->SetBoolField(TEXT("massAsDensity"), Asset->bMassAsDensity);
    Out->SetNumberField(TEXT("mass"), Asset->Mass);
    Out->SetStringField(TEXT("physicsMaterialPath"), Asset->PhysicsMaterial ? Asset->PhysicsMaterial->GetPathName() : FString());
    Out->SetBoolField(TEXT("enableNanite"), Asset->EnableNanite);
    Out->SetNumberField(TEXT("geometrySourceCount"), Asset->GeometrySource.Num());
}

// The details panel's change notification for one property, which rebuilds what depends on it.
void NotifyChanged(UGeometryCollection *Asset, FName Property)
{
    if (FProperty *Changed = FindFProperty<FProperty>(UGeometryCollection::StaticClass(), Property))
    {
        FPropertyChangedEvent Event(Changed, EPropertyChangeType::ValueSet);
        Asset->PostEditChangeProperty(Event);
    }
}
} // namespace SettingsDetail
} // namespace McpGeometryCollection
#endif

bool HandleAnimationGetGeometryCollectionInfoAction(FActionContext &Context, const TSharedPtr<FJsonObject> &Payload)
{
#if MCP_HAS_GC_FRACTURE
    UGeometryCollection *Asset = McpGeometryCollection::ResolveCollection(Context, Payload);
    if (!Asset)
    {
        return false;
    }
    TSharedPtr<FJsonObject> &Resp = Context.Resp;
    Resp->SetStringField(TEXT("geometryCollectionPath"), Asset->GetPathName());
    McpGeometryCollection::DescribeCollectionInto(Asset, Resp);
    McpGeometryCollection::SettingsDetail::SettingsInto(Asset, Resp);
    Context.bSuccess = true;
    Context.Message = FString::Printf(TEXT("%s: %d pieces, %d clusters, %d levels."), *Asset->GetName(),
                                      static_cast<int32>(Resp->GetNumberField(TEXT("pieceCount"))),
                                      static_cast<int32>(Resp->GetNumberField(TEXT("clusterCount"))),
                                      static_cast<int32>(Resp->GetNumberField(TEXT("levelCount"))));
    return false;
#else
    return McpGeometryCollection::RefuseUnsupported(Context);
#endif
}

bool HandleAnimationConfigureGeometryCollectionAction(FActionContext &Context, const TSharedPtr<FJsonObject> &Payload)
{
#if MCP_HAS_GC_FRACTURE
    using namespace McpGeometryCollection;
    using namespace McpGeometryCollection::SettingsDetail;
    UGeometryCollection *Asset = ResolveCollection(Context, Payload);
    if (!Asset)
    {
        return false;
    }

    FString Error;
    TOptional<TArray<float>> Thresholds;
    const TArray<TSharedPtr<FJsonValue>> *ThresholdValues = nullptr;
    if (Payload->TryGetArrayField(TEXT("damageThresholds"), ThresholdValues) && ThresholdValues)
    {
        TArray<float> Values;
        for (const TSharedPtr<FJsonValue> &Value : *ThresholdValues)
        {
            double Number = -1.0;
            if (!Value.IsValid() || !Value->TryGetNumber(Number) || !FMath::IsFinite(Number) || Number < 0.0)
            {
                Error = TEXT("damageThresholds takes numbers of 0 or more, one per cluster level from the root");
                break;
            }
            Values.Add(static_cast<float>(Number));
        }
        Thresholds = Values;
    }
    TOptional<EDamageModelTypeEnum> DamageModel;
    TOptional<ECollisionTypeEnum> CollisionType;
    TOptional<EImplicitTypeEnum> ImplicitType;
    TOptional<EConvexOverlapRemoval> RemoveOverlaps;
    TOptional<double> Margin, Reduction, Shrink, FractionRemove, Simplification, CanExceed, Mass;
    const TOptional<bool> SizeSpecific = ReadFlag(Payload, TEXT("useSizeSpecificDamageThreshold"));
    const TOptional<bool> Clustering = ReadFlag(Payload, TEXT("enableClustering"));
    const TOptional<bool> MassAsDensity = ReadFlag(Payload, TEXT("massAsDensity"));
    const TOptional<bool> Nanite = ReadFlag(Payload, TEXT("enableNanite"));
    FString MaterialPath;
    const bool bHasMaterial = Payload->TryGetStringField(TEXT("physicsMaterialPath"), MaterialPath);
    UPhysicalMaterial *Material = nullptr;
    if (!Error.IsEmpty() || !ReadEnum(Payload, TEXT("damageModel"), DamageModels, DamageModel, Error) ||
        !ReadEnum(Payload, TEXT("collisionType"), CollisionTypes, CollisionType, Error) ||
        !ReadEnum(Payload, TEXT("implicitType"), ImplicitTypes, ImplicitType, Error) ||
        !ReadEnum(Payload, TEXT("convexRemoveOverlaps"), OverlapRemovals, RemoveOverlaps, Error) ||
        !ReadOptional(Payload, TEXT("collisionMarginFraction"), Margin, 0.0, 1.0, Error) ||
        !ReadOptional(Payload, TEXT("collisionObjectReductionPercentage"), Reduction, 0.0, 100.0, Error) ||
        !ReadOptional(Payload, TEXT("convexOverlapShrinkPercent"), Shrink, 0.0, 99.9, Error) ||
        !ReadOptional(Payload, TEXT("convexFractionAllowRemove"), FractionRemove, 0.01, 1.0, Error) ||
        !ReadOptional(Payload, TEXT("convexSimplificationThreshold"), Simplification, 0.0, 100000.0, Error) ||
        !ReadOptional(Payload, TEXT("convexCanExceedFraction"), CanExceed, 0.0, 100.0, Error) ||
        !ReadOptional(Payload, TEXT("mass"), Mass, 0.0, 1000000000.0, Error))
    {
        Context.Fail(TEXT("INVALID_ARGUMENT"), Error + TEXT(". Nothing was changed."));
        return false;
    }
    if (bHasMaterial && !MaterialPath.IsEmpty())
    {
        Material = LoadObject<UPhysicalMaterial>(nullptr, *MaterialPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
        if (!Material)
        {
            Context.Fail(TEXT("ASSET_NOT_FOUND"),
                         FString::Printf(TEXT("%s is not a physical material. Nothing was changed."), *MaterialPath));
            return false;
        }
    }
    const bool bShape = CollisionType || ImplicitType || Margin || Reduction;
    const bool bConvex = RemoveOverlaps || Shrink || FractionRemove || Simplification || CanExceed;
    if (!Thresholds && !SizeSpecific && !DamageModel && !Clustering && !bShape && !bConvex && !MassAsDensity && !Mass &&
        !bHasMaterial && !Nanite)
    {
        Context.Fail(TEXT("INVALID_ARGUMENT"), TEXT("Name at least one setting to change. Nothing was changed."));
        return false;
    }

    // A placed actor's component keeps its own copy of the damage settings and physical material, taken from the asset
    // when it was given the asset; changing the asset leaves it as it was, so the actor breaks as before. Only actors
    // the call names take the new values: the actorName the asset was found through, and applyToActors.
    TArray<FString> TargetNames;
    FString NamedActor, NamedPath;
    if (Payload->TryGetStringField(TEXT("actorName"), NamedActor) && !NamedActor.TrimStartAndEnd().IsEmpty() &&
        !(Payload->TryGetStringField(TEXT("geometryCollectionPath"), NamedPath) && !NamedPath.TrimStartAndEnd().IsEmpty()))
    {
        TargetNames.Add(NamedActor.TrimStartAndEnd());
    }
    const TArray<TSharedPtr<FJsonValue>> *ApplyValues = nullptr;
    if (Payload->TryGetArrayField(TEXT("applyToActors"), ApplyValues) && ApplyValues)
    {
        for (const TSharedPtr<FJsonValue> &Value : *ApplyValues)
        {
            FString Name;
            if (Value.IsValid() && Value->TryGetString(Name) && !Name.TrimStartAndEnd().IsEmpty())
            {
                TargetNames.AddUnique(Name.TrimStartAndEnd());
            }
        }
    }
    TArray<UGeometryCollectionComponent *> Targets;
    for (const FString &Name : TargetNames)
    {
        AActor *Actor = Context.FindActorByName(Name);
        UGeometryCollectionComponent *Component = nullptr;
        if (Actor)
        {
            TArray<UGeometryCollectionComponent *> Found;
            Actor->GetComponents(Found);
            for (UGeometryCollectionComponent *Candidate : Found)
            {
                Component = Candidate->GetRestCollection() == Asset ? Candidate : Component;
            }
        }
        if (!Component)
        {
            Context.Fail(Actor ? TEXT("INVALID_ARGUMENT") : TEXT("ACTOR_NOT_FOUND"),
                         Actor ? FString::Printf(TEXT("'%s' does not draw %s. Nothing was changed."), *Name,
                                                 *Asset->GetName())
                               : FString::Printf(TEXT("No actor named '%s' in the level. Nothing was changed."), *Name));
            return false;
        }
        Targets.AddUnique(Component);
    }
    const bool bDamageSettings = Thresholds || SizeSpecific || DamageModel || bHasMaterial;

    TArray<TSharedPtr<FJsonValue>> Applied;
    TArray<TSharedPtr<FJsonValue>> UpdatedActors;
    const auto Note = [&Applied](const TCHAR *Field) { Applied.Add(MakeShared<FJsonValueString>(Field)); };
    EditCollection(Context, Asset, TEXT("Configure Geometry Collection"), false, [&](FGeometryCollection &Collection) {
        if (Thresholds)
        {
            Asset->DamageThreshold = *Thresholds;
            NotifyChanged(Asset, GET_MEMBER_NAME_CHECKED(UGeometryCollection, DamageThreshold));
            Note(TEXT("damageThresholds"));
        }
        if (SizeSpecific)
        {
            Asset->bUseSizeSpecificDamageThreshold = *SizeSpecific;
            NotifyChanged(Asset, GET_MEMBER_NAME_CHECKED(UGeometryCollection, bUseSizeSpecificDamageThreshold));
            Note(TEXT("useSizeSpecificDamageThreshold"));
        }
        if (DamageModel)
        {
            Asset->DamageModel = *DamageModel;
            NotifyChanged(Asset, GET_MEMBER_NAME_CHECKED(UGeometryCollection, DamageModel));
            Note(TEXT("damageModel"));
        }
        if (Clustering)
        {
            Asset->EnableClustering = *Clustering;
            NotifyChanged(Asset, GET_MEMBER_NAME_CHECKED(UGeometryCollection, EnableClustering));
            Note(TEXT("enableClustering"));
        }
        if (bShape)
        {
            if (Asset->SizeSpecificData.Num() == 0)
            {
                Asset->SizeSpecificData.Add(FGeometryCollectionSizeSpecificData());
            }
            TArray<FGeometryCollectionCollisionTypeData> &Shapes = Asset->SizeSpecificData[0].CollisionShapes;
            if (Shapes.Num() == 0)
            {
                Shapes.Add(FGeometryCollectionCollisionTypeData());
            }
            FGeometryCollectionCollisionTypeData &Shape = Shapes[0];
            if (CollisionType)
            {
                Shape.CollisionType = *CollisionType;
                Note(TEXT("collisionType"));
            }
            if (ImplicitType)
            {
                Shape.ImplicitType = *ImplicitType;
                Note(TEXT("implicitType"));
            }
            if (Margin)
            {
                Shape.CollisionMarginFraction = static_cast<float>(*Margin);
                Note(TEXT("collisionMarginFraction"));
            }
            if (Reduction)
            {
                Shape.CollisionObjectReductionPercentage = static_cast<float>(*Reduction);
                Note(TEXT("collisionObjectReductionPercentage"));
            }
            // Revalidates the size buckets, recomputes convex hulls and rebuilds simulation data.
            NotifyChanged(Asset, GET_MEMBER_NAME_CHECKED(UGeometryCollection, SizeSpecificData));
        }
        if (bConvex)
        {
            FGeometryCollectionConvexPropertiesInterface::FConvexCreationProperties Convex = Collection.GetConvexProperties();
            if (RemoveOverlaps)
            {
                Convex.RemoveOverlaps = *RemoveOverlaps;
                Note(TEXT("convexRemoveOverlaps"));
            }
            if (Shrink)
            {
                Convex.OverlapRemovalShrinkPercent = static_cast<float>(*Shrink);
                Note(TEXT("convexOverlapShrinkPercent"));
            }
            if (FractionRemove)
            {
                Convex.FractionRemove = static_cast<float>(*FractionRemove);
                Note(TEXT("convexFractionAllowRemove"));
            }
            if (Simplification)
            {
                Convex.SimplificationThreshold = static_cast<float>(*Simplification);
                Note(TEXT("convexSimplificationThreshold"));
            }
            if (CanExceed)
            {
                Convex.CanExceedFraction = static_cast<float>(*CanExceed);
                Note(TEXT("convexCanExceedFraction"));
            }
            Collection.SetConvexProperties(Convex);
            Asset->UpdateConvexGeometry();
            Asset->CreateSimulationData();
        }
        if (MassAsDensity)
        {
            Asset->bMassAsDensity = *MassAsDensity;
            NotifyChanged(Asset, GET_MEMBER_NAME_CHECKED(UGeometryCollection, bMassAsDensity));
            Note(TEXT("massAsDensity"));
        }
        if (Mass)
        {
            Asset->Mass = static_cast<float>(*Mass);
            NotifyChanged(Asset, GET_MEMBER_NAME_CHECKED(UGeometryCollection, Mass));
            Note(TEXT("mass"));
        }
        if (bHasMaterial)
        {
            Asset->PhysicsMaterial = Material;
            NotifyChanged(Asset, GET_MEMBER_NAME_CHECKED(UGeometryCollection, PhysicsMaterial));
            Note(TEXT("physicsMaterialPath"));
        }
        if (Nanite)
        {
            Asset->EnableNanite = *Nanite;
            NotifyChanged(Asset, GET_MEMBER_NAME_CHECKED(UGeometryCollection, EnableNanite));
            Note(TEXT("enableNanite"));
        }
        for (UGeometryCollectionComponent *Target : Targets)
        {
            Target->ApplyAssetDefaults();
            Target->MarkPackageDirty();
            UpdatedActors.Add(MakeShared<FJsonValueString>(Target->GetOwner()->GetActorLabel()));
        }
        return Applied.Num() > 0;
    });
    SaveInto(Context, Asset);

    TSharedPtr<FJsonObject> &Resp = Context.Resp;
    Resp->SetStringField(TEXT("geometryCollectionPath"), Asset->GetPathName());
    Resp->SetArrayField(TEXT("applied"), Applied);
    SettingsInto(Asset, Resp);
    Resp->SetArrayField(TEXT("updatedActors"), UpdatedActors);
    TArray<FString> Stale;
    for (UGeometryCollectionComponent *User : ComponentsUsing(Asset))
    {
        if (!Targets.Contains(User))
        {
            Stale.Add(User->GetOwner()->GetActorLabel());
        }
    }
    if (bDamageSettings && Stale.Num() > 0)
    {
        TArray<TSharedPtr<FJsonValue>> StaleValues;
        for (const FString &Label : Stale)
        {
            StaleValues.Add(MakeShared<FJsonValueString>(Label));
        }
        Resp->SetArrayField(TEXT("placedActorsNotUpdated"), StaleValues);
        Resp->SetArrayField(TEXT("warnings"),
                            {MakeShared<FJsonValueString>(FString::Printf(
                                TEXT("%s keep their own damage settings and physical material from when they were "
                                     "placed, so they break as before; name them in applyToActors to give them the "
                                     "asset's new values."),
                                *FString::Join(Stale, TEXT(", "))))});
    }
    Context.bSuccess = true;
    Context.Message = FString::Printf(TEXT("Set %d Geometry Collection setting(s) on %s."), Applied.Num(), *Asset->GetName());
    return false;
#else
    return McpGeometryCollection::RefuseUnsupported(Context);
#endif
}
} // namespace McpAnimationHandlers
