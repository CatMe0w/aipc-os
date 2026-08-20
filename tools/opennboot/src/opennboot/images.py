"""Find the binaries this tool uploads to the device.

A source checkout wins over the copies inside the wheel, so that a local `make`
takes effect without a reinstall. `install` and `run` print the path they
resolved for openNBOOT, because that is the image that ends up in NAND.
"""

from __future__ import annotations

from importlib.resources import files
from pathlib import Path

# name -> path relative to the repository root
_IN_REPO = {
    "opennboot.bin": "baremetal/opennboot/opennboot.bin",
    "nf_read.bin": "tools/opennboot/stub/nf_read.bin",
    "nf_write.bin": "tools/opennboot/stub/nf_write.bin",
    "ddr_init_v1_88.bin": "tools/aipc-ddr-init/stub/ddr_init_v1_88.bin",
}

# What to tell the user when an image is missing from a source checkout.
_BUILD_HINT = {
    "opennboot.bin": "make -C baremetal/opennboot",
    "nf_read.bin": "make -C tools/opennboot/stub",
    "nf_write.bin": "make -C tools/opennboot/stub",
    "ddr_init_v1_88.bin": "make -C tools/aipc-ddr-init/stub",
}


class ImageError(RuntimeError):
    pass


def repo_root() -> Path | None:
    """The checkout this module runs from, or None when installed as a wheel."""
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
            f"{_IN_REPO[name]} is not built. Build it with: {_BUILD_HINT[name]}"
        )

    packaged = files("opennboot") / "data" / name
    if packaged.is_file():
        return Path(str(packaged))

    raise ImageError(
        f"{name} is missing from this installation. Reinstall opennboot, or "
        f"run it from a source checkout"
    )
