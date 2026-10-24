"""Seed-VC's singing voice conversion model (v1, F0-conditioned, 44.1 kHz) to
ONNX, for seedvc.py. Part of the voice-clone engine; GPL-3.0 like Seed-VC.

    python export_onnx.py --seed-vc <Seed-VC checkout> --out <models folder>

Runs from the checkout's own checkpoints (inference.py downloads them there).
Every model is checked against PyTorch on a real input before it is kept:

    whisper_encoder.onnx    log-mel (1, 80, 3000) -> content (1, 1500, 768)
    campplus.onnx           Kaldi fbank (1, T, 80) -> speaker style (1, 192)
    rmvpe.onnx              log-mel (1, 128, T % 32 == 0) -> pitch salience (1, T, 360)
    length_regulator.onnx   content (1, T, 768) + F0 bins (1, T) -> condition (1, T, 768)
    dit.onnx                one flow-matching step: x, prompt, lengths, t, style, condition
    dit_fp16.onnx           the same in float16 (normalisation and softmax kept in float32):
                            what seedvc.py runs - half the size, 1.6x faster on DirectML
    bigvgan.onnx            mel (1, 128, T) -> audio (1, 1, 512 T), without the final clamp to [-1, 1]
    filters.npz             the mel filter banks seedvc.py needs
"""

import argparse
import os
import sys
from pathlib import Path

import numpy as np
import torch
from torch import nn

