# Imagen → Personaje 3D por partes (sin IA generativa) · Guía de construcción

Diseño de un programa de escritorio que convierte una o varias imágenes de un personaje (masculino o femenino) en un modelo 3D **modular**: cabeza, cara, pelo, ojos, boca, orejas, cuello, pecho, abdomen, cadera, hombros, brazos, antebrazos, manos, muslos, espinillas y pies son **mallas independientes que encajan entre sí**, con topología limpia en quads, UV propias, texturas y materiales PBR extraídos de la imagen, esqueleto y pesos automáticos, y una UI moderna con visor 3D.

Toda la reconstrucción es **geometría y visión por computadora clásica** (OpenCV, mínimos cuadrados, Laplacianos, fotogrametría). No hay ninguna red neuronal que "imagine" el volumen: el volumen lo aporta un **maniquí 3D (plantilla)** con topología perfecta que el programa deforma hasta coincidir con la imagen.

---

## 0. Qué se obtiene y qué límites tiene

| Entrada | Salida |
|---|---|
| 1 foto/render frontal (obligatoria). Opcionales: lateral, trasera, primer plano de cara frontal y lateral, foto de manos. Altura del personaje (cm). Género/plantilla. | 28 mallas (42 si separas dedos y zonas del pie) con topología en quads, anillos de unión compartidos y tapas internas; UV por parte; `BaseColor`, `Normal`, `Roughness`, `Metallic`, `AO` por material; esqueleto con nombres compatibles con el maniquí de UE5; pesos de skinning; `character.glb` (+ FBX opcional) y `manifest.json`. |
| 20–80 fotos alrededor del personaje (modo fotogrametría). | Lo mismo, pero con la profundidad medida en vez de heredada de la plantilla. |

Lo que hay que tener claro antes de programar nada:

- **Una foto no contiene profundidad.** Con una vista frontal el programa reproduce exactamente la **silueta, las proporciones, la cara y las texturas**; el grosor del cuerpo (perfil de la nariz, pecho, glúteos, pantorrillas) sale de la plantilla escalada. La vista lateral corrige el perfil. Con muchas fotos (modo B) el volumen se mide con SfM/MVS.
- **La plantilla es el 50 % del resultado.** Un maniquí con buenos loops en cara, codos, rodillas y falanges es lo que hace que el modelo final sea animable. El ajuste nunca cambia la topología: sólo mueve vértices. Así las UV, los anillos de unión entre partes y el rig se conservan siempre.
- **La malla fotogramétrica nunca es la salida.** Una nube de puntos densa o un Poisson es una pieza única con topología sucia (justo lo que no queremos). Se usa sólo como objetivo al que se ajusta la plantilla.
- **La fidelidad de textura depende de la foto**: luz uniforme, sin flash, resolución ≥ 2000 px de alto, pose A o T, brazos despegados del cuerpo, fondo liso. Los renders de skins (PNG con alfa) son la entrada ideal.
- Zonas que la cámara no ve (axilas, entrepierna, interior de la boca) se rellenan por **simetría, inpainting clásico y presets**; no se inventan detalles.

---

## 1. Pipeline

```
 Imágenes + altura + plantilla
        │
        ▼
 [4] Preproceso ──► [4] Segmentación (máscara, zonas de material, partes 2D)
        │
        ▼
 [5] Landmarks + esqueleto 2D  (automático + corrección manual en la UI)
        │
        ├──────────────── Modo B (20+ fotos): [6.2] SfM/MVS → nube densa
        ▼                                                     │
 [6.1] Plantilla: posado → morphs (mín. cuadrados) → ajuste de silueta (Laplaciano/ARAP)
        │◄─────────────── NR‑ICP contra la nube ───────────────┘
        ▼
 [6.3] Pelo en bloques · ojos · boca/dientes · orejas
        ▼
 [6.4] División en partes, anillos compartidos, tapas internas, sockets
        ▼
 [7] UV (heredadas de la plantilla; LSCM/ABF++ para partes regeneradas) → empaquetado
        ▼
 [8] Proyección de texturas (multivista, visibilidad, mezcla, simetría, inpainting, de‑lighting)
        ▼
 [9] Materiales PBR (clasificación de materiales, Normal, Roughness, Metallic, AO horneada)
        ▼
 [10] Rig: articulaciones (anillos + eje medio) → huesos → pesos (Geodesic Voxel Binding)
        ▼
 [11] Export glTF/FBX + manifest  ──►  [12] Visor 3D de la UI / importación en Unreal
```

Orquestador (cada etapa es una función pura que recibe y devuelve datos serializables, así la UI puede reanudar desde cualquier paso y cachear resultados):

```python
def build_character(job: Job) -> CharacterAsset:
    imgs   = preprocess(job.images)                            # §4
    seg    = segment(imgs)                                     # §4  máscaras, etiquetas, partes 2D
    lm     = landmarks(imgs, seg, job.manual_landmarks)        # §5
    tpl    = Template.load(job.template)                       # §3  male_v1 / female_v1
    cams   = cameras_from_views(imgs, lm, job.height_cm)       # ortográficas (modo A) o SfM (modo B)
    tpl    = pose_template(tpl, lm.skeleton2d)                 # §6.1
    tpl    = fit_morphs(tpl, lm, seg, cams)                    # §6.1
    tpl    = fit_silhouette(tpl, seg, cams)                    # §6.1
    if job.photogrammetry: tpl = fit_to_cloud(tpl, sfm(job.images))   # §6.2
    tpl    = build_hair_eyes_mouth_ears(tpl, seg, lm, cams)    # §6.3
    parts  = split_parts(tpl)                                  # §6.4
    uvs    = inherit_or_unwrap(parts)                          # §7
    tex    = project_textures(parts, uvs, imgs, seg, cams)     # §8
    mats   = build_materials(tex, seg.material_labels, parts)  # §9
    rig    = build_rig(parts)                                  # §10
    return export(parts, uvs, mats, rig, job.out_dir)          # §11
```

---

## 2. Las partes del personaje

Los cuatro bloques del brief se concretan en **15 partes lógicas** y **28 objetos de malla** (los pares izquierda/derecha son objetos distintos). Manos y pies se exportan por defecto como un objeto con *grupos de vértices* (palma, 5 dedos; talón, cuerpo, dedos); con `split_fingers=true` / `split_toes=true` se exportan como objetos separados (42 en total).

| Bloque | Parte | Objetos | Loops mínimos / requisitos | Hueso(s) |
|---|---|---|---|---|
| Cabeza | Cráneo / cabeza base | `Head` | Esfera deformada; anillo `face_rim` compartido con `Face`, `ear_l/r` con las orejas, `neck_top` con el cuello | `head` |
| Cabeza | Pelo | `Hair` | Bloque sólido ("casco") + mechones; se genera en §6.3 | `head` |
| Cabeza | Rostro | `Face` | Loops concéntricos en ojos y boca, loop nasolabial, loop de mandíbula; anillos `eye_socket_l/r`, `lips_inner` | `head` (+ blendshapes) |
| Cabeza | Ojos | `Eye_L`, `Eye_R` | Esferas independientes (esclera + iris + pupila en UV); no comparten vértices con nada | `eye_l`, `eye_r` (opcionales) |
| Cabeza | Boca interna y dientes | `MouthBag`, `Teeth_Upper`, `Teeth_Lower` | `MouthBag` comparte `lips_inner` con `Face`; dientes separados para la mandíbula | `head`, `jaw` (opcional) |
| Cabeza | Orejas | `Ear_L`, `Ear_R` | Anillo `ear_l/r` compartido con `Head` | `head` |
| Tronco | Cuello | `Neck` | Cilindro; anillos `neck_top` (cabeza) y `neck_base` (pecho) | `neck_01`, `neck_02` |
| Tronco | Pecho / tórax | `Chest` | Anillos `neck_base`, `clavicle_l/r`, `waist` | `spine_03`…`spine_05` |
| Tronco | Abdomen / cintura | `Abdomen` | 3–4 loops horizontales para torsión; anillos `waist` y `pelvis_top` | `spine_01`, `spine_02` |
| Tronco | Cadera / pelvis | `Hips` | Anillos `pelvis_top`, `thigh_l/r`; centro de gravedad del rig | `pelvis` |
| Brazos | Hombros / clavícula | `Shoulder_L`, `Shoulder_R` | Anillos `clavicle_*` y `upperarm_*` | `clavicle_l/r` |
| Brazos | Brazo y antebrazo | `UpperArm_*`, `Forearm_*` | Corte justo en el codo (anillo `elbow_*`); 3 loops a cada lado del codo | `upperarm_*`, `lowerarm_*` (+ twist) |
| Brazos | Manos | `Hand_*` (grupos: `palm`, `thumb`, `index`, `middle`, `ring`, `pinky`) | 3 loops por falange; anillo `wrist_*` | `hand_*`, 3 huesos por dedo + metacarpos |
| Piernas | Muslo y espinilla | `Thigh_*`, `Shin_*` | Corte en la rodilla (anillo `knee_*`); 3 loops a cada lado | `thigh_*`, `calf_*` (+ twist) |
| Piernas | Pies | `Foot_*` (grupos: `heel`, `body`, `toes`) | Anillo `ankle_*`; loop en la articulación de los dedos | `foot_*`, `ball_*` |

