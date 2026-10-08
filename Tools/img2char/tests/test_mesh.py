import numpy as np
import pytest

from img2char.core.mesh import QuadMesh, catmull_clark, mesh_stats, scale_transform
from img2char.core.mesh.topology import boundary_loops


def cube() -> QuadMesh:
    m = QuadMesh()
    m.box_grid((1, 1, 1), lambda i, j, k: np.array([i, j, k], float) * 2 - 1, lambda *_: "Box")
    return m


def signed_volume(V, F):
    tris = np.concatenate([F[:, [0, 1, 2]], F[:, [0, 2, 3]]])
    a, b, c = V[tris[:, 0]], V[tris[:, 1]], V[tris[:, 2]]
    return np.einsum("ij,ij->i", a, np.cross(b, c)).sum() / 6


def test_cube_is_closed_and_outward():
    m = cube()
    V, F, *_ = m.compact()
    st = mesh_stats(V, F)
    assert (st["faces"], st["boundary_edges"], st["non_manifold_edges"], st["flipped_edges"]) == (6, 0, 0, 0)
    assert signed_volume(V, F) == pytest.approx(8.0)


def test_catmull_clark_cube():
    V, F, *_ = cube().compact()
    V2, F2, edges = catmull_clark(V, F)
    assert len(F2) == 24 and len(V2) == 8 + 12 + 6 and len(edges) == 12
    st = mesh_stats(V2, F2)
    assert st["boundary_edges"] == 0 and st["flipped_edges"] == 0
    assert 0 < signed_volume(V2, F2) < 8.0  # el límite queda dentro de la jaula


def test_catmull_clark_keeps_boundary_on_open_patch():
    V = np.array([[0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0]], float)
    V2, F2, _ = catmull_clark(V, np.array([[0, 1, 2, 3]]))
    assert len(F2) == 4
    assert len(boundary_loops(F2)) == 1
    assert np.allclose(V2[:, 2], 0)


def test_extrude_and_subdivide_propagate_labels_and_loops():
    m = cube()
    top = [f for f in m.alive() if m.face_normal(f)[1] > 0.9]
    res = m.extrude(top, scale_transform(0.5, (0, 1, 0)), label="Top")
    m.loops["ring"] = res.loop_old
    assert len(res.sides) == 4
    s = m.subdivided()
    V, F, labels, _, loops = s.compact()
    assert labels.count("Top") == 20 and labels.count("Box") == 20  # los lados heredan la etiqueta
    assert len(loops["ring"]) == 8
    st = mesh_stats(V, F)
    assert st["boundary_edges"] == 0 and st["non_manifold_edges"] == 0 and st["flipped_edges"] == 0
