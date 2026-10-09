#include "Processing/AnimBlueprintGenerator.h"

#include "AnimationGraph.h"
#include "AnimationStateGraph.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationTransitionGraph.h"
#include "AnimGraphNode_AnimDynamics.h"
#include "AnimGraphNode_Trail.h"
#include "AnimGraphNode_BlendListByBool.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "AnimGraphNode_CopyBoneDelta.h"
#include "AnimGraphNode_LinkedInputPose.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_ModifyBone.h"
#include "AnimGraphNode_TwoBoneIK.h"
#include "AnimGraphNode_CopyPoseFromMesh.h"
#include "AnimGraphNode_LayeredBoneBlend.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_Slot.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_StateResult.h"
#include "AnimGraphNode_TransitionResult.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "AnimStateEntryNode.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "FortnitePorting.h"
#include "FortnitePortingAnimInstance.h"
#include "FortnitePortingPartsAnimInstance.h"
#include "K2Node_VariableGet.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "Factories/AnimBlueprintFactory.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FeedbackContext.h"
#include "UObject/UnrealType.h"

const FName FAnimBlueprintGenerator::UpperBodySlot(TEXT("UpperBody"));
const FName FAnimBlueprintGenerator::AdditiveSlot(TEXT("UpperBodyAdd"));

namespace
{
	const TCHAR* LocomotionCacheName = TEXT("FortniteLocomotion");
	const FName UpperBodyRootBone(TEXT("spine_01"));

	template <typename TNode>
	TNode* SpawnNode(UEdGraph& Graph, int32 X, int32 Y)
	{
		FGraphNodeCreator<TNode> Creator(Graph);
		TNode* Node = Creator.CreateNode(false);
		Node->NodePosX = X;
		Node->NodePosY = Y;
		Creator.Finalize();
		return Node;
	}

	UK2Node_VariableGet* SpawnVariableGet(UEdGraph& Graph, FName VariableName, int32 X, int32 Y)
	{
		FGraphNodeCreator<UK2Node_VariableGet> Creator(Graph);
		UK2Node_VariableGet* Node = Creator.CreateNode(false);
		Node->VariableReference.SetSelfMember(VariableName);
		Node->NodePosX = X;
		Node->NodePosY = Y;
		Creator.Finalize();
		return Node;
	}

	bool IsPosePin(const UEdGraphPin* Pin)
	{
		const UObject* SubCategory = Pin->PinType.PinSubCategoryObject.Get();
		return SubCategory == FPoseLink::StaticStruct() || SubCategory == FComponentSpacePoseLink::StaticStruct();
	}

	UEdGraphPin* FindPosePin(UEdGraphNode* Node, EEdGraphPinDirection Direction)
	{
		if (Node == nullptr)
		{
			return nullptr;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->Direction == Direction && IsPosePin(Pin))
			{
				return Pin;
			}
		}

		return nullptr;
	}

	bool Connect(UEdGraphPin* From, UEdGraphPin* To)
	{
		if (From == nullptr || To == nullptr)
		{
			UE_LOG(LogFortnitePorting, Warning, TEXT("Anim Blueprint: missing pin for connection"));
			return false;
		}

		const UEdGraphSchema* Schema = From->GetOwningNode()->GetGraph()->GetSchema();
		if (!Schema->TryCreateConnection(From, To))
		{
			UE_LOG(LogFortnitePorting, Warning, TEXT("Anim Blueprint: could not connect %s -> %s"), *From->PinName.ToString(), *To->PinName.ToString());
			return false;
		}

		return true;
	}

	UEdGraph* FindAnimGraph(UAnimBlueprint* AnimBlueprint)
	{
		for (UEdGraph* Graph : AnimBlueprint->FunctionGraphs)
		{
			if (Graph && Graph->IsA<UAnimationGraph>() && Graph->GetFName() == TEXT("AnimGraph"))
			{
				return Graph;
			}
		}

		for (UEdGraph* Graph : AnimBlueprint->FunctionGraphs)
		{
			if (Graph && Graph->IsA<UAnimationGraph>())
			{
				return Graph;
			}
		}

		return nullptr;
	}

	UAnimGraphNode_Root* FindRoot(UEdGraph* Graph)
	{
		TArray<UAnimGraphNode_Root*> Roots;
		Graph->GetNodesOfClass<UAnimGraphNode_Root>(Roots);
		return Roots.Num() > 0 ? Roots[0] : nullptr;
	}

	UEdGraphPin* FindPinByName(UEdGraphNode* Node, const TCHAR* Name, EEdGraphPinDirection Direction, int32 FallbackPoseIndex)
	{
		if (UEdGraphPin* Pin = Node->FindPin(Name, Direction))
		{
			return Pin;
		}

		int32 PoseIndex = 0;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->Direction == Direction && IsPosePin(Pin) && PoseIndex++ == FallbackPoseIndex)
			{
				return Pin;
			}
		}

		return nullptr;
	}

	UAnimGraphNode_UseCachedPose* SpawnUseCachedPose(UEdGraph& Graph, UAnimGraphNode_SaveCachedPose* SaveNode, int32 X, int32 Y)
	{
		UAnimGraphNode_UseCachedPose* UseNode = SpawnNode<UAnimGraphNode_UseCachedPose>(Graph, X, Y);

		// Both links are private/weak editor properties; the compiler resolves the cache through them
		if (FStrProperty* NameProperty = CastField<FStrProperty>(UseNode->GetClass()->FindPropertyByName(TEXT("NameOfCache"))))
		{
			NameProperty->SetPropertyValue_InContainer(UseNode, SaveNode->CacheName);
		}
		if (FWeakObjectProperty* SaveProperty = CastField<FWeakObjectProperty>(UseNode->GetClass()->FindPropertyByName(TEXT("SaveCachedPoseNode"))))
		{
			SaveProperty->SetObjectPropertyValue_InContainer(UseNode, SaveNode);
		}

		UseNode->ReconstructNode();
		return UseNode;
	}

	/** Blend space players are driven by Speed, sequence players play (and optionally loop) a single animation */
	UEdGraphNode* SpawnPlayer(UEdGraph& Graph, UAnimationAsset* Asset, bool bLoop, int32 X, int32 Y)
	{
		if (UBlendSpace* BlendSpace = Cast<UBlendSpace>(Asset))
		{
			UAnimGraphNode_BlendSpacePlayer* Player = SpawnNode<UAnimGraphNode_BlendSpacePlayer>(Graph, X, Y);
			Player->SetAnimationAsset(BlendSpace);

			UK2Node_VariableGet* SpeedGetter = SpawnVariableGet(Graph, GET_MEMBER_NAME_CHECKED(UFortnitePortingAnimInstance, Speed), X - 260, Y + 40);
			Connect(SpeedGetter->FindPin(GET_MEMBER_NAME_CHECKED(UFortnitePortingAnimInstance, Speed)), Player->FindPin(TEXT("X")));
			return Player;
		}

		if (UAnimSequenceBase* Sequence = Cast<UAnimSequenceBase>(Asset))
		{
			UAnimGraphNode_SequencePlayer* Player = SpawnNode<UAnimGraphNode_SequencePlayer>(Graph, X, Y);
			Player->SetAnimationAsset(Sequence);
			Player->Node.SetLoopAnimation(bLoop);
			return Player;
		}

		return nullptr;
	}

	UAnimStateNode* AddState(UAnimationStateMachineGraph& StateMachine, const FString& StateName, UAnimationAsset* Asset, bool bLoop, int32 X, int32 Y)
	{
		UAnimStateNode* State = SpawnNode<UAnimStateNode>(StateMachine, X, Y);
		FBlueprintEditorUtils::RenameGraph(State->BoundGraph, StateName);

		UAnimationStateGraph* StateGraph = Cast<UAnimationStateGraph>(State->BoundGraph);
		if (StateGraph == nullptr)
		{
			return State;
		}

		if (UEdGraphNode* Player = SpawnPlayer(*StateGraph, Asset, bLoop, -300, 0))
		{
			Connect(FindPosePin(Player, EGPD_Output), FindPosePin(StateGraph->GetResultNode(), EGPD_Input));
		}

		return State;
	}

	UAnimGraphNode_LayeredBoneBlend* SpawnUpperBodyBlend(UEdGraph& Graph, int32 X, int32 Y)
	{
		// The node constructor already created one blend pose; configure it before pins are allocated
		FGraphNodeCreator<UAnimGraphNode_LayeredBoneBlend> Creator(Graph);
		UAnimGraphNode_LayeredBoneBlend* Layered = Creator.CreateNode(false);
		Layered->NodePosX = X;
		Layered->NodePosY = Y;
		if (Layered->Node.LayerSetup.IsEmpty())
		{
			Layered->Node.BlendWeights.Add(1.0f);
			Layered->Node.BlendPoses.AddDefaulted();
			Layered->Node.LayerSetup.AddDefaulted();
		}
		FBranchFilter Filter;
		Filter.BoneName = UpperBodyRootBone;
		Filter.BlendDepth = 0;
		Layered->Node.LayerSetup[0].BranchFilters.Reset();
		Layered->Node.LayerSetup[0].BranchFilters.Add(Filter);
		Layered->Node.bMeshSpaceRotationBlend = true;
		Creator.Finalize();

		// Pickaxe pose weight: fades to 0 while running so the arms keep the normal run animation, like Fortnite
		const FName Alpha = GET_MEMBER_NAME_CHECKED(UFortnitePortingAnimInstance, PickaxeUpperAlpha);
		UK2Node_VariableGet* AlphaGetter = SpawnVariableGet(Graph, Alpha, X - 250, Y + 350);
		if (UEdGraphPin* WeightPin = Layered->FindPin(TEXT("BlendWeights_0"), EGPD_Input))
		{
			Connect(AlphaGetter->FindPin(Alpha), WeightPin);
		}
		return Layered;
	}

	/**
	 * Pickaxe state = legs from the normal locomotion blend space + upper body (from spine_01) from the pickaxe pose,
	 * like Fortnite's layered weapon locomotion. Fortnite only ships a pickaxe idle (moving variants are additive
	 * deltas), so playing the pickaxe blend space alone would slide the feet or collapse the body.
	 */
	/** Legs player + upper body player -> layered blend from spine_01; returns the blend's output pin */
	UEdGraphPin* SpawnLayeredPose(UEdGraph& Graph, UAnimationAsset* Legs, UAnimationAsset* UpperBody, int32 X, int32 Y)
	{
		UEdGraphNode* LegsPlayer = SpawnPlayer(Graph, Legs, true, X - 400, Y - 80);
		UEdGraphNode* UpperPlayer = SpawnPlayer(Graph, UpperBody, true, X - 400, Y + 120);
		UAnimGraphNode_LayeredBoneBlend* Layered = SpawnUpperBodyBlend(Graph, X, Y);
		Connect(FindPosePin(LegsPlayer, EGPD_Output), FindPinByName(Layered, TEXT("BasePose"), EGPD_Input, 0));
		Connect(FindPosePin(UpperPlayer, EGPD_Output), FindPinByName(Layered, TEXT("BlendPoses_0"), EGPD_Input, 1));
		return FindPosePin(Layered, EGPD_Output);
	}

	void BuildPickaxeStateGraph(UAnimationStateGraph& StateGraph, UAnimationAsset* Legs, UAnimationAsset* UpperBody)
	{
		UEdGraphPin* ResultIn = FindPosePin(StateGraph.GetResultNode(), EGPD_Input);
		if (ResultIn == nullptr || Legs == nullptr || UpperBody == nullptr)
		{
			return;
		}
		ResultIn->BreakAllPinLinks();
		Connect(SpawnLayeredPose(StateGraph, Legs, UpperBody, -300, 0), ResultIn);
	}

	/** Crouch state: plain crouch without a pickaxe, crouch legs + pickaxe upper body while holding it */
	void BuildCrouchStateGraph(UAnimationStateGraph& StateGraph, UAnimationAsset* Crouch, UAnimationAsset* PickaxeUpperBody)
	{
		UEdGraphPin* ResultIn = FindPosePin(StateGraph.GetResultNode(), EGPD_Input);
		if (ResultIn == nullptr || Crouch == nullptr || PickaxeUpperBody == nullptr)
		{
			return;
		}
		ResultIn->BreakAllPinLinks();

		UAnimGraphNode_BlendListByBool* ByPickaxe = SpawnNode<UAnimGraphNode_BlendListByBool>(StateGraph, -250, 0);
		UEdGraphPin* WithPickaxe = SpawnLayeredPose(StateGraph, Crouch, PickaxeUpperBody, -550, -150);
		UEdGraphNode* PlainCrouch = SpawnPlayer(StateGraph, Crouch, true, -700, 250);
		Connect(WithPickaxe, FindPinByName(ByPickaxe, TEXT("BlendPose_0"), EGPD_Input, 0));
		Connect(FindPosePin(PlainCrouch, EGPD_Output), FindPinByName(ByPickaxe, TEXT("BlendPose_1"), EGPD_Input, 1));

		const FName HasPickaxe = GET_MEMBER_NAME_CHECKED(UFortnitePortingAnimInstance, bHasPickaxe);
		UK2Node_VariableGet* Getter = SpawnVariableGet(StateGraph, HasPickaxe, -500, 450);
		Connect(Getter->FindPin(HasPickaxe), ByPickaxe->FindPin(TEXT("bActiveValue"), EGPD_Input));
		Connect(FindPosePin(ByPickaxe, EGPD_Output), ResultIn);
	}

	UAnimStateNode* FindState(UAnimationStateMachineGraph& StateMachine, const FString& StateName)
	{
		TArray<UAnimStateNode*> States;
		StateMachine.GetNodesOfClass<UAnimStateNode>(States);
		for (UAnimStateNode* State : States)
		{
			if (State->GetStateName() == StateName)
			{
				return State;
			}
		}
		return nullptr;
	}

	/** Animation asset played directly into a state's result node (the layout AddState creates) */
	UAnimationAsset* GetStateAsset(UAnimStateNode* State)
	{
		UAnimationStateGraph* StateGraph = State ? Cast<UAnimationStateGraph>(State->BoundGraph) : nullptr;
		UEdGraphPin* ResultIn = StateGraph ? FindPosePin(StateGraph->GetResultNode(), EGPD_Input) : nullptr;
		if (ResultIn == nullptr || ResultIn->LinkedTo.Num() != 1)
		{
			return nullptr;
		}

		const UEdGraphNode* Source = ResultIn->LinkedTo[0]->GetOwningNode();
		if (const UAnimGraphNode_BlendSpacePlayer* BlendSpacePlayer = Cast<UAnimGraphNode_BlendSpacePlayer>(Source))
		{
			return BlendSpacePlayer->GetAnimationAsset();
		}
		if (const UAnimGraphNode_SequencePlayer* SequencePlayer = Cast<UAnimGraphNode_SequencePlayer>(Source))
		{
			return SequencePlayer->GetAnimationAsset();
		}
		return nullptr;
	}

	/** RuleVariable = NAME_None creates an automatic "when the animation is about to finish" transition */
	void AddTransition(UAnimationStateMachineGraph& StateMachine, UAnimStateNode* From, UAnimStateNode* To, FName RuleVariable, float Crossfade, int32 Priority = 1)
	{
		if (From == nullptr || To == nullptr)
		{
			return;
		}

		UAnimStateTransitionNode* Transition = SpawnNode<UAnimStateTransitionNode>(StateMachine, (From->NodePosX + To->NodePosX) / 2, (From->NodePosY + To->NodePosY) / 2);
		Transition->CreateConnections(From, To);
		Transition->CrossfadeDuration = Crossfade;
		Transition->PriorityOrder = Priority;

		if (RuleVariable.IsNone())
		{
			Transition->bAutomaticRuleBasedOnSequencePlayerInState = true;
			return;
		}

		UAnimationTransitionGraph* RuleGraph = Cast<UAnimationTransitionGraph>(Transition->BoundGraph);
		if (RuleGraph == nullptr)
		{
			return;
		}

		UK2Node_VariableGet* Getter = SpawnVariableGet(*RuleGraph, RuleVariable, -300, 0);
		Connect(Getter->FindPin(RuleVariable), RuleGraph->GetResultNode()->FindPin(TEXT("bCanEnterTransition")));
	}
}

