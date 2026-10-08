"""Tabla de partes del personaje (README §2) y nombres de anillos de unión."""
from __future__ import annotations

from dataclasses import dataclass, field


@dataclass(frozen=True)
class PartDef:
    id: str
    block: str        # head | trunk | arms | legs
    bone: str
    material: str
    uv: str           # método de proyección provisional (§7): cyl_y, cyl_pca, planar_z, planar_x
    groups: tuple = field(default_factory=tuple)
    blendshapes: tuple = field(default_factory=tuple)


def _lr(base: str, block: str, bone: str, material: str, uv: str, groups=()) -> list[PartDef]:
    return [PartDef(f"{base}_{s.upper()}", block, bone.format(s=s), material, uv, groups) for s in ("l", "r")]


PARTS: list[PartDef] = [
    PartDef("Head", "head", "head", "skin", "cyl_y"),
    PartDef("Hair", "head", "head", "hair", "cyl_y"),
    PartDef("Face", "head", "head", "skin", "planar_z",
            blendshapes=("jawOpen", "smile", "browUp", "blinkL", "blinkR")),
    *_lr("Eye", "head", "eye_{s}", "eye", "planar_z"),
    PartDef("MouthBag", "head", "jaw", "mouth", "cyl_z"),
    PartDef("Teeth_Upper", "head", "head", "teeth", "planar_z"),
    PartDef("Teeth_Lower", "head", "jaw", "teeth", "planar_z"),
    *_lr("Ear", "head", "head", "skin", "planar_x"),
    PartDef("Neck", "trunk", "neck_01", "skin", "cyl_y"),
    PartDef("Chest", "trunk", "spine_04", "skin", "cyl_y"),
    PartDef("Abdomen", "trunk", "spine_02", "skin", "cyl_y"),
    PartDef("Hips", "trunk", "pelvis", "skin", "cyl_y"),
    *_lr("Shoulder", "arms", "clavicle_{s}", "skin", "cyl_pca"),
    *_lr("UpperArm", "arms", "upperarm_{s}", "skin", "cyl_pca"),
    *_lr("Forearm", "arms", "lowerarm_{s}", "skin", "cyl_pca"),
    *_lr("Hand", "arms", "hand_{s}", "skin", "cyl_pca",
         groups=("palm", "thumb", "index", "middle", "ring", "pinky")),
    *_lr("Thigh", "legs", "thigh_{s}", "skin", "cyl_y"),
    *_lr("Shin", "legs", "calf_{s}", "skin", "cyl_y"),
    *_lr("Foot", "legs", "foot_{s}", "skin", "cyl_pca", groups=("heel", "body", "toes")),
]

PART_INDEX = {p.id: i for i, p in enumerate(PARTS)}
PART_BY_ID = {p.id: p for p in PARTS}

# Anillos entre dos partes (orden alfabético del par) -> nombre del anillo.
RING_NAMES: dict[frozenset, str] = {
    frozenset(("Head", "Face")): "face_rim",
    frozenset(("Head", "Neck")): "neck_top",
    frozenset(("Head", "Ear_L")): "ear_l",
    frozenset(("Head", "Ear_R")): "ear_r",
    frozenset(("Face", "MouthBag")): "lips_inner",
    frozenset(("Neck", "Chest")): "neck_base",
    frozenset(("Chest", "Abdomen")): "waist",
    frozenset(("Abdomen", "Hips")): "pelvis_top",
}
for _s in ("l", "r"):
    S = _s.upper()
    RING_NAMES.update({
        frozenset(("Chest", f"Shoulder_{S}")): f"clavicle_{_s}",
        frozenset((f"Shoulder_{S}", f"UpperArm_{S}")): f"upperarm_{_s}",
        frozenset((f"UpperArm_{S}", f"Forearm_{S}")): f"elbow_{_s}",
        frozenset((f"Forearm_{S}", f"Hand_{S}")): f"wrist_{_s}",
        frozenset(("Hips", f"Thigh_{S}")): f"thigh_{_s}",
        frozenset((f"Thigh_{S}", f"Shin_{S}")): f"knee_{_s}",
        frozenset((f"Shin_{S}", f"Foot_{S}")): f"ankle_{_s}",
    })

# Anillos sin pareja (agujeros): parte -> prefijo; el sufijo _l/_r sale del lado (x > 0 = izquierda).
HOLE_RINGS = {"Face": "eye_socket"}

MATERIAL_COLORS = {
    "skin": (0.80, 0.62, 0.52),
    "hair": (0.16, 0.11, 0.08),
    "eye": (0.93, 0.93, 0.92),
    "mouth": (0.55, 0.20, 0.22),
    "teeth": (0.95, 0.93, 0.86),
    "cap": (0.05, 0.05, 0.06),
}