Reglas que hacen que las piezas "encajen":

1. **La plantilla se modela como una sola malla conectada** con un atributo entero `part_id` por cara. Cada frontera entre dos `part_id` es un anillo de unión con nombre. Toda la deformación (§6) se aplica a esa única malla; la división en objetos (§6.4) se hace al final. Por construcción, los vértices de un anillo quedan en la misma posición en las dos piezas.
2. Los vértices de un anillo reciben **exactamente los mismos pesos de skinning** en las dos piezas (§10.5), así siguen coincidiendo al animar.
3. Cada pieza lleva una **tapa interna** (cap) ligeramente hundida en cada anillo, con un material plano oscuro, para que sea sólida en vista explosionada o cuando una articulación se dobla mucho.
4. Opcional: **sockets**, una extensión de 1–2 cm de la pieza proximal dentro de la distal (p. ej. el brazo dentro del antebrazo) para ocultar huecos al flexionar, igual que en los personajes modulares de videojuegos.
5. Si se prefiere la cabeza en una pieza, `manifest.merge = [["Head","Face"]]` las fusiona al exportar sin tocar nada más.

`manifest.json` (lo consumen la UI, el exportador y el importador de Unreal):

```json
{
  "template": "female_v1", "units": "cm", "up_axis": "Y", "mirror_axis": "X",
  "parts": [
    {"id": "Face", "block": "head", "bone": "head", "material": "skin",
     "rings": {"face_rim": "Head", "eye_socket_l": null, "eye_socket_r": null, "lips_inner": "MouthBag"},
     "groups": [], "blendshapes": ["jawOpen", "smile", "browUp", "blinkL", "blinkR"]},
    {"id": "Hand_L", "block": "arms", "bone": "hand_l", "material": "skin",
     "rings": {"wrist_l": "Forearm_L"},
     "groups": ["palm", "thumb", "index", "middle", "ring", "pinky"]}
  ],
  "seams": [{"ring": "wrist_l", "a": "Forearm_L", "b": "Hand_L", "vertices_a": [], "vertices_b": []}]
}
```

---

## 3. La plantilla (maniquí 3D)

Es el activo más importante. Requisitos:

- **Dos plantillas con la misma topología** (`male_v1`, `female_v1`): mismo número de vértices, mismas caras, mismos `part_id`, mismos índices de landmarks. Así todo el código es independiente del género; sólo cambia la posición base de los vértices y los rangos de los morphs. También puedes tener `stylized_v1` (cabeza grande, manos grandes) para personajes tipo Fortnite.
- 100 % quads, sin n‑gonos, sin polos de más de 5 aristas en zonas de deformación. 25–40 k vértices para el cuerpo entero (la cara concentra 8–12 k).
- Pose **A** (brazos a 45°), pies paralelos, boca cerrada, ojos abiertos; simetría exacta respecto a X (se guarda el mapa `mirror[v]`).
- Anillos de unión en cada articulación (tabla §2), 3 loops a cada lado de codos/rodillas, 3 loops por falange, loops faciales para expresiones, bolsa bucal, dientes, ojos esféricos y orejas como islas con anillo compartido.
- **Landmarks** (índices de vértice fijos): coronilla, mentón, glabela, punta de la nariz, comisuras de ojos y boca, pómulos, lóbulos, acromion L/R, codo, muñeca, nudillos, cresta ilíaca, trocánter, rodilla, tobillo, talón, punta del pie. Unos 60 en total.
- **Morph targets** (desplazamientos por vértice `T_k`, `K ≈ 30`): género, altura, anchura de hombros, anchura de cadera, longitud de piernas/brazos/torso/cuello, tamaño de cabeza, grasa, músculo, pecho, glúteos, distancia interocular, anchura de nariz/boca/mandíbula, tamaño de orejas y manos. Es el nivel "grueso" del ajuste: 30 números bien condicionados valen más que 30 000 vértices libres.
- **Esqueleto** en reposo con nombres de UE5 (§10.1) y **UV ya desplegadas por parte** (§7).

De dónde sacarla:

- **MakeHuman / MPFB**: la malla base, los *targets* y las skins son CC0 (base mesh y assets, confirmado en su licencia; el programa en sí es AGPL, pero no lo redistribuyes). Trae género, edad, proporciones y decenas de targets ya hechos, esqueleto y UV. Hay que recortar las partes, marcar `part_id`, añadir anillos de tapa y renombrar huesos.
- Modelar la propia en Blender (más trabajo, control total). Útil para la variante estilizada.
- Evitar modelos estadísticos con licencia no comercial (SMPL y derivados) o basemeshes de tiendas sin licencia de redistribución.

Formato de almacenamiento (`templates/female_v1/`): `mesh.npz` (`V0`, `F`, `part_id`, `uv`, `uv_faces`, `mirror`), `landmarks.json`, `morphs/*.npy` (`(n,3)` cada uno) con `morphs.json` (rangos), `skeleton.json` (jerarquía, posiciones de reposo, anillo asociado a cada articulación), `parts.json` (tabla §2), `presets/` (texturas de boca, dientes, iris, uñas).

---

## 4. Entrada, preproceso y segmentación (OpenCV)

**Preproceso**: orientación EXIF, reescalado a 2048 px de alto como máximo (se conserva la original para texturas), balance de blancos por *gray‑world* (opcional, con interruptor en la UI), ligera `cv2.bilateralFilter` sólo para la segmentación (nunca para la textura).

**Máscara de silueta**:

```python
import cv2, numpy as np

def silhouette_mask(img_bgr, rect=None, alpha=None):
    if alpha is not None:                       # PNG con alfa (render de skin): la máscara ya existe
        return (alpha > 127).astype(np.uint8) * 255
    h, w = img_bgr.shape[:2]
    rect = rect or (int(w*.05), int(h*.02), int(w*.90), int(h*.96))   # el usuario puede dibujarlo
    mask = np.zeros((h, w), np.uint8)
    bgd, fgd = np.zeros((1, 65), np.float64), np.zeros((1, 65), np.float64)
    cv2.grabCut(img_bgr, mask, rect, bgd, fgd, 5, cv2.GC_INIT_WITH_RECT)
    fg = np.where((mask == cv2.GC_FGD) | (mask == cv2.GC_PR_FGD), 255, 0).astype(np.uint8)
    fg = cv2.morphologyEx(fg, cv2.MORPH_CLOSE, np.ones((5, 5), np.uint8))
    fg = cv2.morphologyEx(fg, cv2.MORPH_OPEN,  np.ones((3, 3), np.uint8))
    contours, _ = cv2.findContours(fg, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_NONE)
    clean = np.zeros_like(fg)
    cv2.drawContours(clean, [max(contours, key=cv2.contourArea)], -1, 255, cv2.FILLED)
    return clean
```

GrabCut (Rother 2004) es un corte de grafos con mezclas de gaussianas: iterativo y clásico, sin redes. Dónde entran **Canny y Sobel**:

- *Refinado del borde*: para cada punto del contorno se busca, a ±4 px a lo largo de su normal 2D, el máximo de magnitud de Sobel (`cv2.Sobel` en x e y, `cv2.magnitude`) y se desplaza el punto allí. Elimina el "halo" de GrabCut.
- *Bordes internos*: `cv2.Canny(blur, 50, 150)` detecta las fronteras brazo/torso y pelo/piel aunque los colores sean parecidos; alimenta la separación de partes 2D.
- *Cara*: bordes de párpados, labios y aletas de la nariz para refinar landmarks (§5).

**Partes 2D** (cabeza, torso, brazo L/R, pierna L/R, mano L/R, pie L/R): marcadores a lo largo de cada rama del esqueleto 2D (§5) + `cv2.watershed(img, markers)` limitado por la máscara y por los bordes de Canny. Cada parte 3D atraerá su contorno sólo hacia su parte 2D: es lo que hace robusto el ajuste cuando un brazo roza el torso.

**Zonas de material** (piel, pelo, prendas, calzado, accesorios):

```python
def material_labels(img_bgr, mask, k=6):
    lab = cv2.cvtColor(img_bgr, cv2.COLOR_BGR2LAB).reshape(-1, 3).astype(np.float32)
    idx = np.flatnonzero(mask.ravel())
    crit = (cv2.TERM_CRITERIA_EPS + cv2.TERM_CRITERIA_MAX_ITER, 20, 1.0)
    _, labels, centers = cv2.kmeans(lab[idx], k, None, crit, 5, cv2.KMEANS_PP_CENTERS)
    out = np.full(mask.size, -1, np.int32); out[idx] = labels.ravel()
    return out.reshape(mask.shape), centers

ycrcb = cv2.cvtColor(img_bgr, cv2.COLOR_BGR2YCrCb)
skin  = cv2.inRange(ycrcb, (0, 133, 77), (255, 173, 127))     # rango clásico de piel en CrCb
```

Reglas encima de los clusters: cluster ∩ `skin` ∩ (cara, cuello, manos) → **piel**; cluster dominante sobre el cráneo y fuera del óvalo facial → **pelo**; clusters dentro de la parte 2D "pie" → **calzado**; el resto → **prenda_1..n**. El usuario puede reetiquetar con un clic (§12). Cada etiqueta se convierte en un material PBR en §9.

---

## 5. Landmarks y esqueleto 2D (sin IA)

1. **Esqueleto de la silueta**: adelgazamiento Zhang‑Suen (`cv2.ximgproc.thinning(mask, thinningType=cv2.ximgproc.THINNING_ZHANGSUEN)` o `skimage.morphology.skeletonize`). Se convierte en grafo: píxeles con 1 vecino = extremos (cabeza, manos, pies), con ≥ 3 vecinos = bifurcaciones (cuello/hombros, pelvis).
2. **Perfil de anchura**: `width[y] = nº de píxeles de máscara en la fila y`. El primer mínimo local bajo la coronilla es el **cuello**; el máximo de la derivada justo debajo, los **hombros**; la **entrepierna** es la fila más baja en la que la columna central sigue siendo primer plano; las **axilas**, la primera fila (bajando desde los hombros) con tres segmentos (brazo, torso, brazo).
3. **Articulaciones de extremidades**: a lo largo de cada rama (hombro→mano, cadera→pie) se mide la anchura local con la transformada de distancia (`cv2.distanceTransform(mask, cv2.DIST_L2, 5)`, el doble del valor sobre el esqueleto es el grosor). Codos, muñecas, rodillas y tobillos son **mínimos locales de grosor** cerca de la proporción esperada (codo ≈ 45 % de hombro→muñeca, rodilla ≈ 48 % de cadera→tobillo). La muñeca es, además, donde el grosor vuelve a crecer (la mano).
4. **Cara** (sobre la imagen de cuerpo o, mejor, sobre la ranura "rostro frontal"): óvalo facial = componente de piel conectada a la cabeza; **ojos** = los dos blobs oscuros más simétricos en el 40 % superior del óvalo (umbral de luminancia + `cv2.connectedComponentsWithStats`); **boca** = máximo de `R − G` en el tercio inferior; **nariz** = en el eje de simetría entre ambos, refinada con bordes de Canny; **mandíbula** = contorno inferior del óvalo. Opcional: cascadas Haar de OpenCV (`haarcascade_frontalface_default.xml`, `haarcascade_eye.xml`), boosting clásico de 2001, para inicializar.
5. **Proporciones en cabezas**: altura total / altura de cabeza (7,5 realista, 5–6 estilizado), hombros/cadera, longitud de piernas. Se convierten en valores iniciales de los morphs (§6.1).
6. **Corrección manual**: todos los puntos se dibujan sobre la imagen en la UI y se pueden arrastrar. El ajuste automático no tiene que ser perfecto; tiene que estar cerca y ser corregible en segundos.

Con altura conocida (`job.height_cm`) la escala es `px_por_cm = altura_px / altura_cm`; de ahí sale una **cámara ortográfica** por vista (modo A). Si la foto se hizo a menos de ~3 m hay distorsión de perspectiva (cabeza grande): se avisa en la UI y, si hay EXIF de focal, se estima una cámara en perspectiva aproximada.

---

## 6. Reconstrucción geométrica

### 6.1 Modo A: 1–3 vistas → ajuste de plantilla (Template Matching / Basemesh Deformer)

Es el camino por defecto y el más eficiente. Cuatro niveles, de grueso a fino; cada uno parte del anterior:

**a) Alineación global (Procrustes con escala)** entre los landmarks 3D de la plantilla proyectados y los landmarks 2D de la vista frontal:

```python
def similarity_fit(src, dst):                # src (n,3) plantilla, dst (n,3) objetivo (z=0 en vista frontal)
    mu_s, mu_d = src.mean(0), dst.mean(0)
    S, D = src - mu_s, dst - mu_d
    U, sig, Vt = np.linalg.svd(S.T @ D)
    if np.linalg.det(U @ Vt) < 0: Vt[-1] *= -1; sig[-1] *= -1
    R = (U @ Vt).T
    s = sig.sum() / (S ** 2).sum()
    return s, R, mu_d - s * R @ mu_s            # Kabsch/Umeyama
```

**b) Posado**: ángulos 2D de cada hueso a partir de las ramas del esqueleto 2D (rotación en el plano de la imagen) aplicados al esqueleto de la plantilla con LBS. Basta para pose A/T. Las rotaciones fuera del plano no se pueden deducir de una vista (ambigüedad de escorzo): la UI pide pose A y, si hay vista lateral, se usa para los brazos.

**c) Morphs por mínimos cuadrados** (`K ≈ 30` incógnitas, baratísimo con jacobiano numérico):

```python
from scipy.optimize import least_squares

def fit_morphs(tpl, lm2d, seg, cam, reg=0.05):
    def residuals(alpha):
        V = tpl.V0 + np.tensordot(alpha, tpl.T, axes=1)        # T: (K, n, 3)
        r_lm  = (cam.project(V[tpl.lm_idx]) - lm2d).ravel() / 2.0          # px
        sil   = silhouette_vertices(V, tpl.F, cam)                           # vértices de contorno en esta vista
        r_sil = sample(seg.dist_to_boundary, cam.project(V[sil])) / 2.0     # distancia al borde de la máscara
        r_in  = sample(seg.dist_outside,     cam.project(V)) / 1.0          # penaliza vértices fuera de la silueta
        return np.concatenate([r_lm, r_sil, r_in, reg * alpha])
    return least_squares(residuals, np.zeros(tpl.K), bounds=(tpl.lo, tpl.hi)).x
```

`dist_to_boundary` = distancia euclídea (sin signo) al borde de la máscara; `dist_outside` = `distanceTransform(~mask)`, cero dentro. `silhouette_vertices` = vértices con alguna arista entre una cara que mira a cámara y otra que no (`n·d` cambia de signo). Se repite con la vista lateral (y, z) si existe.

**d) Ajuste fino de silueta con edición Laplaciana** (Sorkine 2004) o ARAP (Sorkine & Alexa 2007): los vértices de contorno se atraen al punto más cercano del contorno de **su** parte 2D; el resto de la malla sigue preservando sus coordenadas diferenciales (los detalles de la plantilla: nudillos, clavículas, loops faciales) y la simetría.