UAnimBlueprint* FAnimBlueprintGenerator::CreateAnimBlueprint(const FString& Folder, const FString& Name, USkeleton* Skeleton, USkeletalMesh* PreviewMesh, UClass* ParentClass)
{
	UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
	Factory->BlueprintType = BPTYPE_Normal;
	Factory->ParentClass = ParentClass;
	Factory->TargetSkeleton = Skeleton;
	Factory->PreviewSkeletalMesh = PreviewMesh;

	UPackage* Package = CreatePackage(*(Folder / Name));
	UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Factory->FactoryCreateNew(UAnimBlueprint::StaticClass(), Package, FName(*Name), RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));
	if (AnimBlueprint == nullptr)
	{
		UE_LOG(LogFortnitePorting, Error, TEXT("Failed to create Anim Blueprint %s"), *Name);
		return nullptr;
	}

	FAssetRegistryModule::AssetCreated(AnimBlueprint);
	AnimBlueprint->MarkPackageDirty();
	return AnimBlueprint;
}

void FAnimBlueprintGenerator::Compile(UAnimBlueprint* AnimBlueprint)
{
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
	FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);

	if (AnimBlueprint->Status == BS_Error)
	{
		UE_LOG(LogFortnitePorting, Error, TEXT("Anim Blueprint %s compiled with errors, open it to review the graph"), *AnimBlueprint->GetName());
	}

	AnimBlueprint->MarkPackageDirty();
}

UAnimBlueprint* FAnimBlueprintGenerator::CreateLocomotionBlueprint(const FString& Folder, const FString& Name, USkeleton* Skeleton, USkeletalMesh* PreviewMesh, const FLocomotionAnimSet& AnimSet)
{
	if (AnimSet.Locomotion == nullptr && AnimSet.Idle == nullptr)
	{
		UE_LOG(LogFortnitePorting, Error, TEXT("Cannot build %s: no idle/locomotion animation was imported"), *Name);
		return nullptr;
	}

	UAnimBlueprint* AnimBlueprint = CreateAnimBlueprint(Folder, Name, Skeleton, PreviewMesh, UFortnitePortingAnimInstance::StaticClass());
	return BuildLocomotionGraph(AnimBlueprint, Skeleton, AnimSet);
}

UAnimBlueprint* FAnimBlueprintGenerator::RebuildLocomotionBlueprint(UAnimBlueprint* AnimBlueprint, const FLocomotionAnimSet& AnimSet)
{
	UEdGraph* AnimGraph = AnimBlueprint ? FindAnimGraph(AnimBlueprint) : nullptr;
	if (AnimGraph == nullptr || (AnimSet.Locomotion == nullptr && AnimSet.Idle == nullptr))
	{
		return AnimBlueprint;
	}

	// Generated asset: wipe the AnimGraph (state machine sub-graphs go with their nodes) and build it again,
	// so fixes to the generated layout reach characters that were imported before
	AnimBlueprint->Modify();
	TArray<UEdGraphNode*> Nodes = AnimGraph->Nodes;
	for (UEdGraphNode* Node : Nodes)
	{
		if (Node != nullptr && !Node->IsA<UAnimGraphNode_Root>())
		{
			FBlueprintEditorUtils::RemoveNode(AnimBlueprint, Node, true);
		}
	}

	UE_LOG(LogFortnitePorting, Log, TEXT("Rebuilding the animation graph of %s"), *AnimBlueprint->GetName());
	return BuildLocomotionGraph(AnimBlueprint, AnimBlueprint->TargetSkeleton, AnimSet);
}

