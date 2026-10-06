#include "ArenaBuildPiece.h"

#include "ArenaBuildFeedback.h"
#include "ArenaBuildSubsystem.h"
#include "Components/BoxComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicsEngine/BodySetup.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"

namespace
{
	// The exported Fortnite meshes carry simple collision that does not match the shape (a cone is a block, a door is solid),
	// so the triangles themselves are the collision, as in the game. The flag lives on the asset: once per mesh is enough.
	void UseMeshTrianglesForCollision(UStaticMesh* Mesh)
	{
		UBodySetup* Body = Mesh ? Mesh->GetBodySetup() : nullptr;
		if (Body && Body->CollisionTraceFlag != CTF_UseComplexAsSimple)
		{
			Body->CollisionTraceFlag = CTF_UseComplexAsSimple;
		}
	}
}

AArenaBuildPiece::AArenaBuildPiece()
{
	bReplicates = true;
	SetReplicatingMovement(true);
	SetNetUpdateFrequency(10.0f);
	PrimaryActorTick.bCanEverTick = false;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	Mesh->SetCanEverAffectNavigation(false);
	RootComponent = Mesh;

	StairCollisionRamp = CreateDefaultSubobject<UBoxComponent>(TEXT("StairCollisionRamp"));
	StairCollisionRamp->SetupAttachment(RootComponent);
	StairCollisionRamp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	StairCollisionRamp->SetCollisionResponseToAllChannels(ECR_Ignore);
	StairCollisionRamp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	StairCollisionRamp->SetCanEverAffectNavigation(false);
	StairCollisionRamp->CanCharacterStepUpOn = ECB_Yes;
}

void AArenaBuildPiece::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	ApplyLook();
}

void AArenaBuildPiece::Init(EArenaBuildPiece InPiece, EArenaBuildMaterial InMaterial, int32 InShapeIndex, bool bInMirror, int32 InRotation, const FIntVector& InCell, int32 InEdge, APawn* InBuilder, int32 InBuildTextureIndex)
{
	Piece = InPiece;
	Material = InMaterial;
	ShapeIndex = InShapeIndex;
	bMirror = bInMirror;
	Rotation = InRotation;
	BuildTextureIndex = FMath::Clamp(InBuildTextureIndex, 1, 4);
	Cell = InCell;
	Edge = InEdge;
	Builder = InBuilder;
	if (const UWorld* World = GetWorld())
	{
		BuiltServerTime = World->GetGameState() ? World->GetGameState()->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
	}
	Key = ArenaBuild::MakeKey(Piece, Cell, Edge);
	Health = ArenaBuild::MaxHealth(Material);
	ApplyLook();
	if (UArenaBuildSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UArenaBuildSubsystem>() : nullptr)
	{
		Subsystem->Register(Key, this);
	}
	PlayBuiltFeedback();      // the server (and a standalone game) never gets a BeginPlay after Init
}

void AArenaBuildPiece::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority())
	{
		const UWorld* World = GetWorld();
		const float Now = World && World->GetGameState() ? World->GetGameState()->GetServerWorldTimeSeconds() : (World ? World->GetTimeSeconds() : 0.0f);
		if (Now - BuiltServerTime < 1.5f)
		{
			PlayBuiltFeedback();
		}
	}
}

void AArenaBuildPiece::PlayBuiltFeedback()
{
	ArenaBuildFeedback::PlayBuilt(this, Material, GetActorLocation());
	if (Builder)
	{
		ArenaBuildFeedback::PlayAnimation(Builder, ArenaBuildFeedback::EAnim::Place);
	}
}

void AArenaBuildPiece::MulticastEdited_Implementation(APawn* Editor)
{
	ArenaBuildFeedback::PlayAt(this, TEXT("Fort_Build_BluePrint_EnemyConfirmEdit_Cue"), GetActorLocation());
	if (Editor)
	{
		ArenaBuildFeedback::PlayAnimation(Editor, ArenaBuildFeedback::EAnim::Edit);
	}
}

void AArenaBuildPiece::SetShape(int32 InShapeIndex, bool bInMirror, int32 InRotation)
{
	ShapeIndex = InShapeIndex;
	bMirror = bInMirror;
	Rotation = InRotation;
	ApplyLook();
}

void AArenaBuildPiece::OnRep_Look()
{
	ApplyLook();
}