```python
import igl, scipy.sparse as sp, scipy.sparse.linalg as spla

def laplacian_fit(V, F, cons_idx, cons_pos, w_axis=(10.0, 10.0, 0.5)):
    L = igl.cotmatrix(V, F)                        # Laplaciano cotangente (n,n), disperso
    delta = L @ V                                  # coordenadas diferenciales de la plantilla
    m = len(cons_idx)
    out = np.empty_like(V)
    for axis in range(3):                          # x,y fuertes (vienen de la imagen); z débil (se queda donde está)
        C = sp.csr_matrix((np.full(m, w_axis[axis]), (np.arange(m), cons_idx)), shape=(m, len(V)))
        A = sp.vstack([L, C]).tocsc()
        b = np.concatenate([delta[:, axis], w_axis[axis] * cons_pos[:, axis]])
        out[:, axis] = spla.lsqr(A, b, x0=V[:, axis], atol=1e-8, btol=1e-8)[0]
    return out
```

Bucle: deformar → recalcular contorno → buscar objetivos → deformar, 5–10 iteraciones, hasta que la distancia media contorno↔máscara sea < 1 px. Tras cada iteración, **simetrización**: `V = (V + mirror(V)) / 2` salvo que el usuario marque el personaje como asimétrico (hombrera, un guante). Para desplazamientos grandes (personajes muy estilizados) se sustituye el Laplaciano por `igl.ARAP(V, F, 3, cons_idx).solve(cons_pos, V)`, que conserva rotaciones locales.

Con **vista lateral** se repite el paso d) alternando frontal/lateral: en la lateral el contorno se atrae en (y, z) y x queda débil. Con **vista trasera** se comprueba la silueta espejada (y se usa sobre todo para textura). Tres vistas ortográficas ≈ un *visual hull* (Laurentini 1994) del personaje, pero en vez de tallarlo usamos sus siluetas como restricciones y así la topología sigue siendo la de la plantilla.

**Manos y cara con ranuras extra.** En una foto de cuerpo entero la mano mide 30–60 px: la geometría sale de la plantilla escalada por el ancho de muñeca y la longitud muñeca→punta medida en la silueta. Con la ranura "manos" (palma abierta sobre fondo liso) se ajusta por silueta la longitud y el grosor de cada dedo. Con la ranura "rostro frontal/lateral" se ejecutan c) y d) restringidos a la parte `Face` + `Head` + `Ear_*` con la cámara de ese primer plano (un ajuste de cara con 1500–2500 px de alto marca la diferencia en fidelidad).

### 6.2 Modo B: muchas fotos → SfM/MVS → ajuste no rígido

Para fotogrametría clásica hacen falta 20–80 fotos con 60–80 % de solape (vuelta completa, 2–3 alturas), sujeto quieto, luz difusa. Con sólo frente/perfil/espalda el SfM **no** funciona (no hay correspondencias suficientes entre vistas a 90°): ése es el modo A.

1. **SfM + MVS**: COLMAP por línea de comandos (`colmap automatic_reconstructor --workspace_path ws --image_path imgs --dense 1` → `fused.ply`), o OpenMVG + OpenMVS. El módulo `sfm` de OpenCV existe en *contrib*, pero no viene en los wheels de pip (requiere compilar con Ceres); COLMAP es más práctico.
2. **Escala y alineación**: SfM reconstruye a escala desconocida. Se fija con la altura del personaje (o un marcador de tamaño conocido en escena) y se alinea la nube a la plantilla ya ajustada en modo A con la primera foto (Procrustes sobre landmarks + `open3d.pipelines.registration.registration_icp`).
3. **Limpieza de nube** (Open3D): `remove_statistical_outlier`, recorte al bounding box de la plantilla ×1.1, normales estimadas y orientadas hacia las cámaras.
4. **Non‑rigid ICP** (Amberg 2007, implementado con el mismo solver de §6.1 d): en cada iteración, cada vértice de la plantilla busca el punto de nube más cercano dentro de un radio `r` cuya normal forme < 60° con la suya; esos puntos son restricciones blandas; se resuelve ARAP; se reduce `r` y la rigidez (p. ej. 10 → 1) en 8–12 iteraciones. Resultado: los vértices de la plantilla sobre la superficie medida, con la topología intacta.
5. Las cámaras calibradas por SfM se guardan en `cams` y hacen que la proyección de texturas (§8) sea exacta en vez de ortográfica.

### 6.3 Pelo en bloques, ojos, boca, dientes y orejas

- **Pelo**: a partir de la etiqueta "pelo" (§4) en vista frontal (y lateral si existe). Se copia la región del cráneo cuya proyección cae en la máscara de pelo y se desplaza cada vértice a lo largo de su normal un grosor `t`. `t` se obtiene por *ray marching* 2D: desde la proyección del vértice se avanza en la dirección de su normal proyectada hasta salir de la máscara de pelo; la distancia (en cm con `px_por_cm`) se acota a [0,3; 8] cm. Para vértices cuya normal mira a cámara (coronilla, frente) no hay información frontal: se usa la lateral o un grosor por defecto (2 cm). Se cierra el bloque con una "falda" hacia el cráneo y se suaviza 2–3 iteraciones. Mechones largos (coleta, flequillo, melena): esqueleto 2D de la máscara de pelo por debajo del contorno de la cabeza → tubos (cápsulas) a lo largo de cada rama con radio = transformada de distancia local. El resultado son bloques sólidos tipo personaje estilizado, que es lo que se puede reconstruir sin inventar; los sistemas de pelo por partículas quedan fuera (pueden añadirse después en Blender sobre el casco).
- **Ojos**: esferas de la plantilla (radio ≈ 1,2 cm escalado con la cabeza), centradas en los landmarks de ojos, con el radio máximo que no atraviese el anillo `eye_socket`. Color del iris = mediana de los píxeles del blob oscuro central del ojo (en el primer plano de cara); la textura del iris se genera **procedimentalmente** (fibras radiales con ruido, anillo limbal oscuro, pupila) a 512² con ese color; esclera y venas desde preset.
- **Boca y dientes**: `MouthBag` sigue a `Face` por el anillo `lips_inner`; `Teeth_*` se escalan con la anchura de la boca; texturas de preset. Nada de esto es visible en la foto, así que no se "adivina".
- **Orejas**: altura entre línea de cejas y base de la nariz (proporción canónica) o landmark lateral si hay perfil; tamaño por morph; silueta de perfil ajustada con el paso d) si hay vista lateral.

### 6.4 División en partes y sellado

```python
def split_parts(tpl):
    parts = {}
    for pid in np.unique(tpl.part_id):
        faces = tpl.F[tpl.part_id == pid]
        used, local = np.unique(faces, return_inverse=True)        # duplica los vértices de anillo en cada pieza
        P = Part(id=tpl.parts[pid].name, V=tpl.V[used], F=local.reshape(faces.shape), uv=tpl.uv_for(faces),
                 orig_index=used)                                    # orig_index: para copiar pesos y simetría
        P.rings = {name: np.searchsorted(used, ring) for name, ring in tpl.rings_of(pid).items()}
        P.caps  = [make_cap(P, ring, inset=0.4) for ring in P.rings.values()]      # tapa interna hundida 4 mm
        parts[P.id] = P
    return parts
```

Después: comprobación de que cada par de anillos coincide a 0,0 cm, normales recalculadas por pieza (las tapas no participan en el suavizado de normales de la piel), opcionalmente sockets (§2) y fusiones del `manifest.merge`.

### 6.5 Control de calidad geométrico (automático, se muestra en la UI)

- IoU entre la máscara de entrada y la silueta renderizada del modelo: objetivo ≥ 0,97 frontal, ≥ 0,93 lateral.
- 100 % quads por pieza (las tapas pueden ser abanicos de triángulos), sin aristas no‑manifold, sin caras degeneradas (`pymeshlab` o `trimesh` para el informe).
- Asimetría media < 0,2 cm salvo que esté activada la asimetría.
- Ningún vértice de `Face` dentro de `Eye_*`; ninguna autointersección entre piezas adyacentes en pose de reposo.

---

## 7. UV

