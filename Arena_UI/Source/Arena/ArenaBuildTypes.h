// Fortnite style building: the piece catalogue (shapes taken from the game's EditModePatterns), and the grid math.
//
// Every original piece mesh hangs from the MIDPOINT OF ITS SOUTH EDGE and reaches +Y (floors, stairs and roofs 512 deep, walls 384 tall
// standing on that edge). A "cell" is 512x512 cm and a "story" is 384 cm.

#pragma once

#include "CoreMinimal.h"
#include "ArenaBuildTypes.generated.h"

class UStaticMesh;

UENUM(BlueprintType)
enum class EArenaBuildPiece : uint8
{
	Wall,
	Floor,
	Stair,
	Roof,
};

UENUM(BlueprintType)
enum class EArenaBuildMaterial : uint8
{
	Wood,
	Brick,
	Metal,
};

namespace ArenaBuild
{
	constexpr float CellSize = 512.0f;
	constexpr float Story = 384.0f;

	/** One editable shape of a piece type. Masks use one bit per tile, row by row (walls 3x3 from the top, the rest 2x2 from the near edge) */
	struct FShape
	{
		EArenaBuildPiece Piece;
		const TCHAR* Id;          // EditModePattern name, for debugging
		uint16 Mask;              // tiles that are PRESENT
		bool bSpiral;             // stairs only: the 4 tile spiral
		const TCHAR* MeshWood;    // mesh suffix per material (the files are not named the same everywhere)
		const TCHAR* MeshBrick;
		const TCHAR* MeshMetal;
	};

	/** The result of looking a tile mask up: which shape, mirrored left to right, and turned in 90 degree steps (floors, roofs, stairs) */
	struct FResolved
	{
		int32 ShapeIndex = INDEX_NONE;
		bool bMirror = false;
		int32 Rotation = 0;
		bool IsValid() const { return ShapeIndex != INDEX_NONE; }
	};

	ARENA_API const TArray<FShape>& Shapes();

	/** 9 tiles for walls, 4 for everything else */
	ARENA_API int32 TileCount(EArenaBuildPiece Piece);

	/** All tiles present: the shape a piece starts as */
	ARENA_API uint16 FullMask(EArenaBuildPiece Piece);

	/** Index of the shape a freshly built piece starts as */
	ARENA_API int32 DefaultShape(EArenaBuildPiece Piece);

	/** The tile mask of a shape as it is drawn in the edit grid (roofs show present tiles, the game stores the removed ones) */
	ARENA_API uint16 MaskOf(int32 ShapeIndex);

	/** Finds the shape that gives the requested tiles, trying every mirror and turn that makes sense for the piece */
	ARENA_API FResolved Resolve(EArenaBuildPiece Piece, uint16 Mask, bool bSpiral);

	/** Mask of a shape after a mirror and turn (the inverse of Resolve) */
	ARENA_API uint16 Transform(EArenaBuildPiece Piece, uint16 Mask, bool bMirror, int32 Rotation);

	ARENA_API FString MeshPath(EArenaBuildMaterial Material, int32 ShapeIndex);
	ARENA_API UStaticMesh* LoadMesh(EArenaBuildMaterial Material, int32 ShapeIndex);

	/**
	 * Where a piece stands. Walls sit on a cell EDGE (Edge 1: the south edge of cell (X,Y), along X; Edge 2: the west edge, along Y),
	 * everything else fills the cell and is turned in 90 degree steps around the cell centre.
	 */
	ARENA_API FTransform PieceTransform(EArenaBuildPiece Piece, const FIntVector& Cell, int32 Edge, int32 Rotation);

	/** The slot a piece takes: the same key means the same place */
	ARENA_API FString MakeKey(EArenaBuildPiece Piece, const FIntVector& Cell, int32 Edge);

	ARENA_API float MaxHealth(EArenaBuildMaterial Material);
	ARENA_API const TCHAR* PieceName(EArenaBuildPiece Piece);
	ARENA_API const TCHAR* MaterialName(EArenaBuildMaterial Material);
}