void AArenaBuildPiece::ApplyLook()
{
	if (!Mesh)
	{
		return;
	}
	if (StairCollisionRamp)
	{
		StairCollisionRamp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	// roof faces are close to 45 degrees, steeper than a character walks by default; a wall or floor keeps the normal rule
	Mesh->SetWalkableSlopeOverride(Piece == EArenaBuildPiece::Roof ? FWalkableSlopeOverride(WalkableSlope_Increase, 80.0f) : FWalkableSlopeOverride());
	// the pickaxe reads this tag to pick its impact sound
	Tags.Reset();
	Tags.Add(Material == EArenaBuildMaterial::Metal ? FName(TEXT("Surface_Metal")) : (Material == EArenaBuildMaterial::Brick ? FName(TEXT("Surface_Stone")) : FName(TEXT("Surface_Wood"))));
	if (UStaticMesh* NewMesh = ArenaBuild::LoadMesh(Material, ShapeIndex))
	{
		UseMeshTrianglesForCollision(NewMesh);      // before SetStaticMesh: that is when the physics body is built
		Mesh->SetStaticMesh(NewMesh);
		// the straight stair is walked on a smooth ramp, as in the game, whatever its material: the steps and rails are only looks
		if (Piece == EArenaBuildPiece::Stair
			&& ArenaBuild::Shapes().IsValidIndex(ShapeIndex)
			&& FCString::Strcmp(ArenaBuild::Shapes()[ShapeIndex].Id, TEXT("StairW")) == 0
			&& StairCollisionRamp)
		{
			const FBox Bounds = NewMesh->GetBoundingBox();
			const FVector Extent = Bounds.GetExtent();
			const float RampLength = FMath::Sqrt(FMath::Square(Extent.Y) + FMath::Square(Extent.Z));
			const float RampRoll = FMath::RadiansToDegrees(FMath::Atan2(Extent.Z, Extent.Y));
			StairCollisionRamp->SetBoxExtent(FVector(Extent.X, RampLength + 2.0f, 30.0f));
			StairCollisionRamp->SetRelativeLocation(Bounds.GetCenter());
			StairCollisionRamp->SetRelativeRotation(FRotator(0.0f, 0.0f, RampRoll));
			Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
			StairCollisionRamp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		}
	}
	if (BuildTextureIndex > 0)
	{
		const FString TextureName = FString::Printf(TEXT("T_BuildTexture%d"), BuildTextureIndex);
		UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *FString::Printf(TEXT("/Game/Arena/BuildTextures/%s.%s"), *TextureName, *TextureName));
		UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Arena/Materials/M_BuildTexture.M_BuildTexture"));
		if (Texture && BaseMaterial)
		{
			if (UMaterialInstanceDynamic* TextureMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this))
			{
				TextureMaterial->SetTextureParameterValue(TEXT("BuildTexture"), Texture);
				for (int32 MaterialIndex = 0; MaterialIndex < Mesh->GetNumMaterials(); ++MaterialIndex)
				{
					Mesh->SetMaterial(MaterialIndex, TextureMaterial);
				}
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Could not load build texture option %d or its material."), BuildTextureIndex);
		}
	}
	// a mirrored shape is the same mesh seen in a mirror: the engine turns the faces around for a negative scale
	Mesh->SetRelativeScale3D(FVector(bMirror ? -1.0f : 1.0f, 1.0f, 1.0f));
}

uint16 AArenaBuildPiece::GetCurrentMask() const
{
	return ArenaBuild::Transform(Piece, ArenaBuild::MaskOf(ShapeIndex), bMirror, Rotation);
}

bool AArenaBuildPiece::IsSpiral() const
{
	return ArenaBuild::Shapes().IsValidIndex(ShapeIndex) && ArenaBuild::Shapes()[ShapeIndex].bSpiral;
}

namespace
{
	/** Small cubes of the piece's material that burst out and fall: the chips of a hit and the pieces of a break */
	void SpawnChips(UWorld* World, UMaterialInterface* Material, const FVector& Origin, const FVector& Extent, int32 Count, float Scale, float Burst)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (!World || !Cube)
		{
			return;
		}
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FActorSpawnParameters Params;
			Params.ObjectFlags |= RF_Transient;
			const FVector Spot = Origin + FVector(FMath::FRandRange(-1.0f, 1.0f), FMath::FRandRange(-1.0f, 1.0f), FMath::FRandRange(-1.0f, 1.0f)) * Extent;
			AStaticMeshActor* Chip = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform(FRotator(FMath::FRand() * 360.0f, FMath::FRand() * 360.0f, FMath::FRand() * 360.0f), Spot, FVector(Scale * FMath::FRandRange(0.6f, 1.4f))), Params);
			if (!Chip)
			{
				continue;
			}
			UStaticMeshComponent* Component = Chip->GetStaticMeshComponent();
			Component->SetMobility(EComponentMobility::Movable);
			Component->SetStaticMesh(Cube);
			if (Material)
			{
				Component->SetMaterial(0, Material);
			}
			Component->SetCollisionProfileName(TEXT("PhysicsActor"));
			Component->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
			Component->SetCastShadow(false);
			Component->SetSimulatePhysics(true);
			Component->AddImpulse((Spot - Origin).GetSafeNormal() * Burst + FVector(FMath::FRandRange(-1.0f, 1.0f), FMath::FRandRange(-1.0f, 1.0f), FMath::FRandRange(0.3f, 1.2f)) * Burst * 0.6f, NAME_None, true);
			Chip->SetLifeSpan(1.6f + FMath::FRand() * 1.2f);
		}
	}
}