**Regla principal: heredar.** Como el ajuste no cambia la topología, las UV de la plantilla (ya desplegadas por parte, con cortes en zonas ocultas: interior de brazos y piernas, nuca, bajo el pelo) se conservan tal cual. Es determinista, no tiene estiramientos sorpresa y permite que una textura hecha para una plantilla sirva para cualquier personaje generado con ella.

**Cuándo desplegar de nuevo**: piezas generadas o recortadas en tiempo de ejecución (pelo, mechones, dedos separados, piezas fusionadas por `merge`) y cuando el usuario pide "redesplegar" porque un morph extremo ha estirado demasiado una isla.

- **LSCM** (Lévy 2002): mapea cada triángulo a 2D minimizando la energía conforme `Σ_t A_t ‖∇u_t − rot90(∇v_t)‖²` (condiciones de Cauchy‑Riemann por triángulo) con dos vértices fijados; es un sistema lineal disperso. En Python: `uv, _ = igl.lscm(V, F_tris, b, bc)` con `b` = los dos vértices anclados y `bc` sus UV (libigl exige triángulos: los quads se parten en dos temporalmente; las UV por vértice resultantes valen para la malla en quads).
- **ABF++** (Sheffer 2005): optimiza los ángulos de los triángulos y reconstruye la parametrización; menos distorsión de área que LSCM en islas grandes. No está en libigl‑python; se usa Blender en modo headless (`bpy.ops.uv.unwrap(method='ANGLE_BASED')`; `'CONFORMAL'` es LSCM) o se implementa a partir del paper.
- **Costuras automáticas** para tubos (mechones, dedos): camino geodésico más corto entre un vértice de cada anillo elegido en la cara menos visible desde las cámaras de entrada (`igl.exact_geodesic` o Dijkstra sobre aristas).
- **Atlas y empaquetado**: `xatlas` (`vmapping, indices, uvs = xatlas.parametrize(V, F)`) hace cartas + empaquetado si no se quiere controlar el corte; para islas ya desplegadas, un empaquetador *shelf* propio por atlas con `padding` de 8 px a 2k.
- **Densidad de texel**: uniforme entre piezas del mismo atlas; la cara va en su propio atlas con ×3 de densidad.

Atlas por defecto: `Face+Head+Ears` 2048², `Torso` (cuello, pecho, abdomen, cadera, hombros) 2048², `Arms` (brazos, antebrazos, manos) 2048², `Legs` (muslos, espinillas, pies) 2048², `Hair` 1024², `Eyes` 512², `Mouth` 512². Los nombres de atlas se guardan en `manifest.parts[].atlas`.

---

## 8. Texturas: proyección (texture baking) desde las fotos

Para cada atlas y cada vista `v` con cámara `cam_v`:

1. **Rasterizar las islas en espacio UV** (resolución del atlas): por texel se obtiene la cara y las coordenadas baricéntricas; de ahí la posición 3D `p` y la normal `n`.
2. **Proyectar** `q = cam_v.project(p)` (ortográfica en modo A, calibrada en modo B).
3. **Visibilidad**: *z‑buffer* del modelo completo (todas las piezas, incluido el pelo) renderizado desde `cam_v`, o lanzamiento de rayos `p → cámara` con `trimesh` + `embreex`. Un texel ocluido no se pinta desde esa vista.
4. **Peso** `w_v = max(0, n·d_v)^2 · vis · feather(q)`: `d_v` es la dirección a cámara; `feather` decae a 0 en los últimos 10 px antes del borde de la máscara (los bordes proyectados son poco fiables).
5. **Muestreo bilineal** de la foto original (no de la reducida), en espacio lineal (sRGB → lineal antes de mezclar).
6. **Mezcla multivista**: `color = Σ w_v c_v / Σ w_v`. La frontal manda en cara y pecho, la trasera en espalda, las laterales en los flancos.
7. **Relleno por simetría**: texels sin ninguna vista toman el color del vértice espejo (`mirror`) si éste sí fue visto (axila derecha ← izquierda).
8. **Inpainting clásico** de lo que queda (`cv2.inpaint(tex, holes, 3, cv2.INPAINT_TELEA)`), y **dilatación de 8–16 px** del color fuera de las islas para que el mipmapping no sangre.
9. **De‑lighting aproximado** (opcional, interruptor en la UI): la foto lleva sombreado "cocinado". Se estima la iluminación de baja frecuencia `S = GaussianBlur(L, σ ≈ 3 % del alto)` por etiqueta de material y se divide: `albedo = color · mean(S)/S`, con recorte a [0,5; 1,6]. No es un verdadero *intrinsic decomposition*; con luz uniforme da un albedo utilizable, con luz dura deja sombras y conviene desactivarlo.
10. **Ojos, boca y dientes**: texturas procedimentales/preset de §6.3 copiadas a sus atlas.

Resultado por atlas: `BaseColor` (sRGB PNG 8 bit) + máscara de confianza (qué texels se vieron de verdad, útil en la UI para mostrar en rojo lo inventado).

---

## 9. Materiales PBR a partir de la imagen

### 9.1 Clasificación de materiales

Las etiquetas de §4 (piel, pelo, prenda_1..n, calzado, accesorio) se proyectan al atlas igual que el color y definen **un material por etiqueta y pieza**. Cada clase tiene un preset físico de partida:

| Clase | Roughness base | Metallic | Notas |
|---|---|---|---|
| Piel | 0,50 | 0 | subsurface opcional (color de preset) |
| Pelo (bloque) | 0,45 | 0 | anisotropía opcional en el motor |
| Prenda tela | 0,85 | 0 | |
| Cuero / plástico | 0,45 | 0 | detectado como "brillante no metálico" |
| Metal | 0,30 | 1 | sólo si el usuario confirma (ver abajo) |
| Esclera / córnea | 0,35 / 0,05 | 0 | |

Detección de brillo sin IA: por etiqueta se calcula el porcentaje de píxeles con `L > 0,9` y saturación baja (reflejos especulares) y el contraste local (desviación típica de `L` en ventanas de 15 px). Mucho reflejo + saturación baja + contraste alto → se sugiere "metal/plástico" y la UI lo marca para confirmar; el programa no decide solo.

### 9.2 Mapas generados

- **Normal map** (relieve a partir de la imagen): escala de grises → paso alto (`g − blur(g, σ=8)`) para quitar el sombreado global → gradientes Sobel → vector por píxel.

  ```python
  def normal_from_albedo(albedo_bgr, strength=2.0, directx=True):
      g = cv2.cvtColor(albedo_bgr, cv2.COLOR_BGR2GRAY).astype(np.float32) / 255.0
      g = g - cv2.GaussianBlur(g, (0, 0), 8.0)                       # sólo alta frecuencia (poros, costuras, tejido)
      dx = cv2.Sobel(g, cv2.CV_32F, 1, 0, ksize=3)
      dy = cv2.Sobel(g, cv2.CV_32F, 0, 1, ksize=3)
      # filas crecen hacia abajo: OpenGL (Y+ arriba) → G = +dy; DirectX (Y+ abajo) → G = -dy
      n = np.dstack([-dx * strength, (-dy if directx else dy) * strength, np.ones_like(g)])
      n /= np.linalg.norm(n, axis=2, keepdims=True)
      return (n[..., ::-1] * 0.5 + 0.5)                                # XYZ→[0,1], invertido a BGR para cv2.imwrite; guardar lineal, no sRGB
  ```

  Unreal espera la convención DirectX (verde hacia abajo); Blender/three.js, OpenGL. Se exporta con selector. La fuerza se baja en piel (0,8) y se sube en tejidos (2–3).
- **Roughness**: `rough = clip(base_clase − a·reflejo_local + b·variación_alta_frecuencia, 0, 1)`; `reflejo_local` = `L` suavizada por encima de 0,8 (zonas claras y poco saturadas = brillo), `variación` = paso alto en valor absoluto (tejido rugoso). Inversión/contraste tal como pide el brief, pero anclado al preset de la clase para que una camiseta blanca no salga de espejo.
- **Metallic**: constante por material (0 ó 1), salvo máscara pintada por el usuario.
- **AO**: se **hornea desde la geometría**, no desde la foto: por texel (o por vértice e interpolado) 32–64 rayos en el hemisferio de la normal con `trimesh.ray` (+ `embreex`), AO = fracción no ocluida; se mezcla al 30 % con cavidad de la imagen (paso alto negativo). Así las axilas, bajo el pelo y entre dedos oscurecen aunque la foto esté plana.
- **Empaquetado ORM** (R = AO, G = Roughness, B = Metallic): es lo que glTF admite directamente (`occlusionTexture` lee R, `metallicRoughnessTexture` lee G y B de la misma imagen) y lo que Unreal usa normalmente.

