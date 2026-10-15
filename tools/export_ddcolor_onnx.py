"""Export the DDColor models Colorize downloads, as ONNX.

    python tools/export_ddcolor_onnx.py <DDColor checkout> <out dir>

DDColor (ICCV 2023, Apache 2.0): https://github.com/piddnad/DDColor. Needs
PyTorch, onnx, opencv-python and timm. The weights come from the
authors' Hugging Face repos:

    ddcolor_paper_tiny   ConvNeXt-T encoder  -> ddcolor_tiny.onnx
    ddcolor_modelscope   ConvNeXt-L encoder  -> ddcolor_large.onnx

Both take a 1x3x512x512 grey-as-RGB image in 0-1 and return 1x2x512x512: the
a and b of CIE Lab (OpenCV's float scale), to be put together with the
frame's own full-resolution lightness. Publish them with
tools/publish_assets.py under the tag the plugin's src/colorize.cpp uses.
"""

import hashlib
import sys
import urllib.request
from pathlib import Path

import onnx
import torch

CHECKOUT = Path(sys.argv[1]).resolve()
sys.path.insert(0, str(CHECKOUT))
from basicsr.archs.ddcolor_arch import DDColor  # noqa: E402
from ddcolor.pipeline import load_checkpoint_state_dict  # noqa: E402

SIZE = 512
MODELS = {
    "ddcolor_tiny.onnx": ("ddcolor_paper_tiny", "convnext-t"),
    "ddcolor_large.onnx": ("ddcolor_modelscope", "convnext-l"),
}


def weights(repo: str, cache: Path) -> Path:
    path = cache / f"{repo}.pth"
    if not path.is_file():
        urllib.request.urlretrieve(f"https://huggingface.co/piddnad/{repo}/resolve/main/pytorch_model.bin", path)
    return path


def export(name: str, repo: str, encoder: str, out: Path, cache: Path) -> None:
    # the configuration of DDColor's own scripts/export_onnx.py
    model = DDColor(encoder_name=encoder, decoder_name="MultiScaleColorDecoder",
                    input_size=[SIZE, SIZE], num_output_channels=2, last_norm="Spectral",
                    do_normalize=False, num_queries=100, num_scales=3, dec_layers=9)
    model.load_state_dict(load_checkpoint_state_dict(str(weights(repo, cache))), strict=True)
    model.eval()
    path = out / name
    torch.onnx.export(model, torch.rand(1, 3, SIZE, SIZE), path, opset_version=17,
                      input_names=["input"], output_names=["output"])
    # no onnxsim: it crashes on these on Windows, and ONNX Runtime optimizes
    # the graph when it loads it anyway
    onnx.checker.check_model(str(path))
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    print(f"{name}: {path.stat().st_size} bytes, sha256 {digest}")


def main() -> None:
    out = Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    cache = CHECKOUT / "pretrain"
    cache.mkdir(exist_ok=True)
    for name, (repo, encoder) in MODELS.items():
        export(name, repo, encoder, out, cache)


if __name__ == "__main__":
    main()