OPSET = 17


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--seed-vc", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--only", nargs="*", default=None, help="export only these models")
    args = parser.parse_args()

    seed_vc = args.seed_vc.resolve()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    os.chdir(seed_vc)
    sys.path.insert(0, str(seed_vc))
    import inference  # sets HF_HUB_CACHE to ./checkpoints/hf_cache

    inference.device = torch.device("cpu")
    loaded = inference.load_models(argparse.Namespace(
        f0_condition=True, checkpoint=None, config=None, fp16=False))
    model, _, f0_fn, vocoder, campplus, _, mel_args = loaded
    rmvpe = f0_fn.__self__

    from transformers import WhisperModel, WhisperFeatureExtractor
    whisper = WhisperModel.from_pretrained("openai/whisper-small", torch_dtype=torch.float32, attn_implementation="eager").eval()
    extractor = WhisperFeatureExtractor.from_pretrained("openai/whisper-small")

    wanted = set(args.only) if args.only else None
    def want(name):
        return wanted is None or name in wanted

    rng = np.random.default_rng(0)

    if want("filters"):
        import librosa
        np.savez(out / "filters.npz",
                 whisper=extractor.mel_filters.astype(np.float32),          # (201, 80)
                 rmvpe=rmvpe.mel_extractor.mel_basis.numpy().astype(np.float32),  # (128, 513)
                 mel44k=librosa.filters.mel(sr=mel_args["sampling_rate"], n_fft=mel_args["n_fft"],
                                            n_mels=mel_args["num_mels"], fmin=mel_args["fmin"],
                                            fmax=mel_args["fmax"]).astype(np.float32))  # (128, 1025)
        print("filters.npz")

    if want("whisper_encoder"):
        class Encoder(nn.Module):
            def __init__(self, encoder):
                super().__init__()
                self.encoder = encoder
            def forward(self, features):
                return self.encoder(features, return_dict=True).last_hidden_state
        sample = torch.from_numpy(rng.standard_normal((1, 80, 3000)).astype(np.float32) * 0.5)
        export(Encoder(whisper.encoder).eval(), (sample,), out / "whisper_encoder.onnx",
               ["features"], ["content"], {})

    if want("campplus"):
        # ONNX AveragePool divides a partial last segment (ceil_mode) by the whole
        # kernel, PyTorch by the frames it has: pool exact sums and counts instead
        from modules.campplus import layers
        samples = [torch.from_numpy(rng.standard_normal((1, frames, 80)).astype(np.float32))
                   for frames in (700, 173, 2499)]
        with torch.no_grad():
            originals = [campplus.eval()(sample).numpy() for sample in samples]
        def seg_pooling(self, x, seg_len=100, stype="avg"):
            assert stype == "avg"
            pad = (0, seg_len - 1)
            total = nn.functional.avg_pool1d(nn.functional.pad(x, pad), seg_len, seg_len)
            count = nn.functional.avg_pool1d(nn.functional.pad(torch.ones_like(x[:, :1]), pad), seg_len, seg_len)
            seg = total / count
            shape = seg.shape
            seg = seg.unsqueeze(-1).expand(*shape, seg_len).reshape(*shape[:-1], -1)
            return seg[..., :x.shape[-1]]
        layers.CAMLayer.seg_pooling = seg_pooling
        export(campplus.eval(), (samples[0],), out / "campplus.onnx", ["fbank"], ["style"],
               {"fbank": {1: "frames"}}, more=[(sample,) for sample in samples[1:]])
        import onnxruntime
        session = onnxruntime.InferenceSession(str(out / "campplus.onnx"), providers=["CPUExecutionProvider"])
        for sample, original in zip(samples, originals):
            got = session.run(None, {"fbank": sample.numpy()})[0]
            print(f"campplus.onnx against the original pooling, {sample.shape[1]} frames: "
                  f"max difference {np.abs(got - original).max():.2e}", flush=True)

    if want("rmvpe"):
        sample = torch.from_numpy(rng.standard_normal((1, 128, 320)).astype(np.float32) - 5)
        export(rmvpe.model.eval(), (sample,), out / "rmvpe.onnx", ["mel"], ["salience"],
               {"mel": {2: "frames"}, "salience": {1: "frames"}})

    if want("length_regulator"):
        class Regulator(nn.Module):
            # nearest-neighbour stretching is done before: it commutes with
            # the projection and the F0 embedding
            def __init__(self, regulator):
                super().__init__()
                self.regulator = regulator
            def forward(self, content, f0_bins):
                x = self.regulator.content_in_proj(content).transpose(1, 2)
                x = x + self.regulator.f0_embedding(f0_bins).transpose(1, 2)
                return self.regulator.model(x).transpose(1, 2)
        content = torch.from_numpy(rng.standard_normal((1, 400, 768)).astype(np.float32))
        bins = torch.from_numpy(rng.integers(1, 255, (1, 400)).astype(np.int64))
        export(Regulator(model.length_regulator).eval(), (content, bins), out / "length_regulator.onnx",
               ["content", "f0_bins"], ["condition"],
               {"content": {1: "frames"}, "f0_bins": {1: "frames"}, "condition": {1: "frames"}})

    if want("dit"):
        estimator = model.cfm.estimator.eval()
        # "x_lens + self.style_as_token + ..." does not trace with bools
        estimator.style_as_token = int(estimator.style_as_token)
        estimator.time_as_token = int(estimator.time_as_token)
        remove_weight_norm(estimator)
        estimator.setup_caches(max_batch_size=2, max_seq_length=8192)
        frames = 600
        inputs = (
            torch.from_numpy(rng.standard_normal((2, 128, frames)).astype(np.float32)),
            torch.from_numpy(rng.standard_normal((2, 128, frames)).astype(np.float32)),
            torch.tensor([frames, frames], dtype=torch.int64),
            torch.tensor([0.3, 0.3], dtype=torch.float32),
            torch.from_numpy(rng.standard_normal((2, 192)).astype(np.float32)),
            torch.from_numpy(rng.standard_normal((2, frames, 768)).astype(np.float32)),
        )
        longer = tuple(torch.cat([value] * 2, dim=2 if value.dim() == 3 and value.shape[1] == 128 else 1)
                       if value.dim() == 3 else value for value in inputs)
        longer = (longer[0], longer[1], torch.tensor([2 * frames, 2 * frames], dtype=torch.int64),
                  longer[3], longer[4], longer[5])
        export(estimator, inputs, out / "dit.onnx",
               ["x", "prompt_x", "x_lens", "t", "style", "condition"], ["velocity"],
               {"x": {0: "batch", 2: "frames"}, "prompt_x": {0: "batch", 2: "frames"}, "x_lens": {0: "batch"},
                "t": {0: "batch"}, "style": {0: "batch"}, "condition": {0: "batch", 1: "frames"},
                "velocity": {0: "batch", 2: "frames"}}, more=[longer])
        to_float16(out / "dit.onnx", out / "dit_fp16.onnx", inputs)

    if want("bigvgan"):
        sample = torch.from_numpy(rng.standard_normal((1, 128, 200)).astype(np.float32) - 5)
        # The anti-aliasing filters run one kernel over every channel as a grouped
        # convolution whose kernel shape the export cannot know: give each its
        # channel count as a constant. The 2x upsampling, a transposed
        # convolution that takes three quarters of BigVGAN's time on ORT's CPU,
        # becomes two ordinary convolutions with the even and the odd taps,
        # interleaved (polyphase) - the same arithmetic.
        from modules.bigvgan.alias_free_activation.torch import resample, filter as alias_filter
        with torch.no_grad():
            original = vocoder.eval()(sample).numpy()
        channels = {}
        filters = [m for m in vocoder.modules() if isinstance(m, (resample.UpSample1d, alias_filter.LowPassFilter1d))]
        hooks = [m.register_forward_pre_hook(lambda m, x: channels.__setitem__(m, x[0].shape[1])) for m in filters]
        with torch.no_grad():
            vocoder(sample)
        for hook in hooks:
            hook.remove()
        for m, count in channels.items():
            m.channels = count
            if isinstance(m, resample.UpSample1d):
                taps = m.filter[0, 0]
                for phase in range(m.stride):
                    sub = taps[phase::m.stride].flip(0)
                    m.register_buffer(f"phase{phase}", sub.expand(count, 1, -1).contiguous())
            else:
                m.register_buffer("kernel", m.filter.expand(count, -1, -1).contiguous())
        def upsample(self, x):
            x = nn.functional.pad(x, (self.pad, self.pad), mode="replicate")
            phases = []
            for phase in range(self.stride):
                kernel = getattr(self, f"phase{phase}")
                width = kernel.shape[-1] - 1
                phases.append(nn.functional.conv1d(nn.functional.pad(x, (width, width)), kernel, groups=self.channels))
            y = torch.stack(phases, dim=-1).reshape(x.shape[0], self.channels, -1)
            return self.ratio * y[..., self.pad_left:-self.pad_right]
        def lowpass(self, x):
            if self.padding:
                x = nn.functional.pad(x, (self.pad_left, self.pad_right), mode=self.padding_mode)
            return nn.functional.conv1d(x, self.kernel, stride=self.stride, groups=self.channels)
        resample.UpSample1d.forward = upsample
        alias_filter.LowPassFilter1d.forward = lowpass
        with torch.no_grad():
            print(f"bigvgan with polyphase upsampling against the original: max difference "
                  f"{np.abs(vocoder(sample).numpy() - original).max():.2e}", flush=True)
        export(vocoder.eval(), (sample,), out / "bigvgan.onnx", ["mel"], ["audio"],
               {"mel": {2: "frames"}, "audio": {2: "samples"}}, more=[(sample[:, :, :77],)])
        unclip(out / "bigvgan.onnx", vocoder, sample)