UAnimBlueprint* FAnimBlueprintGenerator::BuildLocomotionGraph(UAnimBlueprint* AnimBlueprint, USkeleton* Skeleton, const FLocomotionAnimSet& AnimSet)
{
	UAnimationAsset* LocomotionAsset = AnimSet.Locomotion ? static_cast<UAnimationAsset*>(AnimSet.Locomotion) : static_cast<UAnimationAsset*>(AnimSet.Idle);
	const FString Name = GetNameSafe(AnimBlueprint);
	UEdGraph* AnimGraph = AnimBlueprint ? FindAnimGraph(AnimBlueprint) : nullptr;
	UAnimGraphNode_Root* Root = AnimGraph ? FindRoot(AnimGraph) : nullptr;
	if (Root == nullptr)
	{
		UE_LOG(LogFortnitePorting, Error, TEXT("Anim Blueprint %s has no AnimGraph output node"), *Name);
		return AnimBlueprint;
	}

	// State Machine -> DefaultSlot (emotes, pickaxe swings, mantle, glide montages) -> Output Pose
	UAnimGraphNode_StateMachine* StateMachineNode = SpawnNode<UAnimGraphNode_StateMachine>(*AnimGraph, Root->NodePosX - 600, Root->NodePosY);
	UAnimGraphNode_Slot* SlotNode = SpawnNode<UAnimGraphNode_Slot>(*AnimGraph, Root->NodePosX - 300, Root->NodePosY);
	if (AnimSet.Pickaxe != nullptr)
	{
		// Holding the pickaxe keeps its upper body pose in every state (jump, fall, land...): whole state machine
		// as legs, pickaxe pose from spine_01 up. Pickaxe/Crouch states are layered too, which is harmless.
		UAnimGraphNode_BlendListByBool* ByPickaxe = SpawnNode<UAnimGraphNode_BlendListByBool>(*AnimGraph, Root->NodePosX - 450, Root->NodePosY - 200);
		UEdGraphNode* PickaxePlayer = SpawnPlayer(*AnimGraph, AnimSet.Pickaxe, true, Root->NodePosX - 1100, Root->NodePosY - 50);
		UAnimGraphNode_LayeredBoneBlend* Layered = SpawnUpperBodyBlend(*AnimGraph, Root->NodePosX - 750, Root->NodePosY - 250);
		UAnimGraphNode_SaveCachedPose* SaveNode = SpawnNode<UAnimGraphNode_SaveCachedPose>(*AnimGraph, Root->NodePosX - 1000, Root->NodePosY - 400);
		SaveNode->CacheName = TEXT("FortniteStates");
		SaveNode->OnRenameNode(TEXT("FortniteStates"));
		Connect(FindPosePin(StateMachineNode, EGPD_Output), FindPosePin(SaveNode, EGPD_Input));
		UAnimGraphNode_UseCachedPose* ForLegs = SpawnUseCachedPose(*AnimGraph, SaveNode, Root->NodePosX - 1000, Root->NodePosY - 250);
		UAnimGraphNode_UseCachedPose* Plain = SpawnUseCachedPose(*AnimGraph, SaveNode, Root->NodePosX - 700, Root->NodePosY - 50);
		Connect(FindPosePin(ForLegs, EGPD_Output), FindPinByName(Layered, TEXT("BasePose"), EGPD_Input, 0));
		Connect(FindPosePin(PickaxePlayer, EGPD_Output), FindPinByName(Layered, TEXT("BlendPoses_0"), EGPD_Input, 1));
		Connect(FindPosePin(Layered, EGPD_Output), FindPinByName(ByPickaxe, TEXT("BlendPose_0"), EGPD_Input, 0));
		Connect(FindPosePin(Plain, EGPD_Output), FindPinByName(ByPickaxe, TEXT("BlendPose_1"), EGPD_Input, 1));
		const FName HasPickaxeVar = GET_MEMBER_NAME_CHECKED(UFortnitePortingAnimInstance, bHasPickaxe);
		UK2Node_VariableGet* Getter = SpawnVariableGet(*AnimGraph, HasPickaxeVar, Root->NodePosX - 700, Root->NodePosY - 400);
		Connect(Getter->FindPin(HasPickaxeVar), ByPickaxe->FindPin(TEXT("bActiveValue"), EGPD_Input));
		Connect(FindPosePin(ByPickaxe, EGPD_Output), FindPosePin(SlotNode, EGPD_Input));
	}
	else
	{
		Connect(FindPosePin(StateMachineNode, EGPD_Output), FindPosePin(SlotNode, EGPD_Input));
	}
	Connect(FindPosePin(SlotNode, EGPD_Output), FindPosePin(Root, EGPD_Input));

	UAnimationStateMachineGraph* StateMachine = Cast<UAnimationStateMachineGraph>(StateMachineNode->EditorStateMachineGraph);
	if (StateMachine == nullptr)
	{
		UE_LOG(LogFortnitePorting, Error, TEXT("Anim Blueprint %s: state machine graph was not created"), *Name);
		Compile(AnimBlueprint);
		return AnimBlueprint;
	}
	FBlueprintEditorUtils::RenameGraph(StateMachine, TEXT("FortniteLocomotion"));

	const FName InAir = GET_MEMBER_NAME_CHECKED(UFortnitePortingAnimInstance, bIsInAir);
	const FName OnGround = GET_MEMBER_NAME_CHECKED(UFortnitePortingAnimInstance, bIsOnGround);
	const FName Crouching = GET_MEMBER_NAME_CHECKED(UFortnitePortingAnimInstance, bIsCrouching);
	const FName Standing = GET_MEMBER_NAME_CHECKED(UFortnitePortingAnimInstance, bIsStanding);
	const FName HasPickaxe = GET_MEMBER_NAME_CHECKED(UFortnitePortingAnimInstance, bHasPickaxe);
	const FName NoPickaxe = GET_MEMBER_NAME_CHECKED(UFortnitePortingAnimInstance, bNoPickaxe);

	UAnimStateNode* Locomotion = AddState(*StateMachine, TEXT("Locomotion"), LocomotionAsset, true, 300, 0);
	UAnimStateNode* Crouch = AnimSet.Crouch ? AddState(*StateMachine, TEXT("Crouch"), AnimSet.Crouch, true, 300, 300) : nullptr;
	if (UAnimationStateGraph* CrouchGraph = Crouch && AnimSet.Pickaxe ? Cast<UAnimationStateGraph>(Crouch->BoundGraph) : nullptr)
	{
		BuildCrouchStateGraph(*CrouchGraph, AnimSet.Crouch, AnimSet.Pickaxe);
	}
	UAnimStateNode* Pickaxe = AnimSet.Pickaxe ? AddState(*StateMachine, TEXT("Pickaxe"), AnimSet.Pickaxe, true, 300, -300) : nullptr;
	if (UAnimationStateGraph* PickaxeGraph = Pickaxe ? Cast<UAnimationStateGraph>(Pickaxe->BoundGraph) : nullptr)
	{
		BuildPickaxeStateGraph(*PickaxeGraph, LocomotionAsset, AnimSet.Pickaxe);
	}
	UAnimStateNode* JumpStart = AnimSet.JumpStart ? AddState(*StateMachine, TEXT("JumpStart"), AnimSet.JumpStart, false, 700, -150) : nullptr;
	UAnimStateNode* JumpStartRun = AnimSet.JumpStartRun ? AddState(*StateMachine, TEXT("JumpStartRun"), AnimSet.JumpStartRun, false, 700, -300) : nullptr;
	UAnimStateNode* Falling = AnimSet.JumpLoop ? AddState(*StateMachine, TEXT("Falling"), AnimSet.JumpLoop, true, 1000, 0) : nullptr;
	UAnimStateNode* Land = AnimSet.Land ? AddState(*StateMachine, TEXT("Land"), AnimSet.Land, false, 700, 150) : nullptr;

	if (StateMachine->EntryNode && Locomotion)
	{
		for (UEdGraphPin* EntryPin : StateMachine->EntryNode->Pins)
		{
			if (EntryPin->Direction == EGPD_Output)
			{
				EntryPin->MakeLinkTo(Locomotion->GetInputPin());
				break;
			}
		}
	}

	// Airborne states: jump start -> falling loop -> land, each optional
	UAnimStateNode* AirEntry = JumpStart ? JumpStart : Falling;
	UAnimStateNode* GroundReturn = Land ? Land : Locomotion;

	// Jumping while running plays Fortnite's forward jump; from standstill the standing one
	const FName JumpFromRun = GET_MEMBER_NAME_CHECKED(UFortnitePortingAnimInstance, bJumpFromRun);
	const FName JumpFromStand = GET_MEMBER_NAME_CHECKED(UFortnitePortingAnimInstance, bJumpFromStand);
	AddTransition(*StateMachine, Locomotion, JumpStartRun, JumpFromRun, 0.1f, 0);
	AddTransition(*StateMachine, Pickaxe, JumpStartRun, JumpFromRun, 0.1f, 0);
	AddTransition(*StateMachine, Locomotion, AirEntry, JumpStartRun ? JumpFromStand : InAir, 0.1f, 0);
	AddTransition(*StateMachine, Pickaxe, AirEntry, JumpStartRun ? JumpFromStand : InAir, 0.1f, 0);
	AddTransition(*StateMachine, Crouch, Falling ? Falling : AirEntry, InAir, 0.15f, 0);

	if (JumpStart && Falling)
	{
		AddTransition(*StateMachine, JumpStart, Falling, NAME_None, 0.2f);
	}
	AddTransition(*StateMachine, JumpStart, GroundReturn, OnGround, 0.1f, 0);
	if (JumpStartRun && Falling)
	{
		AddTransition(*StateMachine, JumpStartRun, Falling, NAME_None, 0.2f);
	}
	AddTransition(*StateMachine, JumpStartRun, GroundReturn, OnGround, 0.1f, 0);
	AddTransition(*StateMachine, Falling, GroundReturn, OnGround, 0.1f);
	if (Land)
	{
		AddTransition(*StateMachine, Land, Locomotion, NAME_None, 0.2f);
	}

	AddTransition(*StateMachine, Locomotion, Crouch, Crouching, 0.2f);
	AddTransition(*StateMachine, Crouch, Locomotion, Standing, 0.2f);
	AddTransition(*StateMachine, Locomotion, Pickaxe, HasPickaxe, 0.2f);
	AddTransition(*StateMachine, Pickaxe, Locomotion, NoPickaxe, 0.2f);
	AddTransition(*StateMachine, Pickaxe, Crouch, Crouching, 0.2f);

	RegisterUpperBodySlot(Skeleton);
	if (EnsureUpperBodyLayer(AnimBlueprint))
	{
		return AnimBlueprint;
	}

	Compile(AnimBlueprint);
	return AnimBlueprint;
}

