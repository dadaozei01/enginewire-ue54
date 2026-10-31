// fracture_geometry_collection: Fracture Mode's Uniform Voronoi (random sites in the bounds of what is cut) or Custom
// Voronoi (sites the caller gives), with its Chance To Fracture, Group Fracture, grout and noise. UE 5.4's
// FFractureEngineFracturing::VoronoiFracture ignores chance and group fracture, so this follows the editor tool
// instead: the same site generation, bounds and PlanarCut call. A collection that was already cut is refused unless
// allowRefracture is set, and a cut whose estimated piece count passes maxPieces is refused before it runs, since
// either can leave the editor cutting for minutes.
#include "Domains/Animation/McpAnimActionContext.h"
#include "Domains/Animation/McpAnimActionDecls.h"
#include "Domains/Animation/Physics/McpAnimGeometryCollection.h"

#if MCP_HAS_GC_FRACTURE
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/GeometryCollectionUtility.h"
#include "Math/RandomStream.h"
#include "PlanarCut.h"
#include "Voronoi/Voronoi.h"
#endif

namespace McpAnimationHandlers {
#if MCP_HAS_GC_FRACTURE
namespace McpGeometryCollection {
namespace FractureDetail {
constexpr int32 MaxSites = 5000;

struct FCutContext
{
    TArray<int32> Bones;
    int32 Seed = 0;
    FBox Bounds = FBox(ForceInit);
};

bool ReadSites(const TSharedPtr<FJsonObject> &Payload, TArray<FVector> &Sites, FString &OutError)
{
    const TArray<TSharedPtr<FJsonValue>> *Values = nullptr;
    if (!Payload->TryGetArrayField(TEXT("sites"), Values) || !Values)
    {
        return true;
    }
    if (Values->Num() > MaxSites)
    {
        OutError = FString::Printf(TEXT("sites takes at most %d points"), MaxSites);
        return false;
    }
    for (const TSharedPtr<FJsonValue> &Value : *Values)
    {
        const TSharedPtr<FJsonObject> *Point = nullptr;
        double X = 0, Y = 0, Z = 0;
        if (!Value.IsValid() || !Value->TryGetObject(Point) || !Point || !(*Point)->TryGetNumberField(TEXT("x"), X) ||
            !(*Point)->TryGetNumberField(TEXT("y"), Y) || !(*Point)->TryGetNumberField(TEXT("z"), Z) ||
            !FMath::IsFinite(X) || !FMath::IsFinite(Y) || !FMath::IsFinite(Z))
        {
            OutError = TEXT("each entry of sites must be {x, y, z} in the asset's space");
            return false;
        }
        Sites.Emplace(X, Y, Z);
    }
    return true;
}

// The rigid pieces at or under Index: a cluster's are cut, not the cluster itself.
void CollectRigid(const FGeometryCollection &Collection, int32 Index, TArray<int32> &Out)
{
    if (Collection.SimulationType[Index] == FGeometryCollection::ESimulationTypes::FST_Rigid)
    {
        Out.AddUnique(Index);
        return;
    }
    for (int32 Child : Collection.Children[Index])
    {
        CollectRigid(Collection, Child, Out);
    }
}
} // namespace FractureDetail
} // namespace McpGeometryCollection
#endif

bool HandleAnimationFractureGeometryCollectionAction(FActionContext &Context, const TSharedPtr<FJsonObject> &Payload)
{
#if MCP_HAS_GC_FRACTURE
    using namespace McpGeometryCollection;
    using namespace McpGeometryCollection::FractureDetail;
    if (!RequireFracturePlugins(Context))
    {
        return false;
    }
    UGeometryCollection *Asset = ResolveCollection(Context, Payload);
    if (!Asset)
    {
        return false;
    }
    const TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> Shared = Asset->GetGeometryCollection();
    if (!Shared.IsValid() || Shared->NumElements(FGeometryCollection::TransformGroup) == 0)
    {
        Context.Fail(TEXT("EMPTY_COLLECTION"), FString::Printf(TEXT("%s holds no geometry to cut."), *Asset->GetName()));
        return false;
    }
    const FGeometryCollection &Collection = *Shared;
    const int32 NumTransforms = Collection.NumElements(FGeometryCollection::TransformGroup);

    // Every value is read and checked before anything is cut.
    FString Error;
    TArray<FVector> CustomSites;
    TArray<int32> Requested;
    int32 SiteCount = 0, SiteCountMax = 0, Octaves = 4, MaxPieces = 2000, Seed = -1;
    double Chance = 1.0, Grout = 0.0, Amplitude = 0.0, Frequency = 0.1, Persistence = 0.5, Lacunarity = 2.0;
    double PointSpacing = 10.0, CollisionSpacing = 0.0;
    bool bGroup = true, bAllowRefracture = false;
    Payload->TryGetBoolField(TEXT("groupFracture"), bGroup);
    Payload->TryGetBoolField(TEXT("allowRefracture"), bAllowRefracture);
    if (!ReadSites(Payload, CustomSites, Error) || !ReadWhole(Payload, TEXT("siteCount"), SiteCount, 1, MaxSites, Error) ||
        !ReadWhole(Payload, TEXT("siteCountMax"), SiteCountMax, 1, MaxSites, Error) ||
        !ReadWhole(Payload, TEXT("randomSeed"), Seed, -1, MAX_int32, Error) ||
        !ReadRange(Payload, TEXT("chanceToFracture"), Chance, 0.0, 1.0, Error) ||
        !ReadRange(Payload, TEXT("grout"), Grout, 0.0, 10000.0, Error) ||
        !ReadRange(Payload, TEXT("noiseAmplitude"), Amplitude, 0.0, 10000.0, Error) ||
        !ReadRange(Payload, TEXT("noiseFrequency"), Frequency, 0.00001, 100.0, Error) ||
        !ReadRange(Payload, TEXT("noisePersistence"), Persistence, 0.0, 1.0, Error) ||
        !ReadRange(Payload, TEXT("noiseLacunarity"), Lacunarity, 1.0, 8.0, Error) ||
        !ReadWhole(Payload, TEXT("noiseOctaves"), Octaves, 1, 8, Error) ||
        !ReadRange(Payload, TEXT("noisePointSpacing"), PointSpacing, 1.0, 10000.0, Error) ||
        !ReadRange(Payload, TEXT("collisionSampleSpacing"), CollisionSpacing, 0.0, 10000.0, Error) ||
        !ReadWhole(Payload, TEXT("maxPieces"), MaxPieces, 1, 50000, Error) ||
        !ReadIndexArray(Payload, TEXT("boneIndices"), NumTransforms, Requested, Error))
    {
        Context.Fail(TEXT("INVALID_ARGUMENT"), Error + TEXT(". Nothing was cut."));
        return false;
    }
    if (CustomSites.Num() == 0 && SiteCount == 0)
    {
        Context.Fail(TEXT("INVALID_ARGUMENT"), TEXT("siteCount (or a sites list) is required. Nothing was cut."));
        return false;
    }
    if (SiteCountMax != 0 && SiteCountMax < SiteCount)
    {
        Context.Fail(TEXT("INVALID_ARGUMENT"), TEXT("siteCountMax must be at least siteCount. Nothing was cut."));
        return false;
    }
    SiteCountMax = FMath::Max(SiteCountMax, SiteCount);
    if (Seed < 0)
    {
        Seed = FMath::Rand();
    }

    const int32 PiecesBefore = CountPieces(Collection);
    const int32 SourcePieces = FMath::Max(1, Asset->GeometrySource.Num());
    if (PiecesBefore > SourcePieces && !bAllowRefracture)
    {
        Context.Fail(TEXT("ALREADY_FRACTURED"),
                     FString::Printf(TEXT("%s is already cut into %d pieces; cutting it again multiplies them and can "
                                          "stall the editor. Make a new one with create_geometry_collection and cut "
                                          "that, or send allowRefracture true (with boneIndices for the pieces to "
                                          "cut). Nothing was cut."),
                                     *Asset->GetName(), PiecesBefore));
        return false;
    }

    // Fracture Mode's selection: the chosen bones (the roots by default) as their rigid pieces, thinned by
    // Chance To Fracture.
    TArray<int32> Selection;
    for (int32 Bone : Requested.Num() > 0 ? Requested : RootBones(Collection))
    {
        CollectRigid(Collection, Bone, Selection);
    }
    FRandomStream Thin(Seed);
    for (int32 Index = 0; Index < Selection.Num(); ++Index)
    {
        if (Thin.GetFraction() >= Chance)
        {
            Selection.RemoveAtSwap(Index--);
        }
    }
    if (Selection.Num() == 0)
    {
        Context.Fail(TEXT("NOTHING_TO_CUT"),
                     TEXT("No rigid piece was left to cut (chanceToFracture removed every one, or boneIndices named "
                          "none). Nothing was cut."));
        return false;
    }
    Selection.Sort();

    TArray<FTransform> Globals;
    GeometryCollectionAlgo::GlobalMatrices(Collection.Transform, Collection.Parent, Globals);
    const auto BoneBounds = [&Collection, &Globals](int32 Bone) {
        const int32 Geometry = Collection.TransformToGeometryIndex[Bone];
        return Geometry == INDEX_NONE ? FBox(ForceInit) : Collection.BoundingBox[Geometry].TransformBy(Globals[Bone]);
    };
    TArray<FCutContext> Contexts;
    if (bGroup)
    {
        FCutContext &Group = Contexts.AddDefaulted_GetRef();
        Group.Bones = Selection;
        Group.Seed = Seed;
        for (int32 Bone : Selection)
        {
            Group.Bounds += BoneBounds(Bone);
        }
    }
    else
    {
        for (int32 Bone : Selection)
        {
            FCutContext &Single = Contexts.AddDefaulted_GetRef();
            Single.Bones = {Bone};
            Single.Seed = Seed + Bone;
            Single.Bounds = BoneBounds(Bone);
        }
    }

    const int32 SitesPerCut = CustomSites.Num() > 0 ? CustomSites.Num() : SiteCountMax;
    const int64 Estimate = bGroup ? int64(PiecesBefore) + SitesPerCut
                                  : int64(PiecesBefore) - Selection.Num() + int64(Selection.Num()) * SitesPerCut;
    if (Estimate > MaxPieces)
    {
        Context.Fail(TEXT("TOO_MANY_PIECES"),
                     FString::Printf(TEXT("This cut would make about %lld pieces, over maxPieces %d. Use fewer sites, "
                                          "group fracture, or raise maxPieces. Nothing was cut."),
                                     Estimate, MaxPieces));
        return false;
    }

    // How far noise and grout can move a cut surface, which the Voronoi bounds are widened by.
    double MaxMovement = Grout;
    if (Amplitude > 0.0)
    {
        double Scaled = Amplitude;
        for (int32 Octave = 0; Octave < Octaves; ++Octave, Scaled *= Persistence)
        {
            MaxMovement += FMath::Abs(Scaled);
        }
    }
    TOptional<FNoiseSettings> Noise;
    if (Amplitude > 0.0)
    {
        FNoiseSettings Settings;
        Settings.Amplitude = static_cast<float>(Amplitude);
        Settings.Frequency = static_cast<float>(Frequency);
        Settings.Octaves = Octaves;
        Settings.PointSpacing = static_cast<float>(PointSpacing);
        Settings.Lacunarity = static_cast<float>(Lacunarity);
        Settings.Persistence = static_cast<float>(Persistence);
        Noise = Settings;
    }

    int32 SitesUsed = 0;
    const bool bChanged = EditCollection(Context, Asset, TEXT("Voronoi Fracture"), true, [&](FGeometryCollection &Edit) {
        bool bAny = false;
        for (const FCutContext &Cut : Contexts)
        {
            TArray<FVector> Sites = CustomSites;
            if (Sites.Num() == 0)
            {
                FRandomStream Stream(Cut.Seed);
                const FVector Extent = Cut.Bounds.Max - Cut.Bounds.Min;
                const int32 Count = Stream.RandRange(SiteCount, SiteCountMax);
                Sites.Reserve(Count);
                for (int32 Site = 0; Site < Count; ++Site)
                {
                    Sites.Emplace(Cut.Bounds.Min + FVector(Stream.FRand(), Stream.FRand(), Stream.FRand()) * Extent);
                }
            }
            SitesUsed += Sites.Num();
            FBox VoronoiBounds = Cut.Bounds;
            VoronoiBounds += FBox(Sites);
            VoronoiBounds = VoronoiBounds.ExpandBy(MaxMovement + KINDA_SMALL_NUMBER);
            FVoronoiDiagram Voronoi(Sites, VoronoiBounds, .1f);
            FPlanarCells Cells(Sites, Voronoi);
            Cells.InternalSurfaceMaterials.NoiseSettings = Noise;
            const int32 FirstNewGeometry = CutMultipleWithPlanarCells(Cells, Edit, Cut.Bones, Grout, CollisionSpacing,
                                                                      Cut.Seed, FTransform::Identity, true, true);
            if (FirstNewGeometry > INDEX_NONE)
            {
                ::GeometryCollection::GenerateTemporaryGuids(&Edit, FirstNewGeometry, true);
                bAny = true;
            }
        }
        FGeometryCollectionClusteringUtility::UpdateHierarchyLevelOfChildren(&Edit, -1);
        return bAny;
    });
    if (bChanged)
    {
        SaveInto(Context, Asset);
    }

    TSharedPtr<FJsonObject> &Resp = Context.Resp;
    Resp->SetStringField(TEXT("geometryCollectionPath"), Asset->GetPathName());
    Resp->SetNumberField(TEXT("piecesBefore"), PiecesBefore);
    Resp->SetNumberField(TEXT("bonesCut"), Selection.Num());
    Resp->SetNumberField(TEXT("sitesUsed"), SitesUsed);
    Resp->SetNumberField(TEXT("randomSeed"), Seed);
    DescribeCollectionInto(Asset, Resp);
    const int32 PiecesAfter = static_cast<int32>(Resp->GetNumberField(TEXT("pieceCount")));
    Resp->SetNumberField(TEXT("newPieces"), PiecesAfter - PiecesBefore);
    if (!bChanged)
    {
        Context.Fail(TEXT("NO_CUT"), TEXT("The Voronoi cells did not cut any piece (the sites may lie outside the "
                                          "geometry). Nothing was changed."));
        return false;
    }
    Context.bSuccess = true;
    Context.Message = FString::Printf(TEXT("Cut %d piece(s) of %s with %d Voronoi site(s): %d pieces now (was %d)."),
                                      Selection.Num(), *Asset->GetName(), SitesUsed, PiecesAfter, PiecesBefore);
    return false;
#else
    return McpGeometryCollection::RefuseUnsupported(Context);
#endif
}
} // namespace McpAnimationHandlers
