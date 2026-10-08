# img2char · Imagen → personaje 3D por partes

Programa de escritorio que convertirá imágenes de un personaje en un modelo 3D **modular**
(28 piezas que encajan por anillos compartidos, quads, UV, PBR, rig con nombres de UE5) usando
sólo geometría y visión por computadora clásica. El diseño completo está en
[`docs/DISENO.md`](docs/DISENO.md); las secciones (§) de este README se refieren a ese documento.

![Visor de la fase 1: plantilla female_v1 en vista explosionada con Hand_L seleccionada](docs/fase1_visor.png)

## Estado

| Fase (§14) | Estado |
|---|---|
| **1. Plantilla + visor** | ✅ Hecha: plantillas `male_v1`/`female_v1`, 28 piezas, anillos, tapas, landmarks, 10 morphs, esqueleto UE5, UV, export `.glb` + `manifest.json`, app con visor |
| 2. Segmentación + landmarks | Pendiente |
| 3–11 | Pendiente |

Criterios de aceptación de la fase 1, comprobados por `img2char templates check` y los tests:
las 28 piezas cargan en el visor, los anillos coinciden a **0,0 cm** y la malla es **100 % quads**
(además: sin aristas no-manifold, orientación coherente, simetría exacta).

### Plantilla provisional

El documento recomienda partir de la malla base de MakeHuman (CC0). Hasta que ese activo esté
preparado (marcar `part_id`, anillos y landmarks a mano en Blender), las plantillas se generan **por
código** (`core/template/procedural.py`). Es un maniquí de quads modelado con extrusiones (box
modeling), dos niveles de Catmull–Clark y un tercero tras añadir ojos, nariz, boca, orejas y dedos.
Masculino y femenino comparten topología exacta porque todas las selecciones geométricas se calculan
una vez y se reproducen por índice. Los morphs son diferencias finitas del propio generador.

Limitaciones conocidas de esta plantilla: ~20 k vértices (el objetivo para la definitiva es 25–40 k),
rasgos faciales simplificados, pelo como casco y UV por proyección cilíndrica/plana. LSCM llega en
la fase 6. Cuando exista la plantilla MakeHuman bastará con guardarla en el mismo formato.

## Instalación

```bash
cd Tools/img2char
python -m venv .venv && . .venv/bin/activate      # Windows: .venv\Scripts\activate
pip install -e ".[ui,dev]"
```

Requiere Python ≥ 3.10. La UI usa PySide6 ≥ 6.5 (Qt Quick 3D, `MultiEffect`, `DebugSettings`).

## Uso

```bash
img2char templates build                 # genera templates/male_v1 y templates/female_v1 (~5 s)
img2char templates check female_v1       # control de calidad (código de salida 1 si falla)
img2char export female_v1 --out out/ --morph height=-0.5 --morph fat=0.3
img2char ui                              # aplicación de escritorio
pytest                                   # tests
```

`export` escribe `character.glb` (un nodo por pieza, metros, Y arriba, tapas como segunda
primitiva), `manifest.json` (formato §2: partes, anillos, grupos, costuras) y `report.json`.
La UI genera las plantillas automáticamente la primera vez.

### Aplicación

Ventana sin marco con el tema de §12.3: pasos del pipeline a la izquierda (los de fases futuras
aparecen atenuados), visor 3D (orbitar, zoom, clic para seleccionar), modos sólido/alambre, tapas,
vista explosionada, inspector con piezas visibles, detalle de la pieza (hueso, anillos, grupos),
control de calidad, sliders de morphs en vivo y exportación.

## Formato de plantilla (`templates/<nombre>/`)

| Archivo | Contenido |
|---|---|
| `mesh.npz` | `V0` (n,3) cm, `F` (m,4), `part_id` (m,), `uv`, `uv_faces`, `mirror` (n,) |
| `parts.json` | tabla de partes (§2): bloque, hueso, material, grupos, blendshapes |
| `rings.json` | anillo → partes `a`/`b` (`b = null` en agujeros: cuencas de los ojos) y bucle ordenado |
| `landmarks.json` | 40 landmarks como índices de vértice |
| `morphs.json`, `morphs/*.npy` | 10 morphs (K,n,3) con rango y unidad |
| `skeleton.json` | 67 huesos con nombres del maniquí de UE5 y posición = centroide de anillo (§10.2) |
| `groups.json` | caras de `palm/thumb/index/middle/ring/pinky` y `heel/body/toes` |
| `loops.json`, `meta.json` | bucles auxiliares (falanges, ojos, bola del pie) y metadatos |

Ejes: Y arriba, frente +Z, izquierda del personaje +X (sufijo `_L`), simetría respecto a X.

## Estructura

```
img2char/
├─ cli.py, pipeline.py         # CLI y orquestador (Job, pasos y fase de cada uno)
├─ app/                        # PySide6 + QML (main.py, qml/Main.qml)
└─ core/
   ├─ mesh/                    # QuadMesh (extrusión, puentes, rejillas), Catmull–Clark, QA
   ├─ template/                # partes, generador procedural, anillos, UV, esqueleto, formato
   ├─ parts.py                 # división en piezas, tapas, vista explosionada, informe de calidad
   └─ export/                  # glTF (.glb) y manifest.json
tests/                         # malla, plantilla, piezas, export, CLI
```