void FAnimBlueprintGenerator::RegisterUpperBodySlot(USkeleton* Skeleton)
{
	if (Skeleton == nullptr || Skeleton->ContainsSlotName(UpperBodySlot))
	{
		return;
	}

	Skeleton->Modify();
	Skeleton->AddSlotGroupName(UpperBodySlot);
	Skeleton->RegisterSlotNode(UpperBodySlot);
	Skeleton->SetSlotGroupName(UpperBodySlot, UpperBodySlot);
	Skeleton->MarkPackageDirty();
}

void FAnimBlueprintGenerator::RegisterAdditiveSlot(USkeleton* Skeleton)
{
	if (Skeleton == nullptr || Skeleton->ContainsSlotName(AdditiveSlot))
	{
		return;
	}

	Skeleton->Modify();
	Skeleton->AddSlotGroupName(AdditiveSlot);
	Skeleton->RegisterSlotNode(AdditiveSlot);
	Skeleton->SetSlotGroupName(AdditiveSlot, AdditiveSlot);
	Skeleton->MarkPackageDirty();
}

bool FAnimBlueprintGenerator::EnsureAdditiveLayer(UAnimBlueprint* AnimBlueprint)
{
	UEdGraph* AnimGraph = AnimBlueprint ? FindAnimGraph(AnimBlueprint) : nullptr;
	if (AnimGraph == nullptr)
	{
		return false;
	}

	TArray<UAnimGraphNode_Slot*> Slots;
	AnimGraph->GetNodesOfClass<UAnimGraphNode_Slot>(Slots);
	UAnimGraphNode_Slot* DefaultSlot = nullptr;
	for (UAnimGraphNode_Slot* Slot : Slots)
	{
		if (Slot->Node.SlotName == AdditiveSlot)
		{
			return false;      // already there
		}
		if (Slot->Node.SlotName != UpperBodySlot)
		{
			DefaultSlot = DefaultSlot ? DefaultSlot : Slot;
		}
	}

	UEdGraphPin* DefaultSlotIn = DefaultSlot ? FindPosePin(DefaultSlot, EGPD_Input) : nullptr;
	if (DefaultSlotIn == nullptr || DefaultSlotIn->LinkedTo.Num() != 1)
	{
		return false;
	}

	RegisterAdditiveSlot(AnimBlueprint->TargetSkeleton);
	UEdGraphPin* Source = DefaultSlotIn->LinkedTo[0];
	DefaultSlotIn->BreakAllPinLinks();

	// ... -> AdditiveSlot -> DefaultSlot: the recoil and aim layers land on the finished pose (legs, arms, hold, pickaxe)
	UAnimGraphNode_Slot* AddSlot = SpawnNode<UAnimGraphNode_Slot>(*AnimGraph, DefaultSlot->NodePosX - 250, DefaultSlot->NodePosY);
	AddSlot->Node.SlotName = AdditiveSlot;
	Connect(Source, FindPosePin(AddSlot, EGPD_Input));
	Connect(FindPosePin(AddSlot, EGPD_Output), DefaultSlotIn);
	UE_LOG(LogFortnitePorting, Log, TEXT("Added the additive slot to %s"), *AnimBlueprint->GetName());
	return true;
}

bool FAnimBlueprintGenerator::EnsureUpperBodyLayer(UAnimBlueprint* AnimBlueprint)
{
	const bool bAddedLayer = AddUpperBodyLayer(AnimBlueprint);
	const bool bAddedIK = EnsureLeftHandIK(AnimBlueprint);
	const bool bLayeredPickaxe = EnsurePickaxeLayer(AnimBlueprint);
	const bool bSlideArms = EnsureSlideArms(AnimBlueprint);
	const bool bAdditive = EnsureAdditiveLayer(AnimBlueprint);
	const bool bTwist = EnsureWeaponTwist(AnimBlueprint);
	if (bAddedIK || bLayeredPickaxe || bSlideArms || bAdditive || bTwist)
	{
		Compile(AnimBlueprint);
	}
	return bAddedLayer || bAddedIK || bLayeredPickaxe || bSlideArms || bAdditive || bTwist;
}

bool FAnimBlueprintGenerator::EnsurePickaxeLayer(UAnimBlueprint* AnimBlueprint)
{
	UEdGraph* AnimGraph = AnimBlueprint ? FindAnimGraph(AnimBlueprint) : nullptr;
	if (AnimGraph == nullptr)
	{
		return false;
	}

	TArray<UAnimGraphNode_StateMachine*> StateMachines;
	AnimGraph->GetNodesOfClass<UAnimGraphNode_StateMachine>(StateMachines);
	UAnimationStateMachineGraph* StateMachine = StateMachines.Num() > 0 ? Cast<UAnimationStateMachineGraph>(StateMachines[0]->EditorStateMachineGraph) : nullptr;
	if (StateMachine == nullptr)
	{
		return false;
	}

	UAnimStateNode* Pickaxe = FindState(*StateMachine, TEXT("Pickaxe"));
	UAnimationStateGraph* PickaxeGraph = Pickaxe ? Cast<UAnimationStateGraph>(Pickaxe->BoundGraph) : nullptr;
	UAnimationAsset* PickaxeAsset = GetStateAsset(Pickaxe);
	UAnimationAsset* LegsAsset = GetStateAsset(FindState(*StateMachine, TEXT("Locomotion")));
	if (PickaxeGraph == nullptr || PickaxeAsset == nullptr || LegsAsset == nullptr)
	{
		// Already layered (result fed by the blend node) or a hand-edited graph
		return false;
	}

	BuildPickaxeStateGraph(*PickaxeGraph, LegsAsset, PickaxeAsset);
	UE_LOG(LogFortnitePorting, Log, TEXT("Pickaxe state of %s now layers the pickaxe pose over the running legs"), *AnimBlueprint->GetName());
	return true;
}

namespace
{
	const FName LeftHandBone(TEXT("hand_l"));

	/** Shows a node property as a pin (most of these are shown by default, this guards against engine changes) */
	UEdGraphPin* ExposePin(UAnimGraphNode_Base* Node, FName PropertyName)
	{
		if (UEdGraphPin* Pin = Node->FindPin(PropertyName, EGPD_Input))
		{
			return Pin;
		}

		for (FOptionalPinFromProperty& Optional : Node->ShowPinForProperties)
		{
			if (Optional.PropertyName == PropertyName)
			{
				Optional.bShowPin = true;
			}
		}
		Node->ReconstructNode();
		return Node->FindPin(PropertyName, EGPD_Input);
	}

	void BindVariable(UEdGraph& Graph, UAnimGraphNode_Base* Node, FName PinName, FName VariableName, int32 X, int32 Y)
	{
		UK2Node_VariableGet* Getter = SpawnVariableGet(Graph, VariableName, X, Y);
		Connect(Getter->FindPin(VariableName), ExposePin(Node, PinName));
	}
}

namespace
{
	const FName SlideArmsBones[] = { FName(TEXT("clavicle_l")), FName(TEXT("clavicle_r")) };

	/** First blend space in any graph of the blueprint whose name contains Tag (e.g. "_Crouch", "_Pickaxe") */
	UBlendSpace* FindBlendSpace(UAnimBlueprint* AnimBlueprint, const TCHAR* Tag)
	{
		TArray<UEdGraph*> Graphs;
		AnimBlueprint->GetAllGraphs(Graphs);
		for (UEdGraph* Graph : Graphs)
		{
			TArray<UAnimGraphNode_BlendSpacePlayer*> Players;
			Graph->GetNodesOfClass<UAnimGraphNode_BlendSpacePlayer>(Players);
			for (UAnimGraphNode_BlendSpacePlayer* Player : Players)
			{
				UBlendSpace* BlendSpace = Cast<UBlendSpace>(Player->GetAnimationAsset());
				if (BlendSpace && BlendSpace->GetName().EndsWith(Tag))
				{
					return BlendSpace;
				}
			}
		}
		return nullptr;
	}

	/** Blend space held at its first sample (standing pose): X stays 0 */
	UAnimGraphNode_BlendSpacePlayer* SpawnStillPose(UEdGraph& Graph, UBlendSpace* BlendSpace, int32 X, int32 Y)
	{
		UAnimGraphNode_BlendSpacePlayer* Player = SpawnNode<UAnimGraphNode_BlendSpacePlayer>(Graph, X, Y);
		Player->SetAnimationAsset(BlendSpace);
		return Player;
	}
}

