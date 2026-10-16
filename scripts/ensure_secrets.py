"""
Script de pre-build do PlatformIO.

Cria src/secrets.h a partir de src/secrets.example.h na primeira compilação,
evitando que o usuário esqueça de copiar o template. Não sobrescreve um
secrets.h existente (com valores reais já preenchidos).
"""

Import("env")  # noqa: F821 — injetado pelo PlatformIO

import os
import shutil


def ensure_secrets_h(src_dir, mode=0o600):
    """Cópia segura do template; retorna created|exists|missing_example."""
    example = os.path.join(src_dir, "secrets.example.h")
    target = os.path.join(src_dir, "secrets.h")

    if os.path.exists(target):
        return "exists"
    if not os.path.exists(example):
        return "missing_example"

    shutil.copyfile(example, target)
    os.chmod(target, mode)
    return "created"


SRC_DIR = os.path.join(env.subst("$PROJECT_DIR"), "src")  # noqa: F821


def _prebuild_hook(*_args, **_kwargs):
    result = ensure_secrets_h(SRC_DIR)
    if result == "created":
        print("[prebuild] secrets.h criado a partir do template.")
        print(
            "[prebuild] ATENÇÃO: preencha WIFI_SSID, WIFI_PASSWORD e o MQTT "
            "em src/secrets.h antes de publicar."
        )
    elif result == "exists":
        print("[prebuild] secrets.h já existe. Mantido.")
    else:
        print("[prebuild] AVISO: secrets.example.h não encontrado.")


# Mantém API importável pelos testes unitários via secrets_utils.py
# (este arquivo continua sendo o entrypoint do PlatformIO).

_prebuild_hook()
env.AddPreAction("upload", _prebuild_hook)  # noqa: F821
