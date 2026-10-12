"""Verify images exported by aex_ui_smoke.jsx. Requires Pillow."""
import sys
import tempfile
from pathlib import Path

from PIL import Image


def verify(folder: Path) -> None:
    images = {}
    for name in ("source", "mask", "source-again"):
        with Image.open(folder / f"{name}.png") as image:
            images[name] = image.convert("RGB")
    source = images["source"]
    mask = images["mask"]
    center = source.getpixel((128, 128))
    assert source.size == mask.size == (256, 256), "Unexpected fixture dimensions"
    assert max(center) > 0 and len(set(center)) > 1, f"Source fixture must be colored: {center}"
    assert mask.getbbox() is None, (
        "View=Highlight Mask must be black for this dark source; "
        f"got RGB={mask.getpixel((128, 128))}. UI values did not reach AE rendering."
    )
    assert source.tobytes() == images["source-again"].tobytes(), "Same-frame View switching reused stale pixels"
    print("PASS: real AE Source -> Highlight Mask -> Source at the same frame")


if __name__ == "__main__":
    verify(Path(sys.argv[1]) if len(sys.argv) > 1 else Path(tempfile.gettempdir()) / "dlssnr-aex-ui-smoke")