bool FAnimBlueprintGenerator::EnsureSlideArms(UAnimBlueprint* AnimBlueprint)
{
	UEdGraph* AnimGraph = AnimBlueprint ? FindAnimGraph(AnimBlueprint) : nullptr;
	USkeleton* Skeleton = AnimBlueprint ? ToRawPtr(AnimBlueprint->TargetSkeleton) : nullptr;
	if (AnimGraph == nullptr || Skeleton == nullptr)
	{
		return false;
	}
	for (const FName& Bone : SlideArmsBones)
	{
		if (Skeleton->GetReferenceSkeleton().FindBoneIndex(Bone) == INDEX_NONE)
		{
			return false;
		}
	}

	TArray<UAnimGraphNode_LayeredBoneBlend*> Layers;
	AnimGraph->GetNodesOfClass<UAnimGraphNode_LayeredBoneBlend>(Layers);
	for (const UAnimGraphNode_LayeredBoneBlend* Layer : Layers)
	{
		if (Layer->Node.LayerSetup.Num() > 0 && Layer->Node.LayerSetup[0].BranchFilters.Num() > 0
			&& Layer->Node.LayerSetup[0].BranchFilters[0].BoneName == SlideArmsBones[0])
		{
			return false;
		}
	}

	// The pose leaving DefaultSlot (slide, mantle, emotes, swings) is what gets its arms replaced
	TArray<UAnimGraphNode_Slot*> Slots;
	AnimGraph->GetNodesOfClass<UAnimGraphNode_Slot>(Slots);
	UAnimGraphNode_Slot* DefaultSlot = nullptr;
	for (UAnimGraphNode_Slot* Slot : Slots)
	{
		if (Slot->Node.SlotName != UpperBodySlot)
		{
			DefaultSlot = Slot;
			break;
		}
	}
	UEdGraphPin* SlotOut = DefaultSlot ? FindPosePin(DefaultSlot, EGPD_Output) : nullptr;
	UBlendSpace* CrouchPose = FindBlendSpace(AnimBlueprint, TEXT("_Crouch"));
	if (SlotOut == nullptr || SlotOut->LinkedTo.Num() != 1 || CrouchPose == nullptr)
	{
		UE_LOG(LogFortnitePorting, Warning, TEXT("%s: no DefaultSlot/crouch pose, unarmed slide arms not added"), *AnimBlueprint->GetName());
		return false;
	}
	UBlendSpace* PickaxePose = FindBlendSpace(AnimBlueprint, TEXT("_Pickaxe"));

	UEdGraphPin* Target = SlotOut->LinkedTo[0];
	SlotOut->BreakAllPinLinks();
	const int32 X = DefaultSlot->NodePosX + 250;
	const int32 Y = DefaultSlot->NodePosY + 350;

	// Arms (from the clavicles, in local space so they ride the slide's leaning torso) take the unarmed crouch
	// pose, or the pickaxe pose while holding it. Fortnite does the same: KneeSliding is authored with rifle arms
	// and the held item's anim layer replaces them. SlideArmsAlpha is 0 with a weapon, keeping the rifle arms.
	FGraphNodeCreator<UAnimGraphNode_LayeredBoneBlend> Creator(*AnimGraph);
	UAnimGraphNode_LayeredBoneBlend* Layered = Creator.CreateNode(false);
	Layered->NodePosX = X + 300;
	Layered->NodePosY = DefaultSlot->NodePosY;
	if (Layered->Node.LayerSetup.IsEmpty())
	{
		Layered->Node.BlendWeights.Add(1.0f);
		Layered->Node.BlendPoses.AddDefaulted();
		Layered->Node.LayerSetup.AddDefaulted();
	}
	Layered->Node.LayerSetup[0].BranchFilters.Reset();
	for (const FName& Bone : SlideArmsBones)
	{
		FBranchFilter Filter;
		Filter.BoneName = Bone;
		Filter.BlendDepth = 0;
		Layered->Node.LayerSetup[0].BranchFilters.Add(Filter);
	}
	Layered->Node.bMeshSpaceRotationBlend = false;
	Creator.Finalize();

	UEdGraphPin* ArmsPose = nullptr;
	UAnimGraphNode_BlendSpacePlayer* Unarmed = SpawnStillPose(*AnimGraph, CrouchPose, X - 300, Y + 150);
	if (PickaxePose != nullptr)
	{
		UAnimGraphNode_BlendListByBool* ByPickaxe = SpawnNode<UAnimGraphNode_BlendListByBool>(*AnimGraph, X, Y);
		UAnimGraphNode_BlendSpacePlayer* WithPickaxe = SpawnStillPose(*AnimGraph, PickaxePose, X - 300, Y - 50);
		Connect(FindPosePin(WithPickaxe, EGPD_Output), FindPinByName(ByPickaxe, TEXT("BlendPose_0"), EGPD_Input, 0));
		Connect(FindPosePin(Unarmed, EGPD_Output), FindPinByName(ByPickaxe, TEXT("BlendPose_1"), EGPD_Input, 1));
		const FName HasPickaxe = GET_MEMBER_NAME_CHECKED(UFortnitePortingAnimInstance, bHasPickaxe);
		UK2Node_VariableGet* Getter = SpawnVariableGet(*AnimGraph, HasPickaxe, X - 250, Y + 320);
		Connect(Getter->FindPin(HasPickaxe), ByPickaxe->FindPin(TEXT("bActiveValue"), EGPD_Input));
		ArmsPose = FindPosePin(ByPickaxe, EGPD_Output);
	}
	else
	{
		ArmsPose = FindPosePin(Unarmed, EGPD_Output);
	}

	Connect(SlotOut, FindPinByName(Layered, TEXT("BasePose"), EGPD_Input, 0));
	Connect(ArmsPose, FindPinByName(Layered, TEXT("BlendPoses_0"), EGPD_Input, 1));
	Connect(FindPosePin(Layered, EGPD_Output), Target);

	const FName Alpha = GET_MEMBER_NAME_CHECKED(UFortnitePortingAnimInstance, SlideArmsAlpha);
	UK2Node_VariableGet* AlphaGetter = SpawnVariableGet(*AnimGraph, Alpha, X + 50, DefaultSlot->NodePosY + 200);
	UEdGraphPin* WeightPin = Layered->FindPin(TEXT("BlendWeights_0"), EGPD_Input);
	if (WeightPin != nullptr)
	{
		Connect(AlphaGetter->FindPin(Alpha), WeightPin);
	}
	else
	{
		UE_LOG(LogFortnitePorting, Warning, TEXT("%s: slide arms layer has no weight pin"), *AnimBlueprint->GetName());
	}

	UE_LOG(LogFortnitePorting, Log, TEXT("Added unarmed/pickaxe arms for the knee slide to %s"), *AnimBlueprint->GetName());
	return true;
}

bool FAnimBlueprintGenerator::EnsureWeaponTwist(UAnimBlueprint* AnimBlueprint)
{
	UEdGraph* AnimGraph = AnimBlueprint ? FindAnimGraph(AnimBlueprint) : nullptr;
	UAnimGraphNode_Root* Root = AnimGraph ? FindRoot(AnimGraph) : nullptr;
	if (Root == nullptr || AnimBlueprint->TargetSkeleton == nullptr
		|| AnimBlueprint->TargetSkeleton->GetReferenceSkeleton().FindBoneIndex(UpperBodyRootBone) == INDEX_NONE)
	{
		return false;
	}

	TArray<UAnimGraphNode_ModifyBone*> Existing;
	AnimGraph->GetNodesOfClass<UAnimGraphNode_ModifyBone>(Existing);
	for (const UAnimGraphNode_ModifyBone* Node : Existing)
	{
		if (Node->Node.BoneToModify.BoneName == UpperBodyRootBone)
		{
			return false;
		}
	}

	UEdGraphPin* RootIn = FindPosePin(Root, EGPD_Input);
	if (RootIn == nullptr || RootIn->LinkedTo.Num() != 1)
	{
		return false;
	}

	UEdGraphPin* Source = RootIn->LinkedTo[0];
	RootIn->BreakAllPinLinks();

	const int32 X = Root->NodePosX;
	const int32 Y = Root->NodePosY + 500;
	Root->NodePosX = X + 800;

	UAnimGraphNode_LocalToComponentSpace* ToComponent = SpawnNode<UAnimGraphNode_LocalToComponentSpace>(*AnimGraph, X, Y);

	FGraphNodeCreator<UAnimGraphNode_ModifyBone> TwistCreator(*AnimGraph);
	UAnimGraphNode_ModifyBone* Twist = TwistCreator.CreateNode(false);
	Twist->NodePosX = X + 250;
	Twist->NodePosY = Y;
	Twist->Node.BoneToModify.BoneName = UpperBodyRootBone;
	Twist->Node.TranslationMode = BMM_Ignore;
	Twist->Node.ScaleMode = BMM_Ignore;
	Twist->Node.RotationMode = BMM_Additive;
	Twist->Node.RotationSpace = BCS_ComponentSpace;
	TwistCreator.Finalize();

	UAnimGraphNode_ComponentToLocalSpace* ToLocal = SpawnNode<UAnimGraphNode_ComponentToLocalSpace>(*AnimGraph, X + 550, Y);

	Connect(Source, FindPosePin(ToComponent, EGPD_Input));
	Connect(FindPosePin(ToComponent, EGPD_Output), FindPosePin(Twist, EGPD_Input));
	Connect(FindPosePin(Twist, EGPD_Output), FindPosePin(ToLocal, EGPD_Input));
	Connect(FindPosePin(ToLocal, EGPD_Output), RootIn);

	BindVariable(*AnimGraph, Twist, TEXT("Rotation"), GET_MEMBER_NAME_CHECKED(UFortnitePortingAnimInstance, UpperBodyTwist), X + 50, Y + 200);

	UE_LOG(LogFortnitePorting, Log, TEXT("Added the weapon upper body twist to %s"), *AnimBlueprint->GetName());
	return true;
}