float AArenaBuildPiece::TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (!HasAuthority() || Damage <= 0.0f || bBroken)
	{
		return 0.0f;
	}
	Health -= Damage;
	LastHitTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;

	FVector Point = Mesh ? Mesh->Bounds.Origin : GetActorLocation();
	if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		Point = static_cast<const FPointDamageEvent&>(DamageEvent).HitInfo.ImpactPoint;
	}
	if (Health <= 0.0f)
	{
		bBroken = true;
		MulticastBroken(Point);
		SetActorEnableCollision(false);
		SetLifeSpan(0.3f);
	}
	else
	{
		MulticastDamaged(Point, FMath::Clamp(Health / GetMaxHealth(), 0.0f, 1.0f));
	}
	return Damage;
}

void AArenaBuildPiece::MulticastDamaged_Implementation(FVector Point, float HealthFraction)
{
	// the lower its health, the more chips fly
	const int32 Count = HealthFraction < 0.35f ? 6 : 4;
	SpawnChips(GetWorld(), Mesh ? Mesh->GetMaterial(0) : nullptr, Point, FVector(12.0f), Count, 0.045f, 260.0f);
}

void AArenaBuildPiece::MulticastBroken_Implementation(FVector Point)
{
	if (Mesh)
	{
		SetActorHiddenInGame(true);
		SetActorEnableCollision(false);
		// the pieces come out of the whole piece, not only from where it was hit
		const FBoxSphereBounds Bounds = Mesh->Bounds;
		SpawnChips(GetWorld(), Mesh->GetMaterial(0), Bounds.Origin, Bounds.BoxExtent * 0.8f, 14, 0.22f, 380.0f);
	}
}

void AArenaBuildPiece::CollapseAfter(float Delay)
{
	if (!HasAuthority() || bCollapsing || bBroken || !GetWorld())
	{
		return;
	}
	bCollapsing = true;
	GetWorld()->GetTimerManager().SetTimer(CollapseTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (!bBroken)
		{
			bBroken = true;
			MulticastBroken(Mesh ? Mesh->Bounds.Origin : GetActorLocation());
			SetActorEnableCollision(false);
			SetLifeSpan(0.3f);
		}
	}), FMath::Max(Delay, 0.02f), false);
}

float AArenaBuildPiece::GetMaxHealth() const
{
	return ArenaBuild::MaxHealth(Material);
}

void AArenaBuildPiece::OnRep_Health(float OldHealth)
{
	if (Health < OldHealth && GetWorld())
	{
		LastHitTime = GetWorld()->GetTimeSeconds();
	}
}

void AArenaBuildPiece::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
	{
		if (UArenaBuildSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UArenaBuildSubsystem>() : nullptr)
		{
			Subsystem->Unregister(Key, this);
			if (EndPlayReason == EEndPlayReason::Destroyed)
			{
				Subsystem->RequestSupportCheck();      // whatever stood on this piece may fall now
			}
		}
	}
	Super::EndPlay(EndPlayReason);
}

void AArenaBuildPiece::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AArenaBuildPiece, Piece);
	DOREPLIFETIME(AArenaBuildPiece, Material);
	DOREPLIFETIME(AArenaBuildPiece, ShapeIndex);
	DOREPLIFETIME(AArenaBuildPiece, bMirror);
	DOREPLIFETIME(AArenaBuildPiece, Rotation);
	DOREPLIFETIME(AArenaBuildPiece, BuildTextureIndex);
	DOREPLIFETIME(AArenaBuildPiece, Health);
	DOREPLIFETIME(AArenaBuildPiece, Key);
	DOREPLIFETIME(AArenaBuildPiece, Cell);
	DOREPLIFETIME(AArenaBuildPiece, Edge);
	DOREPLIFETIME(AArenaBuildPiece, Builder);
	DOREPLIFETIME(AArenaBuildPiece, BuiltServerTime);
}
