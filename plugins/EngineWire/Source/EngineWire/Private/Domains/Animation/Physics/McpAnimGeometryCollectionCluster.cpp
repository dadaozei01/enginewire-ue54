// cluster_geometry_collection: Fracture Mode's Auto Cluster. The children of each chosen cluster (the roots by default)
// are grouped into new clusters one level down, by count, fraction of the pieces, piece size or a grid, so a hit
// breaks off a group before the single pieces. One undo step; saved after.
#include "Domains/Animation/McpAnimActionContext.h"
#include "Domains/Animation/McpAnimActionDecls.h"
#include "Domains/Animation/Physics/McpAnimGeometryCollection.h"

#if MCP_HAS_GC_FRACTURE
#include "FractureEngineClustering.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/GeometryCollectionUtility.h"
#endif

namespace McpAnimationHandlers {
bool HandleAnimationClusterGeometryCollectionAction(FActionContext &Context, const TSharedPtr<FJsonObject> &Payload)
{
#if MCP_HAS_GC_FRACTURE
    using namespace McpGeometryCollection;
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
    const int32 NumTransforms = Shared.IsValid() ? Shared->NumElements(FGeometryCollection::TransformGroup) : 0;
    if (NumTransforms == 0)
    {
        Context.Fail(TEXT("EMPTY_COLLECTION"), FString::Printf(TEXT("%s holds no geometry."), *Asset->GetName()));
        return false;
    }

    FString Method = TEXT("by_number"), Error;
    Payload->TryGetStringField(TEXT("clusterSizeMethod"), Method);
    struct FMethodName
    {
        const TCHAR *Name;
        EFractureEngineClusterSizeMethod Value;
    };
    static const FMethodName Methods[] = {{TEXT("by_number"), EFractureEngineClusterSizeMethod::ByNumber},
                                          {TEXT("by_fraction"), EFractureEngineClusterSizeMethod::ByFractionOfInput},
                                          {TEXT("by_size"), EFractureEngineClusterSizeMethod::BySize},
                                          {TEXT("by_grid"), EFractureEngineClusterSizeMethod::ByGrid}};
    const FMethodName *Chosen = nullptr;
    for (const FMethodName &Candidate : Methods)
    {
        Chosen = Method.Equals(Candidate.Name, ESearchCase::IgnoreCase) ? &Candidate : Chosen;
    }
    // Auto Cluster's defaults.
    int32 SiteCount = 10, GridX = 2, GridY = 2, GridZ = 2, DriftIterations = 0;
    double Fraction = .25, SiteSize = 1.0, MinimumSize = 0.0, Concavity = 0.0;
    bool bConnectivity = true, bAvoidIsolated = true, bEnforceSiteParameters = true, bPreferConvexity = false;
    Payload->TryGetBoolField(TEXT("enforceConnectivity"), bConnectivity);
    Payload->TryGetBoolField(TEXT("avoidIsolated"), bAvoidIsolated);
    Payload->TryGetBoolField(TEXT("enforceSiteParameters"), bEnforceSiteParameters);
    Payload->TryGetBoolField(TEXT("preferConvexity"), bPreferConvexity);
    TArray<int32> Requested;
    if (!Chosen)
    {
        Error = FString::Printf(TEXT("clusterSizeMethod '%s' is not by_number, by_fraction, by_size or by_grid"), *Method);
    }
    if (!Chosen || !ReadWhole(Payload, TEXT("siteCount"), SiteCount, 1, 5000, Error) ||
        !ReadRange(Payload, TEXT("siteCountFraction"), Fraction, 0.0, 1.0, Error) ||
        !ReadRange(Payload, TEXT("siteSize"), SiteSize, 0.0001, 1000000.0, Error) ||
        !ReadWhole(Payload, TEXT("gridX"), GridX, 1, 64, Error) || !ReadWhole(Payload, TEXT("gridY"), GridY, 1, 64, Error) ||
        !ReadWhole(Payload, TEXT("gridZ"), GridZ, 1, 64, Error) ||
        !ReadRange(Payload, TEXT("minimumClusterSize"), MinimumSize, 0.0, 1000000.0, Error) ||
        !ReadWhole(Payload, TEXT("driftIterations"), DriftIterations, 0, 1000, Error) ||
        !ReadRange(Payload, TEXT("concavityTolerance"), Concavity, 0.0, 1000000.0, Error) ||
        !ReadIndexArray(Payload, TEXT("clusterBones"), NumTransforms, Requested, Error))
    {
        Context.Fail(TEXT("INVALID_ARGUMENT"), Error + TEXT(". Nothing was changed."));
        return false;
    }

    // Auto Cluster works on clusters: a piece named stands for the cluster that holds it.
    TArray<int32> Clusters;
    for (int32 Bone : Requested.Num() > 0 ? Requested : RootBones(*Shared))
    {
        const int32 Cluster = Shared->Children[Bone].Num() > 0 ? Bone : Shared->Parent[Bone];
        if (Cluster != FGeometryCollection::Invalid && Shared->Children[Cluster].Num() > 1)
        {
            Clusters.AddUnique(Cluster);
        }
    }
    if (Clusters.Num() == 0)
    {
        Context.Fail(TEXT("NOT_FRACTURED"),
                     FString::Printf(TEXT("%s has no cluster with two or more pieces to group; cut it first with "
                                          "fracture_geometry_collection. Nothing was changed."),
                                     *Asset->GetName()));
        return false;
    }

    TSharedPtr<FJsonObject> Before = MakeShared<FJsonObject>();
    DescribeCollectionInto(Asset, Before);
    const bool bGrid = Chosen->Value == EFractureEngineClusterSizeMethod::ByGrid;
    const bool bChanged = EditCollection(Context, Asset, TEXT("Auto Cluster"), false, [&](FGeometryCollection &Edit) {
        const int32 StartTransforms = Edit.NumElements(FGeometryCollection::TransformGroup);
        for (int32 Cluster : Clusters)
        {
            // As the tool: drift iterations only steer grid clustering, which convexity cannot be combined with.
            FFractureEngineClustering::AutoCluster(Edit, Cluster, Chosen->Value, static_cast<uint32>(SiteCount),
                                                   static_cast<float>(Fraction), static_cast<float>(SiteSize),
                                                   bConnectivity, bAvoidIsolated, bEnforceSiteParameters, GridX, GridY,
                                                   GridZ, static_cast<float>(MinimumSize), bGrid ? DriftIterations : 500,
                                                   bPreferConvexity && !bGrid, static_cast<float>(Concavity));
        }
        ::GeometryCollection::GenerateTemporaryGuids(&Edit, StartTransforms, true);
        FGeometryCollectionClusteringUtility::UpdateHierarchyLevelOfChildren(&Edit, -1);
        return Edit.NumElements(FGeometryCollection::TransformGroup) != StartTransforms;
    });

    TSharedPtr<FJsonObject> &Resp = Context.Resp;
    Resp->SetStringField(TEXT("geometryCollectionPath"), Asset->GetPathName());
    Resp->SetNumberField(TEXT("clustersGrouped"), Clusters.Num());
    Resp->SetNumberField(TEXT("clusterCountBefore"), Before->GetNumberField(TEXT("clusterCount")));
    Resp->SetNumberField(TEXT("levelCountBefore"), Before->GetNumberField(TEXT("levelCount")));
    DescribeCollectionInto(Asset, Resp);
    if (!bChanged)
    {
        Context.Fail(TEXT("NO_CLUSTERS_MADE"),
                     TEXT("Auto cluster made no new cluster with these settings (try fewer sites or another "
                          "clusterSizeMethod). Nothing was changed."));
        return false;
    }
    SaveInto(Context, Asset);
    Context.bSuccess = true;
    Context.Message = FString::Printf(TEXT("Auto clustered %s: %d clusters over %d levels (was %d over %d)."),
                                      *Asset->GetName(), static_cast<int32>(Resp->GetNumberField(TEXT("clusterCount"))),
                                      static_cast<int32>(Resp->GetNumberField(TEXT("levelCount"))),
                                      static_cast<int32>(Before->GetNumberField(TEXT("clusterCount"))),
                                      static_cast<int32>(Before->GetNumberField(TEXT("levelCount"))));
    return false;
#else
    return McpGeometryCollection::RefuseUnsupported(Context);
#endif
}
} // namespace McpAnimationHandlers
