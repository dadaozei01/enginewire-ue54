#include "Domains/BlueprintGraph/Code/McpGraphCodeEmit.h"

namespace McpGraphCode
{
namespace Emit
{
void FDecompiler::WalkPure(const FPin& Pin, TSet<FKey>& Seen) const
{
    bool bHalted = false;
    const FPinRef From = Pin.Links.Num() > 0 ? FollowDataKnots(Pin.Links[0], bHalted) : FPinRef();
    if (From.Node == INDEX_NONE || bHalted)
    {
        return;
    }
    const FNode& Source = Graph.Nodes[From.Node];
    const auto IsExec = [](const FPin& Each) { return Each.bExec; };
    if (Source.Ins.ContainsByPredicate(IsExec) || Source.Outs.ContainsByPredicate(IsExec) || Seen.Contains(FKey(From.Node, From.Pin)))
    {
        return;
    }
    Seen.Add(FKey(From.Node, From.Pin));
    if (!IsMakeLiteral(Source.TypeId))
    {
        for (const FPin* In : DataIns(Source))
        {
            WalkPure(*In, Seen);
        }
    }
}

// Pure outputs that two or more nodes of this event read: bound once at the top, as Epic's reader does.
TSet<FKey> FDecompiler::SharedPure(int32 Entry) const
{
    TArray<int32> Queue = {Entry};
    TSet<int32> Seen;
    TMap<FKey, int32> Readers;
    for (int32 Head = 0; Head < Queue.Num(); ++Head)
    {
        const int32 At = Queue[Head];
        if (Seen.Contains(At))
        {
            continue;
        }
        Seen.Add(At);
        TSet<FKey> Keys;
        for (const FPin* In : DataIns(Graph.Nodes[At]))
        {
            WalkPure(*In, Keys);
        }
        for (const FKey& Key : Keys)
        {
            ++Readers.FindOrAdd(Key);
        }
        for (const FPin& Pin : Graph.Nodes[At].Outs)
        {
            if (Pin.bExec && Pin.Links.Num() > 0)
            {
                Queue.Add(Pin.Links[0].Node);
            }
        }
    }
    // Or that one statement reads in two places: printed inline at each, it was written back as one node per place (a
    // GetSlot read 15 times in one call became 15 calls, and each Random node rolled on its own).
    TSet<int32> Readable = Seen;
    for (const TPair<FKey, int32>& Pair : Readers)
    {
        Readable.Add(Pair.Key.Key);
    }
    TSet<FKey> Shared;
    for (const TPair<FKey, int32>& Pair : Readers)
    {
        int32 Reads = 0;
        for (const FPinRef& Link : Graph.Nodes[Pair.Key.Key].Outs[Pair.Key.Value].Links)
        {
            Reads += Readable.Contains(Link.Node) ? 1 : 0;
        }
        if (Pair.Value >= 2 || Reads >= 2 || !PrintsInline(Pair.Key))
        {
            Shared.Add(Pair.Key);
        }
    }
    // A node bound once with every output this event reads, so its outputs are one node, not a call per output.
    TSet<int32> SharedNodes;
    for (const FKey& Each : Shared)
    {
        SharedNodes.Add(Each.Key);
    }
    for (const TPair<FKey, int32>& Pair : Readers)
    {
        if (!ComponentFor(Graph.Nodes[Pair.Key.Key].TypeId, Graph.Nodes[Pair.Key.Key].Outs[Pair.Key.Value].Name) &&
            SharedNodes.Contains(Pair.Key.Key))
        {
            Shared.Add(Pair.Key);
        }
    }
    return Shared;
}

// An inline call names its node's first output: any other (GetSlot's Vel) read inline was written back as the first
// (Loc), the same text wired to another value. Such an output is bound instead, by its place: (bind (_ _vel) ...). A
// .x of a struct says its output, and an operator has one.
bool FDecompiler::PrintsInline(const FKey& Key) const
{
    const FNode& Node = Graph.Nodes[Key.Key];
    return Key.Value == 0 || ComponentFor(Node.TypeId, Node.Outs[Key.Value].Name) != nullptr;
}

// Dependencies before dependents, starting in GUID order so two reads of one graph print the same text.
TArray<FKey> FDecompiler::SortHoisted(const TSet<FKey>& Keys) const
{
    TArray<FKey> Starts = Keys.Array();
    Starts.Sort([this](const FKey& A, const FKey& B)
    {
        const int32 Order = Graph.Nodes[A.Key].Guid.Compare(Graph.Nodes[B.Key].Guid, ESearchCase::CaseSensitive);
        return Order != 0 ? Order < 0 : A.Value < B.Value;
    });
    // The hoisted values a node's text names: read directly, or through pure nodes printed inline in it. A bind printed
    // before one of them spelled that value out again in full, and the write made it twice (a Tick of 54 nodes became 64).
    const TFunction<void(int32, TSet<int32>&, TArray<FKey>&)> DepsOf = [this, &Keys, &DepsOf](int32 NodeIndex, TSet<int32>& Walked, TArray<FKey>& Out)
    {
        for (const FPin* In : DataIns(Graph.Nodes[NodeIndex]))
        {
            bool bHalted = false;
            const FPinRef Dep = In->Links.Num() > 0 ? FollowDataKnots(In->Links[0], bHalted) : FPinRef();
            if (Dep.Node == INDEX_NONE || bHalted)
            {
                continue;
            }
            const FNode& Source = Graph.Nodes[Dep.Node];
            const auto IsExec = [](const FPin& Each) { return Each.bExec; };
            if (Keys.Contains(FKey(Dep.Node, Dep.Pin)))
            {
                Out.AddUnique(FKey(Dep.Node, Dep.Pin));
            }
            else if (!Walked.Contains(Dep.Node) && !IsMakeLiteral(Source.TypeId) && !Source.Ins.ContainsByPredicate(IsExec) &&
                     !Source.Outs.ContainsByPredicate(IsExec))
            {
                Walked.Add(Dep.Node);
                DepsOf(Dep.Node, Walked, Out);
            }
        }
    };
    TArray<FKey> Sorted;
    TSet<FKey> Entered;
    TSet<FKey> Done;
    for (const FKey& Start : Starts)
    {
        TArray<TPair<FKey, bool>> Stack = {TPair<FKey, bool>(Start, false)};
        while (Stack.Num() > 0)
        {
            const TPair<FKey, bool> Top = Stack.Pop();
            if (Top.Value && !Done.Contains(Top.Key))
            {
                Done.Add(Top.Key);
                Sorted.Add(Top.Key);
            }
            if (Top.Value || Entered.Contains(Top.Key))
            {
                continue;
            }
            Entered.Add(Top.Key);
            Stack.Add(TPair<FKey, bool>(Top.Key, true));
            TSet<int32> Walked;
            TArray<FKey> Deps;
            DepsOf(Top.Key.Key, Walked, Deps);
            for (const FKey& DepKey : Deps)
            {
                if (DepKey.Key != Top.Key.Key && !Entered.Contains(DepKey))
                {
                    Stack.Add(TPair<FKey, bool>(DepKey, false));
                }
            }
        }
    }
    return Sorted;
}
FString FDecompiler::Run(TSet<int32>& OutPlaced, TArray<FString>& OutWarnings)
{
    // Entries: nodes the flow starts from (an exec output, no exec input), top to bottom, then by GUID.
    TArray<int32> Entries;
    const auto HasExec = [](const TArray<FPin>& Pins) { return Pins.ContainsByPredicate([](const FPin& Pin) { return Pin.bExec; }); };
    for (int32 At = 0; At < Graph.Nodes.Num(); ++At)
    {
        if (IsEntry(Graph.Nodes[At]))
        {
            Entries.Add(At);
        }
    }
    Entries.StableSort([this](int32 A, int32 B)
    {
        const FNode& Left = Graph.Nodes[A];
        const FNode& Right = Graph.Nodes[B];
        return Left.Y != Right.Y ? Left.Y < Right.Y : Left.Guid.Compare(Right.Guid, ESearchCase::CaseSensitive) < 0;
    });

    // An output of a node that runs (exec in and out) and is read elsewhere gets a name where the node runs.
    TArray<int32> Queue = Entries;
    TSet<int32> Seen;
    for (int32 Head = 0; Head < Queue.Num(); ++Head)
    {
        const int32 At = Queue[Head];
        if (Seen.Contains(At))
        {
            continue;
        }
        Seen.Add(At);
        const FNode& Node = Graph.Nodes[At];
        const bool bRuns = HasExec(Node.Ins) && HasExec(Node.Outs);
        for (int32 Out = 0; Out < Node.Outs.Num(); ++Out)
        {
            const FPin& Pin = Node.Outs[Out];
            if (bRuns && IsDataOut(Pin) && Pin.Links.Num() > 0)
            {
                Needed.Add(FKey(At, Out));
            }
            if (Pin.bExec && Pin.Links.Num() > 0)
            {
                Queue.Add(Pin.Links[0].Node);
            }
        }
    }

    TArray<FLine> Lines;
    for (const int32 Entry : Entries)
    {
        const FNode& Node = Graph.Nodes[Entry];
        Visited.Add(Entry);
        Placed.Add(Entry);
        const bool bEvent = Node.TypeId.StartsWith(TEXT("AddEvent|"), ESearchCase::CaseSensitive);
        const FString Name = EntryName(Node.TypeId);
        TArray<FString> Params;
        FBindings Bindings;
        for (int32 Out = 0; Out < Node.Outs.Num(); ++Out)
        {
            if (IsDataOut(Node.Outs[Out]))
            {
                Params.Add(Node.Outs[Out].Name);
                Bindings.Add(FKey(Entry, Out), Node.Outs[Out].Name);
            }
        }
        const FString ParamList = TEXT("(") + FString::Join(Params, TEXT(" ")) + TEXT(")");
        const bool bFunction = Node.TypeId.StartsWith(TEXT("|"), ESearchCase::CaseSensitive) && !bEvent;
        Lines.Add({bFunction ? FString::Printf(TEXT("(fn %s %s"), *Name, *ParamList)
                             : Params.Num() > 0 ? FString::Printf(TEXT("(event %s %s"), *Name, *ParamList) : TEXT("(event ") + Name,
                   Node.Id});
        // A pure value two nodes read is bound once at the top; Blueprint still works it out again at each read.
        const TSet<FKey> Hoisted = SharedPure(Entry);
        for (const FKey& Key : SortHoisted(Hoisted))
        {
            if (Bindings.Contains(Key))
            {
                continue;
            }
            const FNode& Pure = Graph.Nodes[Key.Key];
            TArray<FKey> Outs;
            for (int32 Out = 0; Out < Pure.Outs.Num(); ++Out)
            {
                const FKey Each(Key.Key, Out);
                if (Hoisted.Contains(Each) && !ComponentFor(Pure.TypeId, Pure.Outs[Out].Name))
                {
                    Outs.Add(Each);
                }
            }
            if (Outs.Num() == 0 || (Outs.Num() == 1 && Outs[0].Value == 0))
            {
                Outs = {Key};
            }
            const FString Value = Source(Key.Key, Outs.Num() > 1 || Outs[0].Value != 0 ? 0 : Key.Value, Bindings);
            FString Var;
            if (Outs.Num() == 1 && Outs[0].Value == Key.Value && PrintsInline(Key))
            {
                Var = AllocName(Pure.Outs[Key.Value].Name, Bindings);
                Bindings.Add(Key, Var);
            }
            else
            {
                TArray<FString> Names;
                Names.Init(TEXT("_"), Outs.Last().Value + 1);
                for (const FKey& Each : Outs)
                {
                    Names[Each.Value] = AllocName(Pure.Outs[Each.Value].Name, Bindings);
                    Bindings.Add(Each, Names[Each.Value]);
                }
                Var = TEXT("(") + FString::Join(Names, TEXT(" ")) + TEXT(")");
            }
            Lines.Add({FString::Printf(TEXT("  (bind %s %s)"), *Var, *Value), Pure.Id + TEXT(" pure, runs at each read")});
        }
        FState State{Bindings, 1, {}};
        const FPinRef Then = FollowExec(Node, TEXT("then"));
        if (Then.Node != INDEX_NONE)
        {
            Stmts(Then, State);
        }
        for (const FForm& Form : State.Lines)
        {
            Form.Render(Lines);
        }
        Lines.Last().Text += TEXT(")");
        Lines.Add({});
    }
    OutPlaced.Append(Placed);
    OutWarnings.Append(Warnings);
    TArray<FString> Text;
    for (const FLine& Line : Lines)
    {
        const bool bCommentOnly = Line.Text.TrimEnd().IsEmpty();
        Text.Add(Line.Tag.IsEmpty() ? Line.Text : Line.Text + (bCommentOnly ? TEXT("; ") : TEXT("  ; ")) + Line.Tag);
    }
    return FString::Join(Text, TEXT("\n"));
}
} // namespace Emit

FString Decompile(const FGraph& Graph, TSet<int32>& OutPlaced, TArray<FString>& OutWarnings)
{
    Emit::FDecompiler Decompiler(Graph);
    return Decompiler.Run(OutPlaced, OutWarnings);
}
} // namespace McpGraphCode
