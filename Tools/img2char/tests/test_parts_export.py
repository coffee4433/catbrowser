import json

import numpy as np
import pytest
from pygltflib import GLTF2

from img2char.cli import main
from img2char.core.export import build_manifest, export_character
from img2char.core.parts import CAP_INSET_CM, quality_report
from img2char.pipeline import Job, build_character


def test_split_rings_coincide_exactly(female, female_parts):
    assert len(female_parts) == 28
    for name, r in female.rings.items():
        if r["b"] is None:
            continue
        a, b = female_parts[r["a"]], female_parts[r["b"]]
        assert np.array_equal(a.V[a.rings[name]], b.V[b.rings[name]]), name
        assert np.array_equal(a.orig_index[a.rings[name]], b.orig_index[b.rings[name]]), name


def test_caps(female_parts):
    hand = female_parts["Hand_L"]
    assert len(hand.cap_F) == len(hand.rings["wrist_l"])
    ring_c = hand.V[hand.rings["wrist_l"]].mean(0)
    assert np.linalg.norm(hand.cap_V[0] - ring_c) == pytest.approx(CAP_INSET_CM, abs=1e-6)
    # la tapa se hunde hacia la mano, no hacia el antebrazo
    assert np.linalg.norm(hand.cap_V[0] - hand.centroid) < np.linalg.norm(ring_c - hand.centroid)
    assert len(female_parts["Eye_L"].cap_F) == 0


def test_quality_report_phase1_criteria(female, female_parts):
    r = quality_report(female, female_parts)
    assert r["parts"] == 28
    assert r["quad_ratio"] == 1.0
    assert r["ring_max_error_cm"] == 0.0
    assert r["asymmetry_mean_cm"] < 0.2


def test_manifest_format(female, female_parts):
    m = build_manifest(female, female_parts)
    assert (m["template"], m["units"], m["up_axis"], m["mirror_axis"]) == ("female_v1", "cm", "Y", "X")
    face = next(p for p in m["parts"] if p["id"] == "Face")
    assert face["rings"] == {"eye_socket_l": None, "eye_socket_r": None, "face_rim": "Head", "lips_inner": "MouthBag"}
    hand = next(p for p in m["parts"] if p["id"] == "Hand_L")
    assert hand["rings"] == {"wrist_l": "Forearm_L"} and hand["groups"][0] == "palm"
    assert len(m["seams"]) == 22


def test_export_glb(female, tmp_path):
    paths = export_character(female, tmp_path)
    g = GLTF2().load(str(paths["glb"]))
    names = [n.name for n in g.nodes]
    assert len(g.meshes) == 28 and names[-1] == "Character"
    assert names[:-1] == [p["id"] for p in json.loads(paths["manifest"].read_text())["parts"]]
    hand = g.meshes[names.index("Hand_L")]
    assert len(hand.primitives) == 2  # piel + tapa
    pos = g.accessors[hand.primitives[0].attributes.POSITION]
    assert max(pos.max) < 2.0  # metros
    report = json.loads(paths["report"].read_text())
    assert report["ring_max_error_cm"] == 0.0


def test_pipeline_with_morphs_and_cli(saved_dir, tmp_path):
    out = build_character(Job(template="male_v1", morphs={"fat": 1.0}, out_dir=tmp_path / "a", template_dir=saved_dir))
    assert json.loads(out["report"].read_text())["morphs_applied"]["fat"] == 1.0
    with pytest.raises(NotImplementedError):
        build_character(Job(images={"front": "x.png"}, template_dir=saved_dir))
    with pytest.raises(ValueError):
        build_character(Job(morphs={"nope": 1.0}, out_dir=tmp_path / "b", template_dir=saved_dir))
    assert main(["--dir", str(saved_dir), "templates", "check", "female_v1"]) == 0
    assert main(["--dir", str(saved_dir), "export", "female_v1", "--out", str(tmp_path / "c"),
                 "--morph", "height=-1"]) == 0
    assert (tmp_path / "c" / "character.glb").exists()