bool FAnimBlueprintGenerator::EnsureLeftHandIK(UAnimBlueprint* AnimBlueprint)
{
	UEdGraph* AnimGraph = AnimBlueprint ? FindAnimGraph(AnimBlueprint) : nullptr;
	UAnimGraphNode_Root* Root = AnimGraph ? FindRoot(AnimGraph) : nullptr;
	if (Root == nullptr || AnimBlueprint->TargetSkeleton == nullptr
		|| AnimBlueprint->TargetSkeleton->GetReferenceSkeleton().FindBoneIndex(LeftHandBone) == INDEX_NONE)
	{
		return false;
	}

	TArray<UAnimGraphNode_TwoBoneIK*> Existing;
	AnimGraph->GetNodesOfClass<UAnimGraphNode_TwoBoneIK>(Existing);
	for (const UAnimGraphNode_TwoBoneIK* Node : Existing)
	{
		if (Node->Node.IKBone.BoneName == LeftHandBone)
		{
			return false;
		}
	}

	UEdGraphPin* RootIn = FindPosePin(Root, EGPD_Input);
	if (RootIn == nullptr || RootIn->LinkedTo.Num() != 1)
	{
		UE_LOG(LogFortnitePorting, Warning, TEXT("%s: Output Pose has no single input, left hand IK not added"), *AnimBlueprint->GetName());
		return false;
	}

	UEdGraphPin* Source = RootIn->LinkedTo[0];
	RootIn->BreakAllPinLinks();

	const int32 X = Root->NodePosX;
	const int32 Y = Root->NodePosY;
	Root->NodePosX = X + 1100;

	UAnimGraphNode_LocalToComponentSpace* ToComponent = SpawnNode<UAnimGraphNode_LocalToComponentSpace>(*AnimGraph, X, Y);

	FGraphNodeCreator<UAnimGraphNode_ModifyBone> RotateCreator(*AnimGraph);
	UAnimGraphNode_ModifyBone* Rotate = RotateCreator.CreateNode(false);
	Rotate->NodePosX = X + 250;
	Rotate->NodePosY = Y;
	Rotate->Node.BoneToModify.BoneName = LeftHandBone;
	Rotate->Node.TranslationMode = BMM_Ignore;
	Rotate->Node.ScaleMode = BMM_Ignore;
	Rotate->Node.RotationMode = BMM_Replace;
	Rotate->Node.RotationSpace = BCS_ComponentSpace;
	RotateCreator.Finalize();

	// Two Bone IK keeps the hand's component space rotation (set just before) and only moves the arm
	FGraphNodeCreator<UAnimGraphNode_TwoBoneIK> IKCreator(*AnimGraph);
	UAnimGraphNode_TwoBoneIK* IK = IKCreator.CreateNode(false);
	IK->NodePosX = X + 550;
	IK->NodePosY = Y;
	IK->Node.IKBone.BoneName = LeftHandBone;
	IK->Node.EffectorLocationSpace = BCS_ComponentSpace;
	IK->Node.JointTargetLocationSpace = BCS_ComponentSpace;
	IK->Node.bAllowStretching = false;
	IK->Node.bTakeRotationFromEffectorSpace = false;
	IK->Node.bMaintainEffectorRelRot = false;
	IKCreator.Finalize();

	UAnimGraphNode_ComponentToLocalSpace* ToLocal = SpawnNode<UAnimGraphNode_ComponentToLocalSpace>(*AnimGraph, X + 850, Y);

	Connect(Source, FindPosePin(ToComponent, EGPD_Input));
	Connect(FindPosePin(ToComponent, EGPD_Output), FindPosePin(Rotate, EGPD_Input));
	Connect(FindPosePin(Rotate, EGPD_Output), FindPosePin(IK, EGPD_Input));
	Connect(FindPosePin(IK, EGPD_Output), FindPosePin(ToLocal, EGPD_Input));
	Connect(FindPosePin(ToLocal, EGPD_Output), RootIn);

	BindVariable(*AnimGraph, Rotate, TEXT("Rotation"), GET_MEMBER_NAME_CHECKED(UFortnitePortingAnimInstance, LeftHandIKRotation), X + 50, Y + 200);
	BindVariable(*AnimGraph, Rotate, TEXT("Alpha"), GET_MEMBER_NAME_CHECKED(UFortnitePortingAnimInstance, LeftHandIKAlpha), X + 50, Y + 280);
	BindVariable(*AnimGraph, IK, TEXT("EffectorLocation"), GET_MEMBER_NAME_CHECKED(UFortnitePortingAnimInstance, LeftHandIKLocation), X + 350, Y + 200);
	BindVariable(*AnimGraph, IK, TEXT("JointTargetLocation"), GET_MEMBER_NAME_CHECKED(UFortnitePortingAnimInstance, LeftHandIKJoint), X + 350, Y + 280);
	BindVariable(*AnimGraph, IK, TEXT("Alpha"), GET_MEMBER_NAME_CHECKED(UFortnitePortingAnimInstance, LeftHandIKAlpha), X + 350, Y + 360);

	UE_LOG(LogFortnitePorting, Log, TEXT("Added left hand IK (two-handed items) to %s"), *AnimBlueprint->GetName());
	return true;
}

bool FAnimBlueprintGenerator::AddUpperBodyLayer(UAnimBlueprint* AnimBlueprint)
{
	UEdGraph* AnimGraph = AnimBlueprint ? FindAnimGraph(AnimBlueprint) : nullptr;
	if (AnimGraph == nullptr)
	{
		return false;
	}

	TArray<UAnimGraphNode_Slot*> Slots;
	AnimGraph->GetNodesOfClass<UAnimGraphNode_Slot>(Slots);
	UAnimGraphNode_Slot* DefaultSlot = nullptr;
	for (UAnimGraphNode_Slot* Slot : Slots)
	{
		if (Slot->Node.SlotName == UpperBodySlot)
		{
			return false;
		}

		DefaultSlot = DefaultSlot ? DefaultSlot : Slot;
	}

	return InsertUpperBodyLayer(AnimBlueprint, FindPosePin(DefaultSlot, EGPD_Input));
}

bool FAnimBlueprintGenerator::InsertUpperBodyLayer(UAnimBlueprint* AnimBlueprint, UEdGraphPin* CutIn)
{
	UEdGraph* AnimGraph = AnimBlueprint ? FindAnimGraph(AnimBlueprint) : nullptr;
	if (AnimGraph == nullptr)
	{
		return false;
	}

	TArray<UAnimGraphNode_StateMachine*> StateMachines;
	AnimGraph->GetNodesOfClass<UAnimGraphNode_StateMachine>(StateMachines);
	UAnimGraphNode_StateMachine* StateMachine = StateMachines.Num() > 0 ? StateMachines[0] : nullptr;
	// The pose feeding CutIn (normally DefaultSlot): the state machine itself, or the pickaxe upper body switch placed after it
	UEdGraphPin* StateMachineOut = CutIn && CutIn->LinkedTo.Num() == 1 ? CutIn->LinkedTo[0] : FindPosePin(StateMachine, EGPD_Output);
	if (StateMachineOut == nullptr || CutIn == nullptr || StateMachine == nullptr)
	{
		UE_LOG(LogFortnitePorting, Warning, TEXT("%s does not have the generated State Machine -> DefaultSlot layout, weapon upper body layer not added"), *AnimBlueprint->GetName());
		return false;
	}
	UEdGraphNode* CutNode = CutIn->GetOwningNode();

	RegisterUpperBodySlot(AnimBlueprint->TargetSkeleton);
	StateMachineOut->BreakAllPinLinks();

	// State Machine -> Save Cached Pose; the cached pose feeds both the legs (base) and the UpperBody slot
	const int32 X = CutNode->NodePosX;
	const int32 Y = CutNode->NodePosY;
	CutNode->NodePosX = X + 150;
	StateMachine->NodePosX = X - 1100;

	UAnimGraphNode_SaveCachedPose* SaveNode = SpawnNode<UAnimGraphNode_SaveCachedPose>(*AnimGraph, X - 800, Y - 250);
	SaveNode->CacheName = LocomotionCacheName;
	SaveNode->OnRenameNode(LocomotionCacheName);
	Connect(StateMachineOut, FindPosePin(SaveNode, EGPD_Input));

	UAnimGraphNode_UseCachedPose* BaseCache = SpawnUseCachedPose(*AnimGraph, SaveNode, X - 650, Y);
	UAnimGraphNode_UseCachedPose* SlotCache = SpawnUseCachedPose(*AnimGraph, SaveNode, X - 650, Y + 150);

	UAnimGraphNode_Slot* UpperSlot = SpawnNode<UAnimGraphNode_Slot>(*AnimGraph, X - 450, Y + 150);
	UpperSlot->Node.SlotName = UpperBodySlot;

	// The node constructor already created one blend pose; configure it before pins are allocated
	FGraphNodeCreator<UAnimGraphNode_LayeredBoneBlend> LayeredCreator(*AnimGraph);
	UAnimGraphNode_LayeredBoneBlend* Layered = LayeredCreator.CreateNode(false);
	Layered->NodePosX = X - 200;
	Layered->NodePosY = Y;
	if (Layered->Node.LayerSetup.IsEmpty())
	{
		Layered->Node.BlendWeights.Add(1.0f);
		Layered->Node.BlendPoses.AddDefaulted();
		Layered->Node.LayerSetup.AddDefaulted();
	}
	FBranchFilter Filter;
	Filter.BoneName = UpperBodyRootBone;
	Filter.BlendDepth = 0;
	Layered->Node.LayerSetup[0].BranchFilters.Reset();
	Layered->Node.LayerSetup[0].BranchFilters.Add(Filter);
	Layered->Node.BlendWeights[0] = 1.0f;
	Layered->Node.bMeshSpaceRotationBlend = true;
	LayeredCreator.Finalize();

	// The weapon layer is always on: with nothing playing in the slot it passes the locomotion through unchanged
	if (UEdGraphPin* WeightPin = Layered->FindPin(TEXT("BlendWeights_0"), EGPD_Input))
	{
		AnimGraph->GetSchema()->TrySetDefaultValue(*WeightPin, TEXT("1.0"));
	}

	Connect(FindPosePin(BaseCache, EGPD_Output), FindPinByName(Layered, TEXT("BasePose"), EGPD_Input, 0));
	Connect(FindPosePin(SlotCache, EGPD_Output), FindPosePin(UpperSlot, EGPD_Input));
	Connect(FindPosePin(UpperSlot, EGPD_Output), FindPinByName(Layered, TEXT("BlendPoses_0"), EGPD_Input, 1));
	Connect(FindPosePin(Layered, EGPD_Output), CutIn);

	Compile(AnimBlueprint);
	UE_LOG(LogFortnitePorting, Log, TEXT("Added the UpperBody weapon layer to %s"), *AnimBlueprint->GetName());
	return true;
}

