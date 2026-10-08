import numpy as np

from img2char.core.mesh import mesh_stats
from img2char.core.template.model import Template
from img2char.core.template.parts_def import PARTS
from img2char.core.template.skeleton import bone_hierarchy

from .conftest import reflect


def test_all_28_parts_present(male):
    counts = np.bincount(male.part_id, minlength=len(PARTS))
    assert len(PARTS) == 28
    assert (counts > 0).all(), [p.id for p, c in zip(PARTS, counts) if c == 0]


def test_mesh_quality(male):
    st = mesh_stats(male.V0, male.F)
    assert male.F.shape[1] == 4
    assert st["non_manifold_edges"] == 0
    assert st["flipped_edges"] == 0
    assert st["degenerate_faces"] == 0
    assert 15_000 < len(male.V0) < 60_000


def test_male_and_female_share_topology(male, female):
    assert (male.F == female.F).all()
    assert (male.part_id == female.part_id).all()
    assert male.landmarks == female.landmarks
    assert (male.mirror == female.mirror).all()
    assert np.abs(male.V0 - female.V0).max() > 1.0
    assert male.V0[:, 1].max() > female.V0[:, 1].max() + 5  # 178 cm frente a 166 cm


def test_exact_symmetry(male):
    m = male.mirror
    assert (m[m] == np.arange(len(m))).all()
    assert np.abs(male.V0[m] - reflect(male.V0)).max() < 1e-9


def test_rings(male):
    shared = {k for k, r in male.rings.items() if r["b"] is not None}
    holes = {k for k, r in male.rings.items() if r["b"] is None}
    assert holes == {"eye_socket_l", "eye_socket_r"}
    assert len(shared) == 22
    for name in ("face_rim", "neck_top", "lips_inner", "wrist_l", "ankle_r", "clavicle_l", "thigh_r"):
        assert name in shared


def test_morphs(male, female):
    assert male.morph_names == female.morph_names
    assert male.morph_T.shape == (10, len(male.V0), 3)
    for name, T in zip(male.morph_names, male.morph_T):
        assert np.abs(T).max() > 0.5, name
        assert np.abs(T[male.mirror] - reflect(T)).max() < 1e-9, name
    crown = male.landmarks["crown"]
    alpha = np.zeros(10)
    alpha[male.morph_names.index("height")] = 1.0
    assert male.deformed(alpha)[crown, 1] - male.V0[crown, 1] > 8.0
    alpha[:] = 0
    alpha[male.morph_names.index("gender")] = 1.0
    V = male.deformed(alpha)

    def hip_to_shoulder(V):
        return V[male.landmarks["trochanter_l"], 0] / V[male.landmarks["acromion_l"], 0]

    assert hip_to_shoulder(V) > hip_to_shoulder(male.V0) + 0.05


def test_skeleton(male):
    names = [b["name"] for b in male.skeleton]
    assert [n for n, _ in bone_hierarchy()] == names
    for core in ("pelvis", "spine_05", "head", "upperarm_l", "hand_r", "index_03_l", "thumb_01_r", "ball_l"):
        assert core in names
    lo, hi = male.V0.min(0) - 1, male.V0.max(0) + 1
    assert male.V0[:, 1].min() == 0.0  # pies en el suelo, root en el origen
    for b in male.skeleton:
        assert b["parent"] is None or b["parent"] in names
        assert ((np.array(b["head"]) >= lo) & (np.array(b["head"]) <= hi)).all(), b["name"]
    pos = {b["name"]: np.array(b["head"]) for b in male.skeleton}
    assert pos["head"][1] > pos["neck_01"][1] > pos["spine_03"][1] > pos["pelvis"][1] > pos["calf_l"][1]
    assert pos["hand_l"][0] > 0 > pos["hand_r"][0]


def test_landmarks_and_groups(male):
    assert len(male.landmarks) >= 40
    lm = {k: male.V0[v] for k, v in male.landmarks.items()}
    assert lm["crown"][1] > lm["nose_tip"][1] > lm["chin"][1] > lm["navel"][1] > lm["knee_l"][1]
    assert lm["eye_outer_l"][0] > lm["eye_inner_l"][0] > 0
    assert male.mirror[male.landmarks["heel_l"]] == male.landmarks["heel_r"]
    assert set(male.groups["Hand_L"]) == {"palm", "thumb", "index", "middle", "ring", "pinky"}
    assert set(male.groups["Foot_R"]) == {"heel", "body", "toes"}
    for part, groups in male.groups.items():
        assert all(len(f) for f in groups.values()), part


def test_uv(male):
    assert male.uv_faces.shape == male.F.shape
    assert male.uv.min() >= 0 and male.uv.max() <= 1


def test_save_load_roundtrip(male, saved_dir):
    t = Template.load(saved_dir / "male_v1")
    assert (t.F == male.F).all() and (t.part_id == male.part_id).all()
    assert np.abs(t.V0 - male.V0).max() < 1e-4
    assert t.morph_names == male.morph_names
    assert t.rings == male.rings and t.skeleton == male.skeleton