Salida por material: `BaseColor.png` (sRGB), `Normal.png` (lineal), `ORM.png` (lineal), más `Roughness`/`Metallic`/`AO` sueltos si se marca "mapas separados".

---

## 10. Rigging y pesos automáticos

### 10.1 Jerarquía (nombres del maniquí de UE5 para retarget directo con IK Rig/IK Retargeter)

```
root
└─ pelvis
   ├─ spine_01 ─ spine_02 ─ spine_03 ─ spine_04 ─ spine_05
   │  ├─ neck_01 ─ neck_02 ─ head ─ (eye_l, eye_r, jaw)            ← opcionales
   │  ├─ clavicle_l ─ upperarm_l ─ lowerarm_l ─ hand_l
   │  │     ├─ thumb_01_l ─ thumb_02_l ─ thumb_03_l
   │  │     ├─ index_metacarpal_l ─ index_01_l ─ index_02_l ─ index_03_l
   │  │     ├─ middle_metacarpal_l ─ middle_01_l ─ … · ring_… · pinky_…
   │  │     └─ (upperarm_twist_01_l, lowerarm_twist_01_l)          ← twist opcionales
   │  └─ clavicle_r … (espejo)
   ├─ thigh_l ─ calf_l ─ foot_l ─ ball_l   (+ thigh_twist_01_l, calf_twist_01_l)
   └─ thigh_r … (espejo)
```

### 10.2 Posición de las articulaciones

Como las piezas están cortadas **en** las articulaciones, la posición de cada articulación es, directamente, el **centroide del anillo compartido** (`elbow_l` → `lowerarm_l`, `knee_l` → `calf_l`, `wrist_l` → `hand_l`, `neck_base` → `neck_01`…). Es exacto, determinista y sigue al personaje aunque sea muy estilizado.

El **eje medio (Medial Axis Transform, Blum 1967)** se usa para lo que los anillos no dan: orientación (*roll*) de cada hueso, centro de las piezas sin anillo proximal claro (pecho, cadera, cabeza) y colocación de los huesos de columna dentro del torso.

```python
from scipy import ndimage

def medial_axis_points(solid, pitch, origin):           # solid: bool (nx,ny,nz), vóxeles rellenos de la pieza
    dt = ndimage.distance_transform_edt(solid) * pitch   # distancia al exterior
    ridge = solid & (dt >= ndimage.maximum_filter(dt, size=3) - 1e-6) & (dt > 2 * pitch)
    pts = np.argwhere(ridge) * pitch + origin            # puntos del eje medio discreto
    return pts, dt[ridge]                                # radio local = grosor de la extremidad
```

`solid` sale de `trimesh.Trimesh.voxelized(pitch).fill()` (`pitch` = 0,5 cm para extremidades, 1 cm para torso). Para piezas cilíndricas, PCA de `pts` da el eje del hueso; el *roll* se fija con la normal media del anillo y la regla "X del hueso hacia el hijo, Z hacia la espalda". Para la columna, los puntos del eje medio de `Chest+Abdomen+Hips` ordenados por altura se parten en 5 segmentos de igual longitud (`spine_01..05`).

### 10.3 Pesos: Geodesic Voxel Binding (Dionne & de Lasa 2013)

La distancia euclídea haría que el pecho se moviera con el brazo (están cerca en línea recta). La **distancia geodésica dentro del volumen** del cuerpo no: del brazo al pecho se pasa por el hombro. Algoritmo:

1. Voxelizar la **unión de todas las piezas** (una sola rejilla sólida, 128³–256³, con las tapas internas ignoradas) para que la distancia fluya a través de las articulaciones.
2. Para cada hueso, marcar como fuente los vóxeles que atraviesa el segmento `head→tail`.
3. Dijkstra (o *fast marching*) desde las fuentes de cada hueso por la rejilla 26‑conexa restringida a vóxeles sólidos → `d_b(voxel)`.
4. Para cada vértice, tomar su vóxel (o el sólido más cercano), `w_b = (1/d_b)^k`, quedarse con las 4–8 influencias mayores, normalizar.
5. Suavizar los pesos 3–5 iteraciones con el Laplaciano de la malla **completa** (piezas unidas por sus anillos), renormalizar, espejar L↔R y copiar los pesos de anillo a ambas piezas (§2).

```python
import scipy.sparse as sp
from scipy.sparse.csgraph import dijkstra

OFFSETS = np.array([(i, j, k) for i in (-1, 0, 1) for j in (-1, 0, 1) for k in (-1, 0, 1) if (i, j, k) != (0, 0, 0)])

def voxel_graph(solid, pitch):
    idx = -np.ones(solid.shape, np.int64); n = int(solid.sum()); idx[solid] = np.arange(n)
    rows, cols, w = [], [], []
    for d in OFFSETS:
        a = idx[max(0, d[0]):solid.shape[0] + min(0, d[0]), max(0, d[1]):solid.shape[1] + min(0, d[1]), max(0, d[2]):solid.shape[2] + min(0, d[2])]
        b = idx[max(0, -d[0]):solid.shape[0] + min(0, -d[0]), max(0, -d[1]):solid.shape[1] + min(0, -d[1]), max(0, -d[2]):solid.shape[2] + min(0, -d[2])]
        ok = (a >= 0) & (b >= 0)
        rows.append(a[ok]); cols.append(b[ok]); w.append(np.full(ok.sum(), np.linalg.norm(d) * pitch))
    G = sp.csr_matrix((np.concatenate(w), (np.concatenate(rows), np.concatenate(cols))), shape=(n, n))
    return G, idx

def geodesic_voxel_weights(solid, pitch, origin, bones, V, k=2.0, max_inf=4):
    G, idx = voxel_graph(solid, pitch)
    D = np.empty((len(bones), G.shape[0]), np.float32)
    for b, bone in enumerate(bones):
        src = idx[tuple(bone.voxels_on_segment(solid, pitch, origin).T)]
        D[b] = dijkstra(G, directed=False, indices=src[src >= 0], min_only=True)
    vox = idx[tuple(nearest_solid_voxel(V, solid, pitch, origin).T)]
    d = D[:, vox].T + 1e-3                             # (nV, nBones)
    W = (1.0 / d) ** k
    keep = np.argsort(-W, axis=1)[:, :max_inf]
    Wk = np.zeros_like(W); np.put_along_axis(Wk, keep, np.take_along_axis(W, keep, 1), 1)
    return Wk / Wk.sum(1, keepdims=True)
```

Con 128³ hay ~2 M vóxeles de los que el cuerpo ocupa ~10 %; cada Dijkstra sobre ~200 k nodos tarda menos de un segundo, y hay unos 70 huesos. `k` controla la suavidad (1 = muy blando, 4 = casi rígido). `scikit‑fmm` sustituye a Dijkstra si se quiere la distancia continua.

### 10.4 Dos modos de skinning

- **Suave** (por defecto): pesos GVB con 4–8 influencias.
- **Rígido por pieza**: cada pieza al 100 % de su hueso, mezcla sólo en los dos loops junto al anillo. Da el aspecto articulado "de muñeco" y es trivial de depurar.

### 10.5 Validación automática del rig

- Suma de pesos = 1 en todos los vértices; ninguna influencia fuera de la pieza + sus vecinas.
- Pesos de anillo idénticos en las dos piezas (se copian de un único cálculo sobre `orig_index`).
- Test de poses: codo 120°, rodilla 130°, hombro 90°, cuello 45°. Se mide la pérdida de área de sección en la articulación; si supera el 20 % se activan huesos *twist* o se sube `k`. Las poses se muestran en el visor con un slider.

---

## 11. Exportación e importación en Unreal

