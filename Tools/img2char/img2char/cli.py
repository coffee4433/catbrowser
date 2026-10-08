"""Línea de comandos: `img2char templates build|check`, `export`, `analyze`, `synth`, `ui`."""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from .core.template.model import DEFAULT_TEMPLATE_DIR, TEMPLATE_NAMES, Template


def _phase1_failures(report: dict) -> list[str]:
    checks = {
        "28 piezas": report["parts"] == 28,
        "100 % quads": report["quad_ratio"] == 1.0,
        "anillos a 0,0 cm": report["ring_max_error_cm"] == 0.0,
        "sin aristas no-manifold": report["non_manifold_edges"] == 0,
        "orientación coherente": report["flipped_edges"] == 0,
        "sin caras degeneradas": report["degenerate_faces"] == 0,
        "simetría < 0,2 cm": report["asymmetry_mean_cm"] < 0.2,
    }
    return [k for k, ok in checks.items() if not ok]


def cmd_build(args) -> int:
    from .core.parts import quality_report, split_parts
    from .core.template.build import build_templates

    for t in build_templates().values():
        d = t.save(args.dir)
        r = quality_report(t, split_parts(t))
        print(f"{t.name}: {r['vertices']} vértices, {r['faces']} quads, {r['parts']} piezas, "
              f"{len(r['rings'])} anillos, {r['landmarks']} landmarks, {r['morphs']} morphs -> {d}")
    return 0


def cmd_check(args) -> int:
    from .core.parts import quality_report, split_parts

    t = Template.load(Path(args.dir) / args.name)
    r = quality_report(t, split_parts(t))
    if not args.verbose:
        r.pop("per_part")
    print(json.dumps(r, indent=1, ensure_ascii=False))
    failed = _phase1_failures(r)
    if failed:
        print("FALLA: " + ", ".join(failed), file=sys.stderr)
    return 1 if failed else 0


def cmd_export(args) -> int:
    from .pipeline import Job, build_character

    morphs = {}
    for kv in args.morph or []:
        k, _, v = kv.partition("=")
        morphs[k] = float(v)
    paths = build_character(Job(template=args.name, morphs=morphs, caps=not args.no_caps, out_dir=Path(args.out),
                                template_dir=Path(args.dir)))
    for k, p in paths.items():
        print(f"{k}: {p}")
    return 0


def cmd_analyze(args) -> int:
    from .pipeline import Job, analyze_job

    a = analyze_job(Job(images={"front": args.image}, height_cm=args.height))
    files = a.save(args.out)
    lm = a.landmarks
    print(f"silueta: {int((a.seg.mask > 0).sum())} px · altura {lm.height_px:.0f} px · {lm.heads:.2f} cabezas · "
          f"{a.camera.px_per_cm:.3f} px/cm")
    shares: dict = {}
    for m in a.materials_summary():
        shares[m["label"]] = shares.get(m["label"], 0.0) + m["share"]
    print("materiales: " + ", ".join(f"{k} {v * 100:.0f} %" for k, v in sorted(shares.items(), key=lambda kv: -kv[1])))
    for w in a.warnings:
        print(f"aviso: {w}")
    for k, p in files.items():
        print(f"{k}: {p}")
    return 0


def cmd_synth(args) -> int:
    """Foto sintética de prueba renderizada desde la plantilla (con su verdad terreno)."""
    import cv2
    import numpy as np

    from .core.synth import synth_photo
    from .core.template.landmarks2d import template_points
    from .pipeline import load_template

    t = load_template(args.name, args.dir)
    alpha = np.zeros(len(t.morph_names))
    for kv in args.morph or []:
        k, _, v = kv.partition("=")
        alpha[t.morph_names.index(k)] = float(v)
    r = synth_photo(t, alpha, height_px=args.height_px, outfit=not args.no_outfit)
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    cv2.imwrite(str(out), cv2.cvtColor(r["rgb"], cv2.COLOR_RGB2BGR))
    truth = {"height_cm": round(r["height_cm"], 3), "morphs": dict(zip(t.morph_names, alpha.tolist())),
             "landmarks": {k: [round(float(x), 2) for x in r["camera"].project(p)[0]]
                           for k, p in template_points(t, r["V"]).items()}}
    out.with_suffix(".json").write_text(json.dumps(truth, indent=1), encoding="utf-8")
    print(f"{out} ({r['rgb'].shape[1]}×{r['rgb'].shape[0]}) · altura {r['height_cm']:.1f} cm · "
          f"verdad terreno en {out.with_suffix('.json')}")
    return 0


def cmd_ui(args) -> int:
    from .app.main import run

    return run(Path(args.dir))


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="img2char", description="Imagen → personaje 3D por partes")
    ap.add_argument("--dir", default=str(DEFAULT_TEMPLATE_DIR), help="carpeta de plantillas")
    sub = ap.add_subparsers(dest="cmd", required=True)
    t = sub.add_parser("templates", help="plantillas").add_subparsers(dest="tcmd", required=True)
    t.add_parser("build", help="genera male_v1 y female_v1").set_defaults(fn=cmd_build)
    c = t.add_parser("check", help="control de calidad de la fase 1")
    c.add_argument("name", choices=TEMPLATE_NAMES)
    c.add_argument("-v", "--verbose", action="store_true")
    c.set_defaults(fn=cmd_check)
    e = sub.add_parser("export", help="exporta character.glb + manifest.json + report.json")
    e.add_argument("name", choices=TEMPLATE_NAMES)
    e.add_argument("--out", default="out")
    e.add_argument("--morph", action="append", metavar="NOMBRE=VALOR")
    e.add_argument("--no-caps", action="store_true")
    e.set_defaults(fn=cmd_export)
    an = sub.add_parser("analyze", help="fase 2: máscara, puntos, partes 2D y materiales de una foto frontal")
    an.add_argument("image")
    an.add_argument("--height", type=float, required=True, help="altura del personaje en cm")
    an.add_argument("--out", default="out/analysis")
    an.set_defaults(fn=cmd_analyze)
    sy = sub.add_parser("synth", help="renderiza una foto de prueba desde la plantilla")
    sy.add_argument("name", choices=TEMPLATE_NAMES)
    sy.add_argument("--out", default="out/synth.png")
    sy.add_argument("--morph", action="append", metavar="NOMBRE=VALOR")
    sy.add_argument("--height-px", type=int, default=1600)
    sy.add_argument("--no-outfit", action="store_true")
    sy.set_defaults(fn=cmd_synth)
    sub.add_parser("ui", help="abre la aplicación").set_defaults(fn=cmd_ui)
    args = ap.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
