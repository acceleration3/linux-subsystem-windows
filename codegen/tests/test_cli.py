"""Smoke tests for the codegen CLI. Phase 0 — only checks arg parsing."""

from __future__ import annotations

from pathlib import Path

from lsw_codegen.__main__ import main


def test_help(capsys):
    try:
        main(["--help"])
    except SystemExit as e:
        assert e.code == 0
    out = capsys.readouterr().out
    assert "lsw-codegen" in out


def test_missing_xml_errors(tmp_path: Path):
    rc = main(["--xml", str(tmp_path / "nope.xml"), "--out", str(tmp_path)])
    assert rc == 2


def test_runs_with_empty_profile(tmp_path: Path):
    xml = tmp_path / "vk.xml"
    xml.write_text("<registry><commands/></registry>")
    profile = tmp_path / "profile.toml"
    profile.write_text('[commands]\ninclude = []\n')
    ids = tmp_path / "ids.toml"
    ids.write_text("[commands]\n")
    out = tmp_path / "out"
    rc = main(
        [
            "--xml", str(xml),
            "--profile", str(profile),
            "--ids", str(ids),
            "--out", str(out),
        ]
    )
    assert rc == 0
    assert out.is_dir()
    # With no commands selected, codegen still emits headers (just empty).
    assert (out / "vk_protocol_ids.h").is_file()