bool FAnimBlueprintGenerator::RepairUpperBodyLayer(UAnimBlueprint* AnimBlueprint)
{
	UEdGraph* AnimGraph = AnimBlueprint ? FindAnimGraph(AnimBlueprint) : nullptr;
	if (AnimGraph == nullptr)
	{
		return false;
	}

	TArray<UAnimGraphNode_Slot*> Slots;
	AnimGraph->GetNodesOfClass<UAnimGraphNode_Slot>(Slots);
	UAnimGraphNode_Slot* UpperSlot = nullptr;
	UAnimGraphNode_Slot* DefaultSlot = nullptr;
	for (UAnimGraphNode_Slot* Slot : Slots)
	{
		if (Slot->Node.SlotName == UpperBodySlot)
		{
			UpperSlot = Slot;
		}
		else if (Slot->Node.SlotName != AdditiveSlot)
		{
			DefaultSlot = DefaultSlot ? DefaultSlot : Slot;
		}
	}
	if (UpperSlot == nullptr)
	{
		return false;      // AddUpperBodyLayer builds it from scratch
	}

	// Healthy: cached locomotion -> UpperBody slot -> layered blend (spine_01, weight 1) -> somewhere on the way to the output
	FString Problem;
	UEdGraphPin* SlotIn = FindPosePin(UpperSlot, EGPD_Input);
	UEdGraphPin* SlotOut = FindPosePin(UpperSlot, EGPD_Output);
	UAnimGraphNode_LayeredBoneBlend* Layered = nullptr;
	if (SlotIn == nullptr || SlotIn->LinkedTo.Num() != 1 || !SlotIn->LinkedTo[0]->GetOwningNode()->IsA<UAnimGraphNode_UseCachedPose>())
	{
		Problem = TEXT("the UpperBody slot does not read the cached locomotion");
	}
	else if (SlotOut == nullptr || SlotOut->LinkedTo.Num() != 1 || (Layered = Cast<UAnimGraphNode_LayeredBoneBlend>(SlotOut->LinkedTo[0]->GetOwningNode())) == nullptr)
	{
		Problem = TEXT("the UpperBody slot does not feed a Layered blend per bone");
	}
	else
	{
		UEdGraphPin* Base = FindPinByName(Layered, TEXT("BasePose"), EGPD_Input, 0);
		UEdGraphPin* Out = FindPosePin(Layered, EGPD_Output);
		UEdGraphPin* Weight = Layered->FindPin(TEXT("BlendWeights_0"), EGPD_Input);
		const bool bFilter = Layered->Node.LayerSetup.Num() > 0 && Layered->Node.LayerSetup[0].BranchFilters.Num() > 0
			&& Layered->Node.LayerSetup[0].BranchFilters[0].BoneName == UpperBodyRootBone;
		if (Base == nullptr || Base->LinkedTo.Num() != 1 || Out == nullptr || Out->LinkedTo.Num() != 1)
		{
			Problem = TEXT("the weapon layer is not connected on both sides");
		}
		else if (!bFilter)
		{
			Problem = FString::Printf(TEXT("the weapon layer does not start at %s"), *UpperBodyRootBone.ToString());
		}
		else if (Weight != nullptr && Weight->LinkedTo.Num() == 0 && FCString::Atof(*Weight->DefaultValue) < 0.99f)
		{
			// The pin default is what runs when nothing is plugged in: a 0 here hides every weapon montage without any error
			AnimGraph->GetSchema()->TrySetDefaultValue(*Weight, TEXT("1.0"));
			Layered->Node.BlendWeights[0] = 1.0f;
			Compile(AnimBlueprint);
			UE_LOG(LogFortnitePorting, Warning, TEXT("%s: the weapon layer weight was %s, set to 1"), *AnimBlueprint->GetName(), *Weight->DefaultValue);
			return true;
		}
	}

	if (Problem.IsEmpty())
	{
		return false;
	}

	UE_LOG(LogFortnitePorting, Warning, TEXT("%s: %s; rebuilding the UpperBody weapon layer"), *AnimBlueprint->GetName(), *Problem);

	// Whatever fed the cached locomotion (the state machine or the pickaxe switch) goes straight back into DefaultSlot, then the layer is
	// added again as on a fresh import. The IK, additive and twist nodes after DefaultSlot are not touched.
	TArray<UAnimGraphNode_SaveCachedPose*> Saves;
	AnimGraph->GetNodesOfClass<UAnimGraphNode_SaveCachedPose>(Saves);
	UAnimGraphNode_SaveCachedPose* LocomotionCache = nullptr;
	for (UAnimGraphNode_SaveCachedPose* Save : Saves)
	{
		if (Save->CacheName == LocomotionCacheName)
		{
			LocomotionCache = Save;
		}
	}
	TArray<UAnimGraphNode_StateMachine*> StateMachines;
	AnimGraph->GetNodesOfClass<UAnimGraphNode_StateMachine>(StateMachines);
	UEdGraphPin* CacheIn = LocomotionCache ? FindPosePin(LocomotionCache, EGPD_Input) : nullptr;
	UEdGraphPin* Source = CacheIn && CacheIn->LinkedTo.Num() == 1 ? CacheIn->LinkedTo[0] : (StateMachines.Num() > 0 ? FindPosePin(StateMachines[0], EGPD_Output) : nullptr);
	// The pin the layer used to drive (the additive slot or DefaultSlot) is where the new layer cuts in again
	UEdGraphPin* LayerOut = Layered ? FindPosePin(Layered, EGPD_Output) : nullptr;
	UEdGraphPin* Target = LayerOut && LayerOut->LinkedTo.Num() == 1 ? LayerOut->LinkedTo[0] : FindPosePin(DefaultSlot, EGPD_Input);
	if (Source == nullptr || Target == nullptr)
	{
		UE_LOG(LogFortnitePorting, Error, TEXT("%s: no state machine / DefaultSlot to rebuild the weapon layer from, open the Anim Blueprint and check its graph"), *AnimBlueprint->GetName());
		return false;
	}

	TArray<UEdGraphNode*> Doomed;
	Doomed.Add(UpperSlot);
	if (Layered != nullptr)
	{
		Doomed.Add(Layered);
	}
	if (LocomotionCache != nullptr)
	{
		Doomed.Add(LocomotionCache);
		TArray<UAnimGraphNode_UseCachedPose*> Uses;
		AnimGraph->GetNodesOfClass<UAnimGraphNode_UseCachedPose>(Uses);
		for (UAnimGraphNode_UseCachedPose* Use : Uses)
		{
			if (FWeakObjectProperty* SaveProperty = CastField<FWeakObjectProperty>(Use->GetClass()->FindPropertyByName(TEXT("SaveCachedPoseNode"))))
			{
				if (SaveProperty->GetObjectPropertyValue_InContainer(Use) == LocomotionCache)
				{
					Doomed.Add(Use);
				}
			}
		}
	}
	for (UEdGraphNode* Node : Doomed)
	{
		FBlueprintEditorUtils::RemoveNode(AnimBlueprint, Node, true);
	}
	Source->BreakAllPinLinks();
	Target->BreakAllPinLinks();
	Connect(Source, Target);

	return InsertUpperBodyLayer(AnimBlueprint, Target);
}

UAnimBlueprint* FAnimBlueprintGenerator::CreatePartsBlueprint(const FString& Folder, const FString& Name, USkeleton* Skeleton, USkeletalMesh* PreviewMesh)
{
	UAnimBlueprint* AnimBlueprint = CreateAnimBlueprint(Folder, Name, Skeleton, PreviewMesh, UAnimInstance::StaticClass());
	UEdGraph* AnimGraph = AnimBlueprint ? FindAnimGraph(AnimBlueprint) : nullptr;
	UAnimGraphNode_Root* Root = AnimGraph ? FindRoot(AnimGraph) : nullptr;
	if (Root == nullptr)
	{
		return AnimBlueprint;
	}

	// Unlike Leader Pose, Copy Pose keeps bones missing from the body (facial bones) following their parents
	UAnimGraphNode_CopyPoseFromMesh* CopyPose = SpawnNode<UAnimGraphNode_CopyPoseFromMesh>(*AnimGraph, Root->NodePosX - 350, Root->NodePosY);
	CopyPose->Node.bUseAttachedParent = true;
	CopyPose->Node.bCopyCurves = true;
	Connect(FindPosePin(CopyPose, EGPD_Output), FindPosePin(Root, EGPD_Input));

	Compile(AnimBlueprint);
	return AnimBlueprint;
}


UAnimBlueprint* FAnimBlueprintGenerator::CreateBodyFixBlueprint(const FString& Folder, const FString& Name, USkeletalMesh* Body)
{
	USkeleton* Skeleton = Body ? Body->GetSkeleton() : nullptr;
	if (Skeleton == nullptr)
	{
		return nullptr;
	}

	const FReferenceSkeleton& MeshBones = Body->GetRefSkeleton();
	TArray<TPair<FName, FName>> Pairs;
	for (const TCHAR* Side : { TEXT("l"), TEXT("r") })
	{
		const FName Source(*FString::Printf(TEXT("upperarm_%s"), Side));
		const FName Target(*FString::Printf(TEXT("dfrm_upperarm_%s"), Side));
		if (MeshBones.FindBoneIndex(Source) != INDEX_NONE && MeshBones.FindBoneIndex(Target) != INDEX_NONE)
		{
			Pairs.Add({ Source, Target });
		}
	}
	if (Pairs.IsEmpty())
	{
		return nullptr;
	}

	UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, *FString::Printf(TEXT("%s/%s.%s"), *Folder, *Name, *Name));
	if (AnimBlueprint == nullptr)
	{
		AnimBlueprint = CreateAnimBlueprint(Folder, Name, Skeleton, Body, UAnimInstance::StaticClass());
	}
	UEdGraph* AnimGraph = AnimBlueprint ? FindAnimGraph(AnimBlueprint) : nullptr;
	UAnimGraphNode_Root* Root = AnimGraph ? FindRoot(AnimGraph) : nullptr;
	if (Root == nullptr)
	{
		return nullptr;
	}

	// Generated asset: start from an empty graph every time
	AnimBlueprint->Modify();
	AnimBlueprint->TargetSkeleton = Skeleton;
	AnimBlueprint->SetPreviewMesh(Body);
	TArray<UEdGraphNode*> Nodes = AnimGraph->Nodes;
	for (UEdGraphNode* Node : Nodes)
	{
		if (Node != nullptr && Node != Root)
		{
			FBlueprintEditorUtils::RemoveNode(AnimBlueprint, Node, true);
		}
	}

	const int32 Y = Root->NodePosY;
	int32 X = Root->NodePosX - (Pairs.Num() + 3) * 300;

	// Post process: the pose of the main animation comes in through the Input Pose node
	UAnimGraphNode_LinkedInputPose* Input = SpawnNode<UAnimGraphNode_LinkedInputPose>(*AnimGraph, X, Y);
	X += 300;
	UAnimGraphNode_LocalToComponentSpace* ToComponent = SpawnNode<UAnimGraphNode_LocalToComponentSpace>(*AnimGraph, X, Y);
	Connect(FindPosePin(Input, EGPD_Output), FindPosePin(ToComponent, EGPD_Input));
	UEdGraphPin* Current = FindPosePin(ToComponent, EGPD_Output);

	for (const TPair<FName, FName>& Pair : Pairs)
	{
		X += 300;
		FGraphNodeCreator<UAnimGraphNode_CopyBoneDelta> Creator(*AnimGraph);
		UAnimGraphNode_CopyBoneDelta* Copy = Creator.CreateNode(false);
		Copy->NodePosX = X;
		Copy->NodePosY = Y;
		Copy->Node.SourceBone.BoneName = Pair.Key;
		Copy->Node.TargetBone.BoneName = Pair.Value;
		Copy->Node.CopyMode = CopyBoneDeltaMode::Accumulate;
		Copy->Node.bCopyTranslation = false;
		Copy->Node.bCopyRotation = true;
		Copy->Node.bCopyScale = false;
		Copy->Node.RotationMultiplier = 1.0f;
		Creator.Finalize();

		Connect(Current, FindPosePin(Copy, EGPD_Input));
		Current = FindPosePin(Copy, EGPD_Output);
	}

	X += 300;
	UAnimGraphNode_ComponentToLocalSpace* ToLocal = SpawnNode<UAnimGraphNode_ComponentToLocalSpace>(*AnimGraph, X, Y);
	Connect(Current, FindPosePin(ToLocal, EGPD_Input));
	Connect(FindPosePin(ToLocal, EGPD_Output), FindPosePin(Root, EGPD_Input));

	UE_LOG(LogFortnitePorting, Log, TEXT("%s: sleeves follow the arms (%d bone(s))"), *Name, Pairs.Num());
	Compile(AnimBlueprint);
	return AnimBlueprint;
}

