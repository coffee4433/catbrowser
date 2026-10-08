import numpy as np
import pytest

from img2char.core.parts import split_parts
from img2char.core.template.build import build_templates


@pytest.fixture(scope="session")
def templates():
    return build_templates()


@pytest.fixture(scope="session")
def male(templates):
    return templates["male_v1"]


@pytest.fixture(scope="session")
def female(templates):
    return templates["female_v1"]


@pytest.fixture(scope="session")
def saved_dir(templates, tmp_path_factory):
    d = tmp_path_factory.mktemp("templates")
    for t in templates.values():
        t.save(d)
    return d


@pytest.fixture(scope="session")
def female_parts(female):
    return split_parts(female)


def reflect(V):
    return V * np.array([-1.0, 1.0, 1.0])
