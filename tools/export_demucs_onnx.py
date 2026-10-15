"""Export the Demucs v4 models Stem Splitter downloads, as ONNX.

    python tools/export_demucs_onnx.py <out dir>

Needs PyTorch, onnx, and the Demucs copy in demucs.onnx with the STFT moved out
of the network - plugins/stem-splitter/fetch.py checks it out:

    python -m venv --system-site-packages venv
    venv\\Scripts\\pip install julius einops openunmix lameenc dora-search pyyaml tqdm diffq
    venv\\Scripts\\pip install --no-deps -e plugins/stem-splitter/demucs-onnx/demucs-for-onnx
    venv\\Scripts\\python tools/export_demucs_onnx.py out

Writes, from Meta's official pretrained weights (MIT):

    htdemucs.onnx            4 sources: drums, bass, other, vocals
    htdemucs_6s.onnx         6 sources: the 4 + guitar, piano
    htdemucs_ft_vocals.onnx  the vocals specialist of the fine-tuned bag

Publish them with tools/publish_assets.py under the tag the plugin's
src/stems.cpp downloads from.
"""

import hashlib
import sys
from pathlib import Path

import torch
from torch.nn import functional as F
from demucs.htdemucs import HTDemucs, standalone_magnitude, standalone_spec
from demucs.pretrained import get_model


def core_model(name: str) -> HTDemucs:
    model = get_model(name)
    if isinstance(model, HTDemucs):
        return model
    return model.models[0]  # a bag of one


def vocals_specialist() -> HTDemucs:
    # htdemucs_ft is a bag of four, one per source; its weights pick each
    # model's specialty
    bag = get_model("htdemucs_ft")
    vocals = bag.sources.index("vocals")
    return bag.models[next(i for i, weights in enumerate(bag.weights) if weights[vocals] == 1.0)]


def export(model: HTDemucs, path: Path) -> None:
    # demucs.onnx's convert-pth-to-onnx.py: one 7.8 s segment, its spectrogram
    waveform = torch.randn(1, 2, 343980)
    length = int(model.segment * model.samplerate)
    waveform = F.pad(waveform, (0, length - waveform.shape[-1]))
    magspec = standalone_magnitude(standalone_spec(waveform))
    torch.onnx.export(model, (waveform, magspec), path, export_params=True, opset_version=17,
                      do_constant_folding=True, input_names=["input"], output_names=["output"])
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    print(f"{path.name}: {path.stat().st_size} bytes, sha256 {digest}")


def main() -> None:
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    export(core_model("htdemucs"), out / "htdemucs.onnx")
    export(core_model("htdemucs_6s"), out / "htdemucs_6s.onnx")
    export(vocals_specialist(), out / "htdemucs_ft_vocals.onnx")


if __name__ == "__main__":
    main()
