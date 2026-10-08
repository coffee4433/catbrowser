"""Fase 2 sobre fotos sintéticas renderizadas desde la plantilla (README §14: tests sin fotos reales)."""
import json

import cv2
import numpy as np
import pytest

from img2char.cli import main
from img2char.core.analyze import analyze
from img2char.core.landmarks import detect_landmarks
from img2char.core.preprocess import gray_world, load_image
from img2char.core.regions import PART3D_TO_2D, PARTS_2D, kind_base
from img2char.core.segment import iou, paint, silhouette_mask
from img2char.core.synth import KINDS, synth_photo
from img2char.core.template.landmarks2d import LANDMARKS_2D, template_points
from img2char.core.template.parts_def import PARTS


def random_alpha(tpl, rng, amp=0.8):
    return np.array([0.0 if m["name"] == "gender" else rng.uniform(max(m["lo"], -amp), min(m["hi"], amp))
                     for m in tpl.morph_info])


@pytest.fixture(scope="module")
def cases(male, female):
    rng = np.random.default_rng(42)
    out = []
    for tpl in (female, male):
        out.append((tpl, synth_photo(tpl)))
        for _ in range(2):
            out.append((tpl, synth_photo(tpl, random_alpha(tpl, rng))))
    return out


@pytest.fixture(scope="module")
def analyses(cases):
    return [analyze(r["rgb"], r["height_cm"]) for _, r in cases]


def test_mask_iou(cases, analyses):
    for (_, r), a in zip(cases, analyses):
        assert iou(a.seg.mask, r["mask"]) >= 0.98


def test_alpha_png_skips_grabcut(female):
    r = synth_photo(female, with_alpha=True)
    img = load_image(r["image"])
    assert img.alpha is not None
    assert iou(silhouette_mask(img.rgb, img.alpha), r["mask"]) >= 0.99  # alfa antialiasado frente a cobertura


def test_landmarks_within_2_percent_of_height(cases, analyses):
    """Criterio de la fase 2: landmarks a < 2 % de la altura de su posición correcta."""
    worst = {}
    for (tpl, r), a in zip(cases, analyses):
        gt = {k: r["camera"].project(p)[0] for k, p in template_points(tpl, r["V"]).items()}
        for k in LANDMARKS_2D:
            e = np.linalg.norm(np.asarray(a.landmarks.points[k]) - gt[k]) / a.landmarks.height_px
            worst[k] = max(worst.get(k, 0.0), e)
        assert not a.landmarks.warnings
    bad = {k: round(v * 100, 2) for k, v in worst.items() if v >= 0.02}
    assert not bad, bad


def test_landmarks_on_gt_mask_are_the_same_detector(female):
    r = synth_photo(female)
    lm = detect_landmarks(r["mask"], r["rgb"])
    assert 7.0 < lm.heads < 8.2
    assert set(LANDMARKS_2D) <= set(lm.points)
    assert all(c >= 0.5 for c in lm.confidence.values())


def test_parts_2d(cases, analyses):
    lut = np.full(256, -1)
    for i, p in enumerate(PARTS):
        lut[i] = PARTS_2D.index(PART3D_TO_2D[p.id])
    for (_, r), a in zip(cases, analyses):
        gt = lut[r["part_map"]]
        both = (gt >= 0) & (a.seg.parts2d >= 0)
        assert (gt[both] == a.seg.parts2d[both]).mean() >= 0.93
        for k, name in enumerate(PARTS_2D):
            # Manos y pies miden ~5–6 % de la altura: un error de muñeca/tobillo dentro del 2 %
            # permitido mueve hasta un 20 % de sus píxeles a la parte vecina.
            need = 0.70 if name.startswith(("hand", "foot")) else 0.85
            assert (a.seg.parts2d[gt == k] == k).mean() >= need, name
        assert ((a.seg.parts2d >= 0) == (a.seg.mask > 0)).all()


def test_material_zones(cases, analyses):
    for (_, r), a in zip(cases, analyses):
        kinds = np.array([kind_base(k) for k in a.seg.material_kinds] + ["bg"])
        pred = kinds[np.where(a.seg.materials >= 0, a.seg.materials, len(kinds) - 1)]
        for kind, need in (("skin", 0.97), ("hair", 0.95), ("garment", 0.97), ("footwear", 0.85)):
            gt = r["kind_map"] == KINDS.index(kind)
            assert (pred[gt] == kind).mean() >= need, kind


def test_camera_scale(cases, analyses):
    for (_, r), a in zip(cases, analyses):
        assert a.camera.px_per_cm == pytest.approx(r["camera"].px_per_cm, rel=0.01)
        assert a.camera.cx == pytest.approx(r["camera"].cx, abs=3)


def test_manual_edits_are_kept(female):
    r = synth_photo(female)
    a = analyze(r["rgb"], r["height_cm"])
    a.landmarks.move("knee_l", 10, 20)
    a.seg.mask[:5, :5] = 0
    paint(a.seg.mask, (5, 5), (5, 5), 3, add=False)
    a.redo_landmarks()
    assert a.landmarks.points["knee_l"] == (10.0, 20.0)
    assert "knee_l" in a.landmarks.manual
    a.landmarks.reset("knee_l")
    assert a.landmarks.points["knee_l"] != (10.0, 20.0)
    a.set_material_kind(0, "accessory")
    assert a.materials_summary()[0]["kind"] == "accessory"
    with pytest.raises(ValueError):
        a.set_material_kind(0, "plasma")


def test_brush():
    m = np.zeros((50, 50), np.uint8)
    paint(m, (10, 10), (40, 10), 3, add=True)
    assert m[10, 10:41].all() and not m[30, 30]
    paint(m, (25, 10), (25, 10), 4, add=False)
    assert not m[10, 25]


def test_preprocess(tmp_path):
    img = np.zeros((3000, 1000, 3), np.uint8)
    img[:] = (200, 100, 50)
    p = tmp_path / "big.png"
    cv2.imwrite(str(p), img)
    out = load_image(p)
    assert out.size == (683, 2048) and out.scale == pytest.approx(2048 / 3000)
    assert out.rgb[0, 0].tolist() == [50, 100, 200]  # BGR en disco -> RGB
    wb = gray_world(out.rgb)
    assert np.ptp(wb.reshape(-1, 3).mean(0)) < 2


def test_robust_to_bad_input():
    with pytest.raises(ValueError):
        detect_landmarks(np.zeros((100, 100), np.uint8))
    blob = np.zeros((400, 200), np.uint8)
    cv2.ellipse(blob, (100, 200), (40, 180), 0, 0, 360, 255, -1)
    lm = detect_landmarks(blob)
    assert set(LANDMARKS_2D) <= set(lm.points) and lm.warnings


def test_cli_synth_and_analyze(saved_dir, tmp_path):
    img = tmp_path / "f.png"
    assert main(["--dir", str(saved_dir), "synth", "female_v1", "--out", str(img), "--morph", "fat=0.5"]) == 0
    truth = json.loads(img.with_suffix(".json").read_text())
    assert main(["analyze", str(img), "--height", str(truth["height_cm"]), "--out", str(tmp_path / "an")]) == 0
    res = json.loads((tmp_path / "an" / "analysis.json").read_text())
    H = res["landmarks"]["bottom"] - res["landmarks"]["top"]
    for k, p in truth["landmarks"].items():
        assert np.linalg.norm(np.subtract(res["landmarks"]["points"][k], p)) / H < 0.02, k
    for f in ("mask.png", "materials.png", "parts2d.png", "overlay.png"):
        assert (tmp_path / "an" / f).stat().st_size > 1000