- **glTF 2.0 (`.glb`)**: un `skin` común, un `node`/`mesh` por pieza (nombre = `manifest.parts[].id`), `JOINTS_0`/`WEIGHTS_0` (dos conjuntos si se permiten 8 influencias), materiales con `baseColorTexture`, `normalTexture`, `metallicRoughnessTexture` y `occlusionTexture` apuntando a la misma ORM, blendshapes de `Face` como *morph targets*. Unidades: metros y Y arriba (se convierte desde cm). Con `pygltflib` se escribe a mano (buffers + accessors); es la ruta que usa el visor de la UI.
- **FBX** (lo más cómodo para Unreal y Maya): mediante `bpy` (Blender instalado desde pip, modo headless): crear armadura + objetos, modificador *Armature*, grupos de vértices con los pesos, `bpy.ops.export_scene.fbx(use_armature_deform_only=True, add_leaf_bones=False, mesh_smooth_type='FACE', ...)`. Los ajustes de escala/ejes varían con la versión de Blender: validar con un ciclo de importación en UE antes de dar la ruta por buena.
- **`manifest.json`** + carpeta `textures/` + `report.json` (IoU, estadísticas de malla, materiales, influencias).

En Unreal (este proyecto):

1. Importar el FBX/glTF con "Combine Meshes" desactivado: una *Skeletal Mesh* por pieza compartiendo un único asset *Skeleton*. Al usar los nombres de UE5, el *IK Retargeter* desde Manny/Quinn funciona sin mapear huesos a mano.
2. En el Blueprint del personaje: un `SkeletalMeshComponent` líder (`Hips` o el torso) y las demás piezas como componentes hijos con **Leader Pose Component** (`SetLeaderPoseComponent`), o fusionar en tiempo de ejecución con `FSkeletalMeshMerge` cuando no haga falta cambiar piezas.
3. Esta modularidad es la misma que usa la taquilla del lobby: pelo, cabeza, torso y piernas se pueden intercambiar entre personajes generados con la misma plantilla porque comparten anillos y esqueleto.

---

## 12. Interfaz de usuario

### 12.1 Stack

- **Opción recomendada (un solo lenguaje): PySide6 + QML + Qt Quick 3D.** Esquinas redondeadas, sombras y animaciones son triviales en QML; `View3D` renderiza PBR (`PrincipledMaterial` con `baseColorMap`, `normalMap`, `roughnessMap`, `metalnessMap`, `occlusionMap`, `lightProbe` HDR) y `RuntimeLoader` (`QtQuick3D.AssetUtils`, Qt ≥ 6.2) carga el `.glb` en tiempo de ejecución; Qt Quick 3D soporta *vertex skinning* y *morph targets*, así que el visor puede mover el rig y las expresiones. El pipeline corre en un proceso aparte (`multiprocessing`) y publica progreso a la UI; la vista 3D se recarga tras cada etapa (gris tras el ajuste, texturizado tras §8, PBR tras §9).
- **Alternativa web: Tauri (o Electron) + React + three.js (react‑three‑fiber)** y el núcleo Python detrás de un servidor local (FastAPI + WebSocket). `GLTFLoader` + `MeshStandardMaterial` + `OrbitControls` + `RoomEnvironment` cubren el visor; CSS cubre la estética. Más código de "fontanería", pero una UI que cualquier front‑end sabe mantener.

### 12.2 Layout

```
┌──────────────────────────────────────────────────────────────────────────────┐
│  ● Imagen→3D                 Proyecto: heroína_01            ⟳  ⚙  ─  □  ✕   │
├──────────┬─────────────────────────────────────────────┬─────────────────────┤
│ Pasos    │                                             │ Inspector           │
│ ○ Entrada│   ┌───────────────────────────────────────┐ │ ┌─────────────────┐ │
│ ○ Máscara│   │                                       │ │ │ Partes          │ │
│ ○ Puntos │   │        Visor 3D (PBR, HDRI,           │ │ │ 👁 Head         │ │
│ ● Ajuste │   │        orbit/pan/zoom, rejilla)        │ │ │ 👁 Face         │ │
│ ○ Textura│   │                                       │ │ │ 👁 Hair     ... │ │
│ ○ Materia│   │                                       │ │ ├─────────────────┤ │
│ ○ Rig    │   └───────────────────────────────────────┘ │ │ Material: piel  │ │
│ ○ Export │   [Sólido] [Wire] [UV] [Pesos] [Explosión ▮▯]│ │ Roughness ▮▮▮▯▯ │ │
│          │   Pose: codo ▮▮▯▯  rodilla ▮▯▯▯  cuello ▯▯▯▯ │ │ Normal   ▮▮▯▯▯  │ │
├──────────┴─────────────────────────────────────────────┴─────────────────────┤
│  Ajustando silueta · iteración 6/10 · IoU 0,964  ▮▮▮▮▮▮▮▮▯▯   [Cancelar]      │
└──────────────────────────────────────────────────────────────────────────────┘
```

Pantallas (una por paso del pipeline; el usuario puede volver a cualquiera y se reanuda desde ahí):

1. **Entrada**: ranuras arrastrar‑y‑soltar (frontal*, lateral, trasera, cara frontal, cara lateral, manos; o carpeta de fotos para modo B), plantilla (masculino/femenino/estilizado, sugerida por la relación hombros/cadera de la silueta), altura en cm, interruptores (balance de blancos, de‑lighting, asimetría).
2. **Máscara**: imagen con la silueta superpuesta, rectángulo de GrabCut editable, pincel añadir/quitar, botón "recalcular bordes". Zonas de material coloreadas con la etiqueta editable al clic.
3. **Puntos**: landmarks y esqueleto 2D arrastrables, con guías de proporción (líneas de cabezas).
4. **Ajuste**: visor con el modelo gris sobre la foto en semitransparencia (modo "calco": la foto como fondo ortográfico detrás del modelo), IoU en vivo, sliders de morphs para retocar a mano.
5. **Textura**: atlas por pestañas, mapa de confianza (en rojo lo rellenado), botón "re‑proyectar".
6. **Materiales**: lista de materiales con preset, sliders de fuerza de normal/roughness, vista previa PBR con HDRI seleccionable.
7. **Rig**: esqueleto dibujado sobre el modelo, modo de skinning, visualización de pesos por hueso (mapa de calor), sliders de pose de prueba.
8. **Exportar**: formato, convención de normal map, influencias máximas, separar dedos/pies, carpeta de salida, botón "abrir en Unreal" (copia a `Content/Characters/<nombre>/`).

### 12.3 Estilo

Tema oscuro: fondo `#121417`, paneles `#1B1F24`, borde `#2A2F36`, acento `#6C8CFF`, texto `#E6E8EB`; tipografía Inter; radios de 16 px en paneles, 10 px en botones y campos, 24 px en la tarjeta del visor; sombras suaves; transiciones de 120–160 ms. Ventana sin marco con barra de título propia.

```qml
import QtQuick; import QtQuick.Effects          // MultiEffect (Qt ≥ 6.5)
import QtQuick3D; import QtQuick3D.AssetUtils; import QtQuick3D.Helpers

// Panel redondeado (QML)
Rectangle { radius: 16; color: "#1B1F24"; border.color: "#2A2F36"; border.width: 1 }

// Visor 3D con esquinas redondeadas: View3D dentro de un Item con máscara
Item {
    id: viewerCard; layer.enabled: true
    layer.effect: MultiEffect { maskEnabled: true; maskSource: ShaderEffectSource { sourceItem: Rectangle { radius: 24; width: viewerCard.width; height: viewerCard.height } } }
    View3D {
        anchors.fill: parent
        environment: SceneEnvironment { backgroundMode: SceneEnvironment.Color; clearColor: "#0E1013"
                                        lightProbe: Texture { source: "qrc:/hdri/studio.hdr" } }
        PerspectiveCamera { id: cam; position: Qt.vector3d(0, 100, 350) }
        RuntimeLoader { id: model; source: app.currentGlb }        // se recarga tras cada etapa
        DirectionalLight { eulerRotation.x: -35; brightness: 0.6 }
    }
    OrbitCameraController { camera: cam; origin: model }
}
```

---

## 13. Estructura del proyecto y stack

