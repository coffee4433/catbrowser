#include "ArenaBuildSubsystem.h"

#include "ArenaBuildPiece.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "TimerManager.h"

void UArenaBuildSubsystem::Register(const FString& Key, AArenaBuildPiece* Piece)
{
	Slots.Add(Key, Piece);
}

void UArenaBuildSubsystem::Unregister(const FString& Key, AArenaBuildPiece* Piece)
{
	if (const TWeakObjectPtr<AArenaBuildPiece>* Found = Slots.Find(Key))
	{
		if (!Found->IsValid() || Found->Get() == Piece)
		{
			Slots.Remove(Key);
		}
	}
}

AArenaBuildPiece* UArenaBuildSubsystem::Find(const FString& Key) const
{
	if (const TWeakObjectPtr<AArenaBuildPiece>* Found = Slots.Find(Key))
	{
		return Found->Get();
	}
	return nullptr;
}

void UArenaBuildSubsystem::RequestSupportCheck()
{
	UWorld* World = GetWorld();
	if (!World || World->bIsTearingDown || World->GetTimerManager().IsTimerActive(SupportTimer))
	{
		return;
	}
	World->GetTimerManager().SetTimer(SupportTimer, FTimerDelegate::CreateUObject(this, &UArenaBuildSubsystem::RunSupportCheck), 0.35f, false);
}

void UArenaBuildSubsystem::RunSupportCheck()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// Pieces touching each other form one structure; a structure that touches the ground stands, one that does not falls.
	TArray<AArenaBuildPiece*> Pieces;
	TArray<FBox> Boxes;
	for (TActorIterator<AArenaBuildPiece> It(World); It; ++It)
	{
		AArenaBuildPiece* Piece = *It;
		if (!IsValid(Piece) || Piece->IsActorBeingDestroyed() || Piece->IsCollapsing() || Piece->GetLifeSpan() > 0.0f)
		{
			continue;
		}
		Pieces.Add(Piece);
		Boxes.Add(Piece->GetComponentsBoundingBox().ExpandBy(28.0f));
	}

	TArray<bool> Standing;
	Standing.Init(false, Pieces.Num());
	TArray<int32> Queue;
	for (int32 Index = 0; Index < Pieces.Num(); ++Index)
	{
		if (Boxes[Index].Min.Z <= 40.0f)      // touches the ground (the box was grown by 28)
		{
			Standing[Index] = true;
			Queue.Add(Index);
		}
	}
	for (int32 Head = 0; Head < Queue.Num(); ++Head)
	{
		const int32 Current = Queue[Head];
		for (int32 Other = 0; Other < Pieces.Num(); ++Other)
		{
			if (!Standing[Other] && Boxes[Current].Intersect(Boxes[Other]))
			{
				Standing[Other] = true;
				Queue.Add(Other);
			}
		}
	}

	TArray<int32> Falling;
	for (int32 Index = 0; Index < Pieces.Num(); ++Index)
	{
		if (!Standing[Index])
		{
			Falling.Add(Index);
		}
	}
	Falling.Sort([&Boxes](int32 A, int32 B) { return Boxes[A].Min.Z < Boxes[B].Min.Z; });
	for (int32 Order = 0; Order < Falling.Num(); ++Order)
	{
		Pieces[Falling[Order]]->CollapseAfter(0.08f + Order * 0.07f);      // from the bottom to the top
	}
}
