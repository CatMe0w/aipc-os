from __future__ import annotations

from importlib.resources import files
from pathlib import Path

# A source checkout wins over the copies inside the wheel, so that a local
# `make` takes effect without a reinstall.
_IN_REPO = {
    "opennboot.bin": "baremetal/opennboot/opennboot.bin",
    "nf_block0.bin": "tools/opennboot/stub/nf_block0.bin",
    "nf_backup.bin": "tools/opennboot/stub/nf_backup.bin",
    "nf_restore.bin": "tools/opennboot/stub/nf_restore.bin",
    "ddr_init_v1_88.bin": "tools/aipc-ddr-init/stub/ddr_init_v1_88.bin",
}

_BUILD_HINT = {
    "opennboot.bin": "make -C baremetal/opennboot",
    "nf_block0.bin": "make -C tools/opennboot/stub",
    "nf_backup.bin": "make -C tools/opennboot/stub",
    "nf_restore.bin": "make -C tools/opennboot/stub",
    "ddr_init_v1_88.bin": "make -C tools/aipc-ddr-init/stub",
}


class ImageError(RuntimeError):
    pass


def repo_root() -> Path | None:
    for parent in Path(__file__).resolve().parents:
        if (parent / "baremetal").is_dir() and (parent / "tools").is_dir():
            return parent
    return None


def resolve(name: str) -> Path:
    root = repo_root()
    if root is not None:
        candidate = root / _IN_REPO[name]
        if candidate.exists():
            return candidate
        raise ImageError(
            f"{_IN_REPO[name]}: file not found. Build it with: {_BUILD_HINT[name]}"
        )

    packaged = files("opennboot") / "data" / name
    if packaged.is_file():
        return Path(str(packaged))

    raise ImageError(
        f"{name}: not found in this installation. Reinstall opennboot or run "
        f"it from a source checkout"
    )