```
img2char/
├─ app/                     # PySide6 + QML (pantallas, visor, tema)
│  ├─ main.py  qml/  theme/  workers.py (proceso del pipeline, progreso)
├─ core/
│  ├─ preprocess.py  segment.py  landmarks.py  cameras.py
│  ├─ template/  (loader.py, morphs.py, skeleton.py, manifest.py)
│  ├─ fit/       (procrustes.py, pose2d.py, morph_fit.py, laplacian_fit.py, nricp.py)
│  ├─ recon/     (colmap.py, cloud.py)
│  ├─ features/  (hair.py, eyes.py, mouth.py, ears.py)
│  ├─ parts.py   (split, caps, sockets, merge, qa)
│  ├─ uv/        (inherit.py, lscm.py, seams.py, pack.py)
│  ├─ texture/   (project.py, blend.py, symmetry.py, inpaint.py, delight.py)
│  ├─ materials/ (classify.py, normal.py, roughness.py, ao_bake.py, pack_orm.py)
│  ├─ rig/       (joints.py, medial_axis.py, gvb.py, smooth.py, validate.py)
│  └─ export/    (gltf.py, fbx_blender.py, report.py)
├─ templates/  male_v1/  female_v1/  stylized_v1/
├─ tests/      (siluetas sintéticas renderizadas desde la plantilla → el ajuste debe recuperar los morphs)
└─ pyproject.toml
```

| Librería | Para qué |
|---|---|
| `numpy`, `scipy` | álgebra, mínimos cuadrados, sistemas dispersos, `distance_transform_edt`, Dijkstra |
| `opencv-contrib-python` | PDI: GrabCut, Canny/Sobel, watershed, thinning, k‑means, inpainting, distancias, EXIF |
| `scikit-image` | `skeletonize`, morfología, medidas de regiones |
| `trimesh` + `embreex` | mallas, voxelización, rayos (visibilidad, AO), export básico |
| `open3d` | nubes de puntos, ICP, limpieza, visor de depuración |
| `libigl` (python) | `cotmatrix`, `ARAP`, `lscm`, geodésicas |
| `xatlas` | cartas y empaquetado UV automático |
| `pymeshlab` | limpieza/QA de mallas, filtros |
| `pygltflib` | escritura de glTF con skins y morph targets |
| `bpy` (opcional) | FBX con armadura, ABF++ |
| `scikit-fmm` (opcional) | distancias geodésicas por fast marching |
| COLMAP (externo, opcional) | SfM/MVS del modo B |
| `PySide6` | UI, QML, Qt Quick 3D |

```
pip install numpy scipy opencv-contrib-python scikit-image trimesh embreex open3d libigl xatlas pymeshlab pygltflib PySide6
```

---

## 14. Plan por fases y criterios de aceptación

| Fase | Entregable | Se da por buena cuando… |
|---|---|---|
| 1. Plantilla + visor | Plantillas M/F con `part_id`, anillos, landmarks, 10 morphs, esqueleto, UV; app que carga la plantilla, lista las piezas y las explosiona | Las 28 piezas cargan en el visor; los anillos coinciden a 0,0 cm; 100 % quads |
| 2. Segmentación + landmarks | Máscara, zonas de material, partes 2D, esqueleto 2D, editor manual | En 20 fotos de prueba la máscara necesita < 30 s de corrección; landmarks a < 2 % de la altura de su posición manual |
| 3. Ajuste grueso | Procrustes + posado + morphs | IoU frontal ≥ 0,90 sin tocar nada |
| 4. Ajuste fino | Laplaciano/ARAP, simetría, vista lateral, cara y manos con ranuras | IoU frontal ≥ 0,97, lateral ≥ 0,93; sin autointersecciones |
| 5. Pelo/ojos/boca/orejas + división | §6.3 y §6.4 | Vista explosionada limpia; tapas; sockets |
| 6. UV + texturas | Herencia, LSCM para piezas nuevas, proyección multivista, simetría, inpainting | Sin costuras visibles en frontal; mapa de confianza coherente |
| 7. Materiales PBR | Clasificación, Normal, Roughness, AO horneada, ORM | Vista PBR en el visor reconocible frente a la foto con el mismo HDRI |
| 8. Rig | Articulaciones, MAT, GVB, validación | Test de poses sin pérdidas > 20 %; retarget desde Manny en UE sin mapeo manual |
| 9. Export + Unreal | glTF/FBX, manifest, Leader Pose en el proyecto | Personaje caminando en `Lvl_Arena1v1` con animaciones del maniquí |
| 10. Modo B | COLMAP + NR‑ICP | Perfil medido con error < 1 cm frente a un escaneo de referencia |
| 11. Pulido UI | Tema, atajos, deshacer, proyectos guardados | Flujo completo < 5 min para una foto bien hecha |

Tests sin fotos reales: se renderizan siluetas y texturas **desde la propia plantilla** con morphs aleatorios conocidos; el pipeline debe recuperar esos morphs (error < 5 %) y una textura con PSNR > 30 dB en las zonas visibles. Es la prueba de regresión de todo el núcleo.

---

## 15. Límites conocidos y mitigaciones

| Límite | Mitigación |
|---|---|
| Profundidad no observable con una foto | Vista lateral; modo B; morphs de perfil ajustables a mano en "Ajuste" |
| Pose no A/T, brazos pegados al cuerpo | Partes 2D por watershed + posado 2D; aviso en "Entrada"; corrección manual de puntos |
| Ropa suelta (faldas, abrigos) no sigue la topología del cuerpo | Se ajusta la silueta con el cuerpo "envuelto" (la pieza de abdomen/muslos crece); para prendas separadas, segunda pasada con una plantilla de prenda (fase posterior) |
| Pelo realista por hebras | Fuera de alcance sin IA; bloques sólidos + mechones, pelo por partículas después en Blender |
| Sombreado "cocinado" en el albedo | De‑lighting aproximado; pedir luz difusa; renders de skins no tienen el problema |
| Metal/plástico indistinguibles de la tela brillante | Sugerencia automática + confirmación del usuario |
| Perspectiva en fotos cercanas | Aviso; cámara en perspectiva aproximada con EXIF; preferir fotos a ≥ 3 m con zoom |
| Manos pequeñas en la foto | Ranura de manos; si no, plantilla escalada por muñeca |

---

## 16. Referencias

- Canny, J. (1986). *A computational approach to edge detection.* IEEE TPAMI.
- Rother, C., Kolmogorov, V., Blake, A. (2004). *GrabCut: interactive foreground extraction using iterated graph cuts.* SIGGRAPH.
- Zhang, T. Y., Suen, C. Y. (1984). *A fast parallel algorithm for thinning digital patterns.* CACM.
- Blum, H. (1967). *A transformation for extracting new descriptors of shape* (Medial Axis Transform).
- Umeyama, S. (1991). *Least-squares estimation of transformation parameters between two point patterns.* IEEE TPAMI.
- Sorkine, O. et al. (2004). *Laplacian surface editing.* SGP. · Sorkine, O., Alexa, M. (2007). *As-rigid-as-possible surface modeling.* SGP.
- Amberg, B., Romdhani, S., Vetter, T. (2007). *Optimal step nonrigid ICP algorithms for surface registration.* CVPR.
- Laurentini, A. (1994). *The visual hull concept for silhouette-based image understanding.* IEEE TPAMI.
- Schönberger, J. L., Frahm, J.-M. (2016). *Structure-from-Motion revisited* (COLMAP). CVPR.
- Lévy, B. et al. (2002). *Least squares conformal maps for automatic texture atlas generation.* SIGGRAPH.
- Sheffer, A. et al. (2005). *ABF++: fast and robust angle based flattening.* ACM TOG.
- Telea, A. (2004). *An image inpainting technique based on the fast marching method.* J. Graphics Tools.
- Dionne, O., de Lasa, M. (2013). *Geodesic voxel binding for production character meshes.* SCA.
- Licencia de assets de MakeHuman (CC0): https://static.makehumancommunity.org/about/license.html
- Qt Quick 3D: `RuntimeLoader` (https://doc.qt.io/qt-6/qml-qtquick3d-assetutils-runtimeloader.html), *Vertex Skinning*, `MorphTarget`.
- libigl (python): https://libigl.github.io/libigl-python-bindings/ · xatlas: https://github.com/mworchel/xatlas-python · trimesh: https://trimesh.org · Open3D: https://www.open3d.org · OpenCV: https://docs.opencv.org
