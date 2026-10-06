#include "ArenaBuildTypes.h"

#include "Engine/StaticMesh.h"
#include "UObject/UObjectGlobals.h"

namespace ArenaBuild
{
	// "#" present tile, "." missing tile, rows concatenated
	static constexpr uint16 M(const char* Tiles)
	{
		uint16 Mask = 0;
		for (int32 Index = 0; Tiles[Index]; ++Index)
		{
			if (Tiles[Index] == '#')
			{
				Mask |= (1 << Index);
			}
		}
		return Mask;
	}

	static const TArray<FShape>& MakeShapes()
	{
		using P = EArenaBuildPiece;
		static const TArray<FShape> Table = {
			// walls: 3x3, top row first, columns grow toward +X (the patterns of the EMP_Wall_* assets)
			{ P::Wall, TEXT("Solid"),               M("#########"),  false, TEXT("Solid"),               TEXT("Solid"),               TEXT("Solid") },
			{ P::Wall, TEXT("DoorC"),               M("###" "#.#" "#.#"), false, TEXT("DoorC"),          TEXT("DoorC"),               TEXT("DoorC") },
			{ P::Wall, TEXT("DoorS"),               M("###" ".#." ".##"), false, TEXT("DoorS"),          TEXT("DoorS"),               TEXT("DoorS") },
			{ P::Wall, TEXT("DoorSide"),            M("###" ".##" ".##"), false, TEXT("DoorSide"),       TEXT("DoorSide"),            TEXT("DoorSide") },
			{ P::Wall, TEXT("HalfWallDoor"),        M("..." "#.#" "#.#"), false, TEXT("HalfWallDoor"),   TEXT("HalfWallDoor"),        TEXT("HalfWallDoor") },
			{ P::Wall, TEXT("HalfWallDoorS"),       M("..." ".##" ".##"), false, TEXT("HalfWallDoorS"),  TEXT("HalfWallDoorS"),       TEXT("HalfWallDoorS") },
			{ P::Wall, TEXT("HalfWallHalf"),        M("..." "#.." "#.."), false, TEXT("HalfWallHalf"),   TEXT("HalfWallHalf"),        TEXT("HalfWallHalf") },
			{ P::Wall, TEXT("HalfWallS"),           M("..." "###" "###"), false, TEXT("HalfWallS"),      TEXT("HalfWallS"),           TEXT("HalfWall") },
			{ P::Wall, TEXT("QuarterWallHalf"),     M("..." "..." "##."), false, TEXT("QuarterWallHalf"), TEXT("QuarterWallHalf"),    TEXT("QuarterWallHalf") },
			{ P::Wall, TEXT("QuarterWallS"),        M("..." "..." "###"), false, TEXT("QuarterWallS"),   TEXT("QuarterWallS"),        TEXT("QuarterWallS") },
			{ P::Wall, TEXT("WindowC"),             M("###" "#.#" "###"), false, TEXT("WindowC"),        TEXT("WindowC"),             TEXT("WindowC") },
			{ P::Wall, TEXT("Windows"),             M("###" ".#." "###"), false, TEXT("Windows"),        TEXT("Windows"),             TEXT("Windows") },
			{ P::Wall, TEXT("WindowSide"),          M("###" ".##" "###"), false, TEXT("WindowSide"),     TEXT("WindowSide"),          TEXT("WindowSide") },
			{ P::Wall, TEXT("Archway"),             M("###" "#.#" "..."), false, TEXT("Archway"),        TEXT("Archway"),             TEXT("Archway") },
			{ P::Wall, TEXT("ArchwayLarge"),        M("###" "..#" "..#"), false, TEXT("ArchwayLarge"),   TEXT("ArchwayLarge"),        TEXT("ArchwayLarge") },
			{ P::Wall, TEXT("ArchwayLargeSupport"), M("..#" "..#" "..#"), false, TEXT("ArchwaySupportLarge"), TEXT("ArchwayLargeSupport"), TEXT("ArchwayLargeSupport") },
			{ P::Wall, TEXT("Brace"),               M("###" ".##" "..#"), false, TEXT("Brace"),          TEXT("Brace"),               TEXT("Brace") },
			{ P::Wall, TEXT("RoofWall"),            M("#.." "##." "###"), false, TEXT("RoofWall"),       TEXT("RoofWall"),            TEXT("RoofWall") },

			// floors: 2x2, near row first (row 1 is the far half), columns grow toward +X
			{ P::Floor, TEXT("Floor"),              M("##" "##"),    false, TEXT("Floor"),               TEXT("Floor"),               TEXT("Floor") },
			{ P::Floor, TEXT("BalconyL"),           M("#." "##"),    false, TEXT("BalconyI"),            TEXT("BalconyI"),            TEXT("BalconyI") },
			{ P::Floor, TEXT("BalconyS"),           M(".." "##"),    false, TEXT("BalconyS"),            TEXT("BalconyS"),            TEXT("BalconyS") },
			{ P::Floor, TEXT("BalconyD"),           M("#." ".#"),    false, TEXT("BalconyD"),            TEXT("BalconyD"),            TEXT("BalconyD") },
			{ P::Floor, TEXT("BalconyO"),           M(".." "#."),    false, TEXT("BalconyO"),            TEXT("BalconyO"),            TEXT("BalconyO") },

			// roofs: the game stores the REMOVED tiles; these are the present ones (RoofS removed "##.." leaves the far half, ...)
			{ P::Roof, TEXT("RoofC"),               M("##" "##"),    false, TEXT("RoofC"),               TEXT("RoofC"),               TEXT("RoofC") },
			{ P::Roof, TEXT("RoofS"),               M(".." "##"),    false, TEXT("RoofS"),               TEXT("RoofS"),               TEXT("RoofS") },
			{ P::Roof, TEXT("RoofD"),               M(".#" "#."),    false, TEXT("RoofD"),               TEXT("RoofD"),               TEXT("RoofD") },
			{ P::Roof, TEXT("RoofO"),               M("#." "##"),    false, TEXT("RoofO"),               TEXT("RoofO"),               TEXT("RoofO") },
			{ P::Roof, TEXT("RoofI"),               M(".." "#."),    false, TEXT("RoofI"),               TEXT("RoofI"),               TEXT("RoofI") },

			// stairs: W straight, F half width, T turning (three tiles), R spiral (four tiles)
			{ P::Stair, TEXT("StairW"),             M("##" "##"),    false, TEXT("StairW"),              TEXT("StairW"),              TEXT("StairW") },
			{ P::Stair, TEXT("StairF"),             M("#." "#."),    false, TEXT("StairF"),              TEXT("StairF"),              TEXT("StairF") },
			{ P::Stair, TEXT("StairT"),             M("##" "#."),    false, TEXT("StairT"),              TEXT("StairT"),              TEXT("StairT") },
			{ P::Stair, TEXT("StairR"),             M("##" "##"),    true,  TEXT("StairR"),              TEXT("StairR"),              TEXT("StairR") },
		};
		return Table;
	}