void FAnimBlueprintGenerator::BuildPartsGraph(UAnimBlueprint* AnimBlueprint, USkeleton* Skeleton, const TArray<FPartsChain>& Chains, const TArray<FPartsBoneOffset>& BlinkBones)
{
	UEdGraph* AnimGraph = AnimBlueprint ? FindAnimGraph(AnimBlueprint) : nullptr;
	UAnimGraphNode_Root* Root = AnimGraph ? FindRoot(AnimGraph) : nullptr;
	if (Root == nullptr || Skeleton == nullptr)
	{
		return;
	}

	AnimBlueprint->Modify();
	if (AnimBlueprint->ParentClass != UFortnitePortingPartsAnimInstance::StaticClass())
	{
		AnimBlueprint->ParentClass = UFortnitePortingPartsAnimInstance::StaticClass();
		FBlueprintEditorUtils::RefreshAllNodes(AnimBlueprint);
	}

	TArray<UEdGraphNode*> Nodes = AnimGraph->Nodes;
	for (UEdGraphNode* Node : Nodes)
	{
		if (Node != nullptr && Node != Root)
		{
			FBlueprintEditorUtils::RemoveNode(AnimBlueprint, Node, true);
		}
	}

	const int32 Y = Root->NodePosY;
	int32 X = Root->NodePosX - 400 - (BlinkBones.Num() + Chains.Num() + 2) * 300;

	// Unlike Leader Pose, Copy Pose keeps bones missing from the body (facial bones, tails) following their parents
	UAnimGraphNode_CopyPoseFromMesh* CopyPose = SpawnNode<UAnimGraphNode_CopyPoseFromMesh>(*AnimGraph, X, Y);
	CopyPose->Node.bUseAttachedParent = true;
	CopyPose->Node.bCopyCurves = true;
	UEdGraphPin* Current = FindPosePin(CopyPose, EGPD_Output);

	const FReferenceSkeleton& ReferenceSkeleton = Skeleton->GetReferenceSkeleton();
	if (BlinkBones.Num() > 0 || Chains.Num() > 0)
	{
		X += 300;
		UAnimGraphNode_LocalToComponentSpace* ToComponent = SpawnNode<UAnimGraphNode_LocalToComponentSpace>(*AnimGraph, X, Y);
		Connect(Current, FindPosePin(ToComponent, EGPD_Input));
		Current = FindPosePin(ToComponent, EGPD_Output);

		// Eye blink: Fortnite's blink pose (eyelid bone deltas) added on top, scaled by BlinkAlpha
		for (const FPartsBoneOffset& Offset : BlinkBones)
		{
			if (ReferenceSkeleton.FindBoneIndex(Offset.Bone) == INDEX_NONE)
			{
				continue;
			}

			X += 300;
			FGraphNodeCreator<UAnimGraphNode_ModifyBone> Creator(*AnimGraph);
			UAnimGraphNode_ModifyBone* Modify = Creator.CreateNode(false);
			Modify->NodePosX = X;
			Modify->NodePosY = Y;
			Modify->Node.BoneToModify.BoneName = Offset.Bone;
			Modify->Node.TranslationMode = BMM_Additive;
			Modify->Node.TranslationSpace = BCS_ParentBoneSpace;
			Modify->Node.Translation = Offset.Location;
			Modify->Node.RotationMode = BMM_Additive;
			Modify->Node.RotationSpace = BCS_ParentBoneSpace;
			Modify->Node.Rotation = Offset.Rotation;
			Modify->Node.ScaleMode = BMM_Ignore;
			Creator.Finalize();

			Connect(Current, FindPosePin(Modify, EGPD_Input));
			Current = FindPosePin(Modify, EGPD_Output);
			BindVariable(*AnimGraph, Modify, TEXT("Alpha"), GET_MEMBER_NAME_CHECKED(UFortnitePortingPartsAnimInstance, BlinkAlpha), X, Y + 220);
		}

		// Tails / ears: a spring chain that sways with the character's movement and settles back to its pose
		for (const FPartsChain& Chain : Chains)
		{
			if (ReferenceSkeleton.FindBoneIndex(Chain.Start) == INDEX_NONE || ReferenceSkeleton.FindBoneIndex(Chain.End) == INDEX_NONE)
			{
				continue;
			}

			// Trail controller: follows the parent's motion with a lag and always relaxes back to the authored
			// shape. Unlike AnimDynamics it can't collapse or explode, so hair/tails stay stable.
			const int32 StartIndex = ReferenceSkeleton.FindBoneIndex(Chain.Start);
			const int32 EndIndex = ReferenceSkeleton.FindBoneIndex(Chain.End);
			const int32 ControlIndex = ReferenceSkeleton.GetParentIndex(StartIndex);
			if (ControlIndex == INDEX_NONE)
			{
				continue;
			}
			int32 Length = 1;
			for (int32 Bone = EndIndex; Bone != INDEX_NONE && Bone != ControlIndex; Bone = ReferenceSkeleton.GetParentIndex(Bone))
			{
				Length++;
			}

			// Separate tail meshes are authored straight out (Fortnite sags them with rigid-body gravity): curve
			// each joint down a little so the tail hangs, and the Trail below keeps that shape as its rest pose
			if (Chain.Start.ToString().Contains(TEXT("_Tail_"), ESearchCase::CaseSensitive))
			{
				for (int32 Bone = EndIndex; Bone != INDEX_NONE && Bone != ControlIndex; Bone = ReferenceSkeleton.GetParentIndex(Bone))
				{
					X += 300;
					FGraphNodeCreator<UAnimGraphNode_ModifyBone> DroopCreator(*AnimGraph);
					UAnimGraphNode_ModifyBone* Droop = DroopCreator.CreateNode(false);
					Droop->NodePosX = X;
					Droop->NodePosY = Y;
					Droop->Node.BoneToModify.BoneName = ReferenceSkeleton.GetBoneName(Bone);
					Droop->Node.TranslationMode = BMM_Ignore;
					Droop->Node.ScaleMode = BMM_Ignore;
					Droop->Node.RotationMode = BMM_Additive;
					Droop->Node.RotationSpace = BCS_BoneSpace;
					Droop->Node.Rotation = FRotator(-9.0f, 0.0f, 0.0f);
					DroopCreator.Finalize();
					Connect(Current, FindPosePin(Droop, EGPD_Input));
					Current = FindPosePin(Droop, EGPD_Output);
				}
			}

			X += 300;
			FGraphNodeCreator<UAnimGraphNode_Trail> Creator(*AnimGraph);
			UAnimGraphNode_Trail* Dynamics = Creator.CreateNode(false);
			Dynamics->NodePosX = X;
			Dynamics->NodePosY = Y;
			FAnimNode_Trail& Node = Dynamics->Node;
			Node.TrailBone.BoneName = Chain.End;
			Node.ChainLength = Length;
			Node.RelaxationSpeedScale = 1.5f;
			Node.bLimitStretch = true;
			Node.StretchLimit = 0.0f;
			Node.bActorSpaceFakeVel = false;

			// Mirrored rigs (the _r side of Fortnite hair) point their bones down -X: aligning +X to the child
			// would spin those bones 180 degrees, twisting the strand inside out (it renders dark). Use the axis
			// the reference pose actually points along.
			{
				// Sum of the chain's segments: short diagonal offsets (e.g. base_/dyn_ pairs) don't decide the axis
				FVector ChildOffset = FVector::ZeroVector;
				for (int32 Bone = EndIndex; Bone != INDEX_NONE && Bone != ControlIndex; Bone = ReferenceSkeleton.GetParentIndex(Bone))
				{
					if (Bone != StartIndex || StartIndex == EndIndex)
					{
						ChildOffset += ReferenceSkeleton.GetRefBonePose()[Bone].GetTranslation();
					}
				}
				const FVector Abs = ChildOffset.GetAbs();
				const int32 Axis = Abs.X >= Abs.Y && Abs.X >= Abs.Z ? 0 : (Abs.Y >= Abs.Z ? 1 : 2);
				Node.ChainBoneAxis = Axis == 0 ? EAxis::X : (Axis == 1 ? EAxis::Y : EAxis::Z);
				Node.bInvertChainBoneAxis = ChildOffset[Axis] < 0.0;
			}
			Creator.Finalize();

			Connect(Current, FindPosePin(Dynamics, EGPD_Input));
			Current = FindPosePin(Dynamics, EGPD_Output);
			UE_LOG(LogFortnitePorting, Log, TEXT("%s: physics chain %s -> %s"), *AnimBlueprint->GetName(), *Chain.Start.ToString(), *Chain.End.ToString());
		}

		X += 300;
		UAnimGraphNode_ComponentToLocalSpace* ToLocal = SpawnNode<UAnimGraphNode_ComponentToLocalSpace>(*AnimGraph, X, Y);
		Connect(Current, FindPosePin(ToLocal, EGPD_Input));
		Current = FindPosePin(ToLocal, EGPD_Output);
	}

	Connect(Current, FindPosePin(Root, EGPD_Input));
	Compile(AnimBlueprint);
	UE_LOG(LogFortnitePorting, Log, TEXT("%s rebuilt: %d blink bones, %d physics chains"), *AnimBlueprint->GetName(), BlinkBones.Num(), Chains.Num());
}
