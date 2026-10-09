#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;

/**
 * Fortnite skins arrive without a physics asset, so they cannot ragdoll. Eliminated players fall as a ragdoll in Fortnite, and so do ours:
 * every imported body gets a physics asset with a body on every bone (head, hands, tail... whatever the skeleton has).
 */
namespace FPRagdoll
{
	/** Makes and saves the physics asset of a skeletal mesh when it has none. Returns true when one was created */
	bool EnsurePhysicsAsset(USkeletalMesh* Mesh);

	/** Does it for every skeletal mesh under a content folder, for skins imported before this existed */
	int32 EnsureForFolder(const FString& Folder);
}