	const TArray<FShape>& Shapes()
	{
		return MakeShapes();
	}

	int32 TileCount(EArenaBuildPiece Piece)
	{
		return Piece == EArenaBuildPiece::Wall ? 9 : 4;
	}

	uint16 FullMask(EArenaBuildPiece Piece)
	{
		return Piece == EArenaBuildPiece::Wall ? 0x1FF : 0xF;
	}

	int32 DefaultShape(EArenaBuildPiece Piece)
	{
		const TArray<FShape>& Table = Shapes();
		for (int32 Index = 0; Index < Table.Num(); ++Index)
		{
			if (Table[Index].Piece == Piece)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	uint16 MaskOf(int32 ShapeIndex)
	{
		return Shapes().IsValidIndex(ShapeIndex) ? Shapes()[ShapeIndex].Mask : 0;
	}

	uint16 Transform(EArenaBuildPiece Piece, uint16 Mask, bool bMirror, int32 Rotation)
	{
		uint16 Result = 0;
		if (Piece == EArenaBuildPiece::Wall)
		{
			for (int32 Row = 0; Row < 3; ++Row)
			{
				for (int32 Col = 0; Col < 3; ++Col)
				{
					if (Mask & (1 << (Row * 3 + Col)))
					{
						Result |= (1 << (Row * 3 + (bMirror ? 2 - Col : Col)));
					}
				}
			}
			return Result;
		}
		for (int32 Row = 0; Row < 2; ++Row)
		{
			for (int32 Col = 0; Col < 2; ++Col)
			{
				if (!(Mask & (1 << (Row * 2 + Col))))
				{
					continue;
				}
				// tile centre around the cell centre (x grows with the column, y with the row), mirror first, then turn counter clockwise
				int32 X = Col * 2 - 1;
				int32 Y = Row * 2 - 1;
				if (bMirror)
				{
					X = -X;
				}
				for (int32 Step = 0; Step < (Rotation & 3); ++Step)
				{
					const int32 NewX = -Y;
					Y = X;
					X = NewX;
				}
				Result |= (1 << ((Y > 0 ? 1 : 0) * 2 + (X > 0 ? 1 : 0)));
			}
		}
		return Result;
	}

	FResolved Resolve(EArenaBuildPiece Piece, uint16 Mask, bool bSpiral)
	{
		const TArray<FShape>& Table = Shapes();
		const int32 Turns = Piece == EArenaBuildPiece::Wall ? 1 : 4;
		for (int32 Index = 0; Index < Table.Num(); ++Index)
		{
			const FShape& Shape = Table[Index];
			if (Shape.Piece != Piece || (Piece == EArenaBuildPiece::Stair && Shape.bSpiral != bSpiral))
			{
				continue;
			}
			for (int32 Mirror = 0; Mirror < 2; ++Mirror)
			{
				for (int32 Turn = 0; Turn < Turns; ++Turn)
				{
					if (Transform(Piece, Shape.Mask, Mirror != 0, Turn) == Mask)
					{
						FResolved Result;
						Result.ShapeIndex = Index;
						Result.bMirror = Mirror != 0;
						Result.Rotation = Turn;
						return Result;
					}
				}
			}
		}
		return FResolved();
	}

	FString MeshPath(EArenaBuildMaterial Material, int32 ShapeIndex)
	{
		if (!Shapes().IsValidIndex(ShapeIndex))
		{
			return FString();
		}
		const FShape& Shape = Shapes()[ShapeIndex];
		const TCHAR* Folder = TEXT("Wood");
		const TCHAR* Letter = TEXT("W");
		const TCHAR* Suffix = Shape.MeshWood;
		if (Material == EArenaBuildMaterial::Brick)
		{
			Folder = TEXT("Brick");
			Letter = TEXT("B");
			Suffix = Shape.MeshBrick;
		}
		else if (Material == EArenaBuildMaterial::Metal)
		{
			Folder = TEXT("Metal");
			Letter = TEXT("M");
			Suffix = Shape.MeshMetal;
		}
		const FString Name = FString::Printf(TEXT("PBW_%s1_%s"), Letter, Suffix);
		return FString::Printf(TEXT("/Game/Packages/PBW/%s/L1/%s.%s"), Folder, *Name, *Name);
	}

	UStaticMesh* LoadMesh(EArenaBuildMaterial Material, int32 ShapeIndex)
	{
		static TMap<FString, TWeakObjectPtr<UStaticMesh>> Cache;
		const FString Path = MeshPath(Material, ShapeIndex);
		if (Path.IsEmpty())
		{
			return nullptr;
		}
		if (const TWeakObjectPtr<UStaticMesh>* Found = Cache.Find(Path))
		{
			if (Found->IsValid())
			{
				return Found->Get();
			}
		}
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path);
		Cache.Add(Path, Mesh);
		return Mesh;
	}

	FTransform PieceTransform(EArenaBuildPiece Piece, const FIntVector& Cell, int32 Edge, int32 Rotation)
	{
		const float Z = Cell.Z * Story;
		const float Half = CellSize * 0.5f;
		if (Piece == EArenaBuildPiece::Wall)
		{
			if (Edge == 2)
			{
				return FTransform(FRotator(0.0f, 90.0f, 0.0f), FVector(Cell.X * CellSize, Cell.Y * CellSize + Half, Z));
			}
			return FTransform(FRotator::ZeroRotator, FVector(Cell.X * CellSize + Half, Cell.Y * CellSize, Z));
		}
		const float Yaw = 90.0f * (Rotation & 3);
		const FVector Centre(Cell.X * CellSize + Half, Cell.Y * CellSize + Half, Z);
		const FVector Pivot = Centre + FRotator(0.0f, Yaw, 0.0f).RotateVector(FVector(0.0f, -Half, 0.0f));
		return FTransform(FRotator(0.0f, Yaw, 0.0f), Pivot);
	}

	FString MakeKey(EArenaBuildPiece Piece, const FIntVector& Cell, int32 Edge)
	{
		// walls of one edge share a slot whatever their shape; floors, roofs and stairs each have their own slot per cell
		return FString::Printf(TEXT("%d_%d_%d_%d_%d"), static_cast<int32>(Piece), Cell.X, Cell.Y, Cell.Z, Piece == EArenaBuildPiece::Wall ? Edge : 0);
	}

	float MaxHealth(EArenaBuildMaterial Material)
	{
		switch (Material)
		{
		case EArenaBuildMaterial::Brick: return 300.0f;
		case EArenaBuildMaterial::Metal: return 500.0f;
		default: return 150.0f;
		}
	}

	const TCHAR* PieceName(EArenaBuildPiece Piece)
	{
		switch (Piece)
		{
		case EArenaBuildPiece::Wall: return TEXT("Pared");
		case EArenaBuildPiece::Floor: return TEXT("Suelo");
		case EArenaBuildPiece::Stair: return TEXT("Escalera");
		default: return TEXT("Tejado");
		}
	}

	const TCHAR* MaterialName(EArenaBuildMaterial Material)
	{
		switch (Material)
		{
		case EArenaBuildMaterial::Brick: return TEXT("Piedra");
		case EArenaBuildMaterial::Metal: return TEXT("Metal");
		default: return TEXT("Madera");
		}
	}
}
