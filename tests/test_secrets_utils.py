"""Testes unitários do helper de secrets.h."""

from __future__ import annotations

import os
import stat
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from secrets_utils import ensure_secrets_h  # noqa: E402


@pytest.fixture()
def src_dir(tmp_path: Path) -> Path:
    d = tmp_path / "src"
    d.mkdir()
    (d / "secrets.example.h").write_text(
        '#define WIFI_SSID "SUA-REDE-WIFI"\n', encoding="utf-8"
    )
    return d


def test_creates_secrets_from_example(src_dir: Path) -> None:
    assert ensure_secrets_h(src_dir) == "created"
    target = src_dir / "secrets.h"
    assert target.exists()
    assert "SUA-REDE-WIFI" in target.read_text(encoding="utf-8")
    mode = stat.S_IMODE(target.stat().st_mode)
    assert mode == 0o600


def test_keeps_existing_secrets(src_dir: Path) -> None:
    target = src_dir / "secrets.h"
    target.write_text('#define WIFI_SSID "real"\n', encoding="utf-8")
    assert ensure_secrets_h(src_dir) == "exists"
    assert target.read_text(encoding="utf-8") == '#define WIFI_SSID "real"\n'


def test_missing_example(tmp_path: Path) -> None:
    src = tmp_path / "src"
    src.mkdir()
    assert ensure_secrets_h(src) == "missing_example"
    assert not (src / "secrets.h").exists()


def test_example_file_is_versioned() -> None:
    example = ROOT / "src" / "secrets.example.h"
    assert example.is_file()
    text = example.read_text(encoding="utf-8")
    assert "WIFI_SSID" in text
    assert "MQTT" in text or "WIFI_PASSWORD" in text


def test_secrets_h_not_tracked_by_git() -> None:
    import subprocess

    tracked = subprocess.check_output(
        ["git", "ls-files", "src/secrets.h"],
        cwd=ROOT,
        text=True,
    ).strip()
    assert tracked == ""
    gitignore = (ROOT / ".gitignore").read_text(encoding="utf-8")
    assert "src/secrets.h" in gitignore
