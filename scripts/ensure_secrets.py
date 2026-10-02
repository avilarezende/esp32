"""
Script de pre-build do PlatformIO.

Cria src/secrets.h a partir de src/secrets.example.h na primeira compilação,
evitando que o usuário esqueça de copiar o template. Não sobrescreve um
secrets.h existente (com valores reais já preenchidos).
"""

Import("env")
import os
import shutil

SRC_DIR = os.path.join(env.subst("$PROJECT_DIR"), "src")
EXAMPLE = os.path.join(SRC_DIR, "secrets.example.h")
TARGET = os.path.join(SRC_DIR, "secrets.h")


def ensure_secrets_h(*_args, **_kwargs):
    if not os.path.exists(TARGET):
        if not os.path.exists(EXAMPLE):
            print("[prebuild] AVISO: secrets.example.h não encontrado.")
            return
        shutil.copyfile(EXAMPLE, TARGET)
        print("[prebuild] secrets.h criado a partir do template.")
        print(
            "[prebuild] ATENÇÃO: preencha WIFI_SSID, WIFI_PASSWORD e o MQTT "
            "em src/secrets.h antes de publicar."
        )
    else:
        print("[prebuild] secrets.h já existe. Mantido.")


# Roda antes de cada build e antes do upload
env.AddPreAction("buildprog", ensure_secrets_h)
env.AddPreAction("upload", ensure_secrets_h)