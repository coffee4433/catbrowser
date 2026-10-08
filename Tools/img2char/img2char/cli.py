"""Línea de comandos: `img2char templates build|check`, `img2char export`, `img2char ui`."""
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
    sub.add_parser("ui", help="abre la aplicación").set_defaults(fn=cmd_ui)
    args = ap.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
