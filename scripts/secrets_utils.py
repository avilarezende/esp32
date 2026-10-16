"""Utilitários para criação segura de src/secrets.h a partir do template."""

from __future__ import annotations

import os
import shutil
from pathlib import Path


def ensure_secrets_h(src_dir: str | Path, *, mode: int = 0o600) -> str:
    """Garante que secrets.h exista a partir de secrets.example.h.

    Returns:
        "created" se copiou o template,
        "exists" se secrets.h já existia,
        "missing_example" se o template não existe.
    """
    src = Path(src_dir)
    example = src / "secrets.example.h"
    target = src / "secrets.h"

    if target.exists():
        return "exists"

    if not example.exists():
        return "missing_example"

    shutil.copyfile(example, target)
    os.chmod(target, mode)
    return "created"