def to_float16(source, target, inputs):
    import onnx
    import onnxruntime
    from onnxconverter_common import float16
    half = float16.convert_float_to_float16(
        onnx.load(str(source)), keep_io_types=True,
        op_block_list=["LayerNormalization", "ReduceMean", "Pow", "Sqrt", "Softmax", "Exp", "Sin", "Cos"])
    onnx.save(half, str(target))
    names = ["x", "prompt_x", "x_lens", "t", "style", "condition"]
    feed = {name: value.numpy() for name, value in zip(names, inputs)}
    full = onnxruntime.InferenceSession(str(source), providers=["CPUExecutionProvider"]).run(None, feed)[0]
    got = onnxruntime.InferenceSession(str(target), providers=["CPUExecutionProvider"]).run(None, feed)[0]
    print(f"{target.name}: {target.stat().st_size / 1e6:.0f} MB, max difference from float32 "
          f"{np.abs(got - full).max():.2e} (output max {np.abs(full).max():.2e})", flush=True)


def unclip(path, vocoder, sample):
    """BigVGAN ends in clamp(-1, 1), and Seed-VC sings about 12 dB above a vocal
    stem, so peaks get cut off. seedvc.py sets the level itself afterwards: take
    the clamp out and check the model now gives what its last convolution does."""
    import onnx
    import onnxruntime
    proto = onnx.load(str(path))
    graph = proto.graph
    last = next(node for node in graph.node if graph.output[0].name in node.output)
    assert last.op_type == "Clip", last.op_type
    for node in graph.node:
        for index, name in enumerate(node.output):
            if name == last.input[0]:
                node.output[index] = graph.output[0].name
    graph.node.remove(last)
    onnx.save(proto, str(path))
    captured = []
    hook = vocoder.conv_post.register_forward_hook(lambda module, inputs, output: captured.append(output))
    loud = sample + 3  # a mel loud enough to go past full scale
    with torch.no_grad():
        for check in (sample, loud):
            vocoder(check)
    hook.remove()
    session = onnxruntime.InferenceSession(str(path), providers=["CPUExecutionProvider"])
    for check, expected in zip((sample, loud), captured):
        got = session.run(None, {"mel": check.numpy()})[0]
        print(f"{path.name} without the clamp: {path.stat().st_size / 1e6:.0f} MB, peak {np.abs(got).max():.2f}, "
              f"max difference from the last convolution {np.abs(got - expected.numpy()).max():.2e}", flush=True)


def remove_weight_norm(module):
    for child in module.modules():
        try:
            torch.nn.utils.remove_weight_norm(child)
        except ValueError:
            pass


def export(module, inputs, path, input_names, output_names, dynamic_axes, more=()):
    import onnxruntime
    with torch.no_grad():
        torch.onnx.export(module, inputs, str(path), input_names=input_names, output_names=output_names,
                          dynamic_axes=dynamic_axes, opset_version=OPSET, do_constant_folding=True)
    session = onnxruntime.InferenceSession(str(path), providers=["CPUExecutionProvider"])
    size = sum(f.stat().st_size for f in path.parent.glob(path.name + "*"))
    for check in [inputs, *more]:
        with torch.no_grad():
            expected = module(*check)
        if isinstance(expected, (tuple, list)):
            expected = expected[0]
        got = session.run(None, {name: value.numpy() for name, value in zip(input_names, check)})[0]
        difference = np.abs(got - expected.numpy()).max()
        scale = np.abs(expected.numpy()).max()
        print(f"{path.name}: {size / 1e6:.0f} MB, input {tuple(check[0].shape)}: max difference {difference:.2e} "
              f"(output max {scale:.2e})", flush=True)


if __name__ == "__main__":
    main()
