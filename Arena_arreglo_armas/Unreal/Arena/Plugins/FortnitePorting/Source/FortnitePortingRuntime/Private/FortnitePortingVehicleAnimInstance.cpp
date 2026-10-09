#include "FortnitePortingVehicleAnimInstance.h"

#include "Animation/AnimTypes.h"
#include "BoneContainer.h"
#include "BonePose.h"

FAnimInstanceProxy* UFortnitePortingVehicleAnimInstance::CreateAnimInstanceProxy()
{
	return new FFortnitePortingVehicleAnimProxy(this);
}

void FFortnitePortingVehicleAnimProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	if (const UFortnitePortingVehicleAnimInstance* Instance = Cast<UFortnitePortingVehicleAnimInstance>(InAnimInstance))
	{
		WheelAngle = Instance->WheelAngle;
		SteerAngle = Instance->SteerAngle;
		WheelAxis = Instance->WheelAxis;
		bHideKickstand = Instance->bHideKickstand;
	}
}

bool FFortnitePortingVehicleAnimProxy::Evaluate(FPoseContext& Output)
{
	Output.ResetToRefPose();

	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	const FReferenceSkeleton& Ref = Bones.GetReferenceSkeleton();
	const TArray<FTransform>& RefPose = Ref.GetRefBonePose();

	// Rotation of a bone in mesh space in the reference pose
	auto MeshSpaceRotation = [&](int32 MeshBoneIndex)
	{
		FQuat Rotation = FQuat::Identity;
		for (int32 Index = MeshBoneIndex; Index != INDEX_NONE; Index = Ref.GetParentIndex(Index))
		{
			Rotation = RefPose[Index].GetRotation() * Rotation;
		}
		return Rotation;
	};

	for (int32 MeshBoneIndex = 0; MeshBoneIndex < Ref.GetNum(); ++MeshBoneIndex)
	{
		const FString Name = Ref.GetBoneName(MeshBoneIndex).ToString();
		float Angle = 0.0f;
		FVector MeshAxis = WheelAxis;
		if (Name.StartsWith(TEXT("tire_")))
		{
			Angle = WheelAngle;
		}
		// Only the front wheels steer; the rear ones keep pointing straight
		else if (Name.StartsWith(TEXT("wheel_steering_")) && Name.Contains(TEXT("_fr")))
		{
			Angle = SteerAngle;
			MeshAxis = FVector::UpVector;
		}
		else if (bHideKickstand && Name == TEXT("kickstand"))
		{
			const FCompactPoseBoneIndex StandIndex = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshBoneIndex));
			if (StandIndex.IsValid())
			{
				Output.Pose[StandIndex].SetScale3D(FVector(0.001f));
			}
			continue;
		}
		else
		{
			continue;
		}

		const FCompactPoseBoneIndex CompactIndex = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshBoneIndex));
		if (!CompactIndex.IsValid())
		{
			continue;
		}

		// The axis is given in mesh space: bring it into the space of the bone
		const FVector LocalAxis = MeshSpaceRotation(MeshBoneIndex).UnrotateVector(MeshAxis).GetSafeNormal();
		const FQuat Spin(LocalAxis, FMath::DegreesToRadians(Angle));
		FTransform& Local = Output.Pose[CompactIndex];
		Local.SetRotation((Local.GetRotation() * Spin).GetNormalized());
	}

	return true;
}
