// Server side bookkeeping of which grid slot holds which piece, so two players cannot build in the same place.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ArenaBuildSubsystem.generated.h"

class AArenaBuildPiece;

UCLASS()
class ARENA_API UArenaBuildSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:

	void Register(const FString& Key, AArenaBuildPiece* Piece);
	void Unregister(const FString& Key, AArenaBuildPiece* Piece);

	/** The piece in that slot, if it is still alive */
	AArenaBuildPiece* Find(const FString& Key) const;

	/** A piece was destroyed: soon after, every piece that no longer reaches the ground through other pieces collapses, bottom first */
	void RequestSupportCheck();

private:

	void RunSupportCheck();
	FTimerHandle SupportTimer;

	TMap<FString, TWeakObjectPtr<AArenaBuildPiece>> Slots;
};
