"""seedvc - sing or speak in another voice: Seed-VC's singing voice conversion
(v1, F0-conditioned, 44.1 kHz) on ONNX Runtime. GPL-3.0, like Seed-VC
(https://github.com/Plachtaa/seed-vc), whose inference.py this follows step by
step; export_onnx.py makes the models it runs.

    seedvc --models DIR --source S.f32 --source16k S16.f32
           --reference R.f32 --reference16k R16.f32 --output OUT.f32
           [--steps 25] [--cfg 0.7] [--pitch SEMITONES] [--auto-pitch]
           [--device auto|gpu|cpu] [--seed 0]

Audio is raw float32 mono: the song and the voice sample at 44.1 kHz and at
16 kHz (ffmpeg makes both); the output is 44.1 kHz. What the voice sings, its
melody and timing come from the source; how it sounds, from the reference.

stdout: "progress <0-100> <stage>" lines, then "device <name>" once and
"done <seconds>". Errors go to stderr with exit code 1.
"""

import argparse
import os
import sys
import time

import numpy as np

SAMPLE_RATE = 44100
HOP = 512
N_FFT = 2048
# What the DiT sees at once, the voice sample included. Seed-VC takes 30 s;
# 21 s keeps one step of the GPU under the 2 s after which Windows resets the
# driver, and with a 12 s sample costs least per second of song
CONTEXT_FRAMES = 1800
OVERLAP_FRAMES = 16
REFERENCE_SECONDS = 12
RMVPE_FRAMES = 2048    # pitch in pieces of 20 s, 1.28 s of context each side
RMVPE_CONTEXT = 128
VOCODER_FRAMES = 200   # BigVGAN in pieces, for the same reason; with 32 frames
VOCODER_CONTEXT = 32   # of context each side a piece is exactly the whole
F0_BINS = 256
F0_MEL_MIN = 1127 * np.log(1 + 50.0 / 700)
F0_MEL_MAX = 1127 * np.log(1 + 1100.0 / 700)


def report(percent, stage):
    print(f"progress {int(percent)} {stage}", flush=True)


# ---------------------------------------------------------------- features

def frames_of(signal, length, hop):
    count = 1 + (len(signal) - length) // hop
    stride = signal.strides[0]
    return np.lib.stride_tricks.as_strided(signal, (count, length), (hop * stride, stride))


def hann(length, periodic=True):
    n = length if periodic else length - 1
    return (0.5 - 0.5 * np.cos(2 * np.pi * np.arange(length) / n)).astype(np.float32)


def whisper_features(audio16k, filters):
    """WhisperFeatureExtractor: 30 s, 80 log-mel bins, (1, 80, 3000)."""
    samples = np.zeros(480000, np.float32)
    samples[:min(len(audio16k), 480000)] = audio16k[:480000]
    padded = np.pad(samples, 200, mode="reflect")
    spectrum = np.fft.rfft(frames_of(padded, 400, 160) * hann(400), n=400)
    power = (spectrum.real ** 2 + spectrum.imag ** 2)[:-1]          # (3000, 201)
    mel = np.log10(np.maximum(power @ filters, 1e-10))               # (3000, 80)
    mel = np.maximum(mel, mel.max() - 8.0)
    return ((mel + 4.0) / 4.0).T[None].astype(np.float32)


def kaldi_fbank(audio16k):
    """torchaudio.compliance.kaldi.fbank(num_mel_bins=80, dither=0) on 16 kHz."""
    frames = frames_of(audio16k.astype(np.float32), 400, 160).astype(np.float64)
    frames = frames - frames.mean(axis=1, keepdims=True)
    previous = np.concatenate([frames[:, :1], frames[:, :-1]], axis=1)
    frames = frames - 0.97 * previous
    frames = frames * (hann(400, periodic=False).astype(np.float64) ** 0.85)
    spectrum = np.fft.rfft(frames, n=512)
    power = spectrum.real ** 2 + spectrum.imag ** 2                   # (T, 257)

    def mel(f):
        return 1127.0 * np.log(1.0 + f / 700.0)
    low, high = mel(20.0), mel(8000.0)
    delta = (high - low) / 81
    bins = mel(16000.0 / 512 * np.arange(256))
    left = low + np.arange(80)[:, None] * delta
    center, right = left + delta, left + 2 * delta
    banks = np.maximum(0.0, np.minimum((bins - left) / (center - left), (right - bins) / (right - center)))
    banks = np.pad(banks, ((0, 0), (0, 1)))                           # (80, 257)
    energies = power @ banks.T
    eps = np.finfo(np.float32).eps
    return np.log(np.maximum(energies, eps)).astype(np.float32)


def rmvpe_mel(audio16k, basis):
    """RMVPE's log-mel: 1024-point STFT, hop 160, 128 HTK bins 30-8000 Hz."""
    padded = np.pad(audio16k.astype(np.float32), 512, mode="reflect")
    spectrum = np.fft.rfft(frames_of(padded, 1024, 160) * hann(1024), n=1024)
    magnitude = np.sqrt(spectrum.real ** 2 + spectrum.imag ** 2)      # (T, 513)
    return np.log(np.maximum(basis @ magnitude.T, 1e-5)).astype(np.float32)   # (128, T)


def mel44k(audio, basis):
    """Seed-VC's mel: reflect padding, 2048-point STFT, hop 512, 128 bins."""
    pad = (N_FFT - HOP) // 2
    padded = np.pad(audio.astype(np.float32), pad, mode="reflect")
    spectrum = np.fft.rfft(frames_of(padded, N_FFT, HOP) * hann(N_FFT), n=N_FFT)
    magnitude = np.sqrt(spectrum.real ** 2 + spectrum.imag ** 2 + 1e-9)
    return np.log(np.maximum(basis @ magnitude.T, 1e-5)).astype(np.float32)[None]   # (1, 128, T)


def stretch(length_in, length_out):
    """F.interpolate(mode='nearest') source indices."""
    if length_in == length_out:
        return np.arange(length_out)
    scale = np.float32(length_in) / np.float32(length_out)
    return np.minimum(np.floor(np.arange(length_out, dtype=np.float32) * scale).astype(np.int64), length_in - 1)


def f0_to_coarse(f0):
    f0_mel = 1127 * np.log(1 + f0 / 700)
    a = (F0_BINS - 2) / (F0_MEL_MAX - F0_MEL_MIN)
    b = F0_MEL_MIN * a - 1.0
    f0_mel = np.where(f0_mel > 0, f0_mel * a - b, f0_mel)
    coarse = np.round(f0_mel).astype(np.int64)
    coarse = coarse * (coarse > 0)
    coarse = coarse + (coarse < 1)
    coarse = coarse * (coarse < F0_BINS)
    coarse = coarse + (coarse >= F0_BINS) * (F0_BINS - 1)
    return np.clip(coarse, 0, F0_BINS - 1)


def voice_sample(audio, audio16k, seconds=REFERENCE_SECONDS, threshold_db=-40.0):
    """What the DiT is shown of the voice sample: from just before the voice
    starts to where it ends as recorded, with up to 1 s of what follows - or,
    past `seconds`, to the end of the last pause in its last 4 s within them.

    How the sample is cut moves the whole result in time. Cut in the middle of a
    word, or with the silence after the voice trimmed off, it makes the new voice
    come about 10 ms late against the source (Seed-VC in PyTorch too, and
    appending digital silence does not help); cut like this, it came 6 ms late
    on the samples tried, as Seed-VC with the sample as it is (README)."""
    frame = SAMPLE_RATE // 100
    count = len(audio) // frame
    limit = seconds * 100
    if count == 0:
        return audio, audio16k
    energy = np.sqrt(np.mean(audio[:count * frame].reshape(count, frame) ** 2, axis=1) + 1e-12)
    loud = 20 * np.log10(energy / (energy.max() + 1e-12)) > threshold_db
    if not loud.any():
        return audio[:limit * frame], audio16k[:limit * 160]
    voiced = np.nonzero(loud)[0]
    first = max(0, voiced[0] - 5)
    last = min(count, voiced[-1] + 100)
    if last - first > limit:
        start = first + max(0, limit - 400)
        quiet = np.concatenate([[0], (~loud[start:first + limit]).astype(np.int8), [0]])
        edges = np.diff(quiet)
        pauses = [end for begin, end in zip(np.nonzero(edges == 1)[0], np.nonzero(edges == -1)[0])
                  if end - begin >= 10]
        last = start + (pauses[-1] if pauses else int(np.argmin(energy[start:first + limit])) + 1)
    return audio[first * frame:last * frame], audio16k[first * 160:last * 160]


def loudness(audio):
    """Integrated loudness as ITU-R BS.1770 / EBU R 128 measure it, as a power:
    the 400 ms blocks (75 % overlap) through the K-weighting - applied to each
    block's spectrum, the 48 kHz filters' magnitude at the same frequencies -
    gated at -70 LUFS and then 10 LU under the mean of the rest."""
    block, hop = SAMPLE_RATE * 4 // 10, SAMPLE_RATE // 10
    audio = np.ascontiguousarray(audio, dtype=np.float32)
    if len(audio) < block:
        audio = np.pad(audio, (0, block - len(audio)))
    z = np.exp(-2j * np.pi * np.fft.rfftfreq(block, 1 / SAMPLE_RATE) / 48000)
    def biquad(b, a):
        return (b[0] + b[1] * z + b[2] * z * z) / (a[0] + a[1] * z + a[2] * z * z)
    weight = np.abs(biquad((1.53512485958697, -2.69169618940638, 1.19839281085285),
                           (1.0, -1.69065929318241, 0.73248077421585)) *
                    biquad((1.0, -2.0, 1.0), (1.0, -1.99004745483398, 0.99007225036621))) ** 2
    weight[1:-1] *= 2  # the half of the spectrum rfft leaves out (Parseval)
    blocks = frames_of(audio, block, hop)
    power = np.concatenate([np.abs(np.fft.rfft(blocks[start:start + 256], axis=1)) ** 2 @ weight
                            for start in range(0, len(blocks), 256)]) / block ** 2
    power = power[power > 10 ** ((-70 + 0.691) / 10)]
    if len(power) == 0:
        return 0.0
    return power[power > power.mean() / 10].mean()


def torch_median(values):
    return np.sort(values)[(len(values) - 1) // 2]


# ---------------------------------------------------------------- models

class Engine:
    def __init__(self, folder, device):
        import onnxruntime
        self.ort = onnxruntime
        self.folder = folder
        self.filters = np.load(os.path.join(folder, "filters.npz"))
        available = onnxruntime.get_available_providers()
        self.gpu = device != "cpu" and "DmlExecutionProvider" in available
        if device == "gpu" and not self.gpu:
            raise RuntimeError("no DirectML device")
        self.sessions = {}
        self.device = "cpu"

    def session(self, name, cpu=False):
        if name in self.sessions and not cpu:
            return self.sessions[name]
        path = os.path.join(self.folder, name + ".onnx")
        options = self.ort.SessionOptions()
        options.log_severity_level = 3
        session = None
        if self.gpu and not cpu:
            options.enable_mem_pattern = False
            options.execution_mode = self.ort.ExecutionMode.ORT_SEQUENTIAL
            try:
                session = self.ort.InferenceSession(path, options, providers=["DmlExecutionProvider", "CPUExecutionProvider"])
                self.device = "gpu"
            except Exception:
                session = None
        if session is None:
            options = self.ort.SessionOptions()
            options.log_severity_level = 3
            session = self.ort.InferenceSession(path, options, providers=["CPUExecutionProvider"])
        self.sessions[name] = session
        return session

    def run(self, name, **inputs):
        try:
            return self.session(name).run(None, inputs)[0]
        except Exception as error:
            if not self.gpu:
                raise
            # the graphics card failed (a driver reset, out of memory): go on with the CPU
            print(f"gpu failed in {name}, cpu from now on: {str(error).strip().splitlines()[-1][:300]}", flush=True)
            self.gpu = False
            self.sessions = {}
            self.device = "cpu"
            return self.session(name, cpu=True).run(None, inputs)[0]

    def release(self, *names):
        """Models done with leave the graphics card's memory: on a 4 GB card two
        of them at once already slow everything down"""
        for name in names:
            self.sessions.pop(name, None)

    def vocode(self, mel):
        frames = mel.shape[2]
        pieces = []
        for start in range(0, frames, VOCODER_FRAMES):
            left = max(0, start - VOCODER_CONTEXT)
            right = min(frames, start + VOCODER_FRAMES + VOCODER_CONTEXT)
            wave = self.run("bigvgan", mel=np.ascontiguousarray(mel[:, :, left:right]))[0, 0]
            pieces.append(wave[(start - left) * HOP:(start - left + min(VOCODER_FRAMES, frames - start)) * HOP])
        return np.concatenate(pieces)

    # what the voice says: Whisper's encoder, 30 s at a time with 5 s overlap
    def content(self, audio16k):
        def encode(chunk):
            features = whisper_features(chunk, self.filters["whisper"])
            return self.run("whisper_encoder", features=features)[:, :len(chunk) // 320 + 1]
        if len(audio16k) <= 16000 * 30:
            return encode(audio16k)
        overlap = 5
        parts, buffer, done = [], None, 0
        while done < len(audio16k):
            if buffer is None:
                chunk = audio16k[done:done + 16000 * 30]
            else:
                chunk = np.concatenate([buffer, audio16k[done:done + 16000 * (30 - overlap)]])
            encoded = encode(chunk)
            parts.append(encoded if done == 0 else encoded[:, 50 * overlap:])
            buffer = chunk[-16000 * overlap:]
            done += 30 * 16000 if done == 0 else len(chunk) - 16000 * overlap
        return np.concatenate(parts, axis=1)

    # the melody: RMVPE, 100 frames a second, 0 where unvoiced. A whole song at
    # once does not fit a 4 GB graphics card: pieces of 20 s with 1.28 s of
    # context each side
    def f0(self, audio16k, threshold=0.03, piece=RMVPE_FRAMES, context=RMVPE_CONTEXT):
        mel = rmvpe_mel(audio16k, self.filters["rmvpe"])
        frames = mel.shape[1]
        parts = []
        for start in range(0, frames, piece):
            left = max(0, start - context)
            right = min(frames, start + piece + context)
            part = mel[:, left:right]
            width = part.shape[1]
            part = np.pad(part, ((0, 0), (0, 32 * ((width - 1) // 32 + 1) - width)))
            hidden = self.run("rmvpe", mel=part[None])[0, :width]
            parts.append(hidden[start - left:start - left + min(piece, frames - start)])
        salience = np.concatenate(parts)
        center = np.argmax(salience, axis=1) + 4
        salience = np.pad(salience, ((0, 0), (4, 4)))
        cents_mapping = np.pad(20 * np.arange(360) + 1997.3794084376191, (4, 4))
        window = center[:, None] + np.arange(-4, 5)[None]
        weights = np.take_along_axis(salience, window, axis=1)
        cents = (weights * cents_mapping[window]).sum(1) / weights.sum(1)
        cents[salience.max(axis=1) <= threshold] = 0
        f0 = 10 * 2 ** (cents / 1200)
        f0[f0 == 10] = 0
        return f0

    def style(self, audio16k):
        fbank = kaldi_fbank(audio16k)
        fbank = fbank - fbank.mean(axis=0, keepdims=True)
        return self.run("campplus", fbank=fbank[None])

    def condition(self, content, f0, frames):
        content = content[:, stretch(content.shape[1], frames)]
        bins = f0_to_coarse(f0)[stretch(len(f0), frames)][None]
        return self.run("length_regulator", content=content.astype(np.float32), f0_bins=bins)

    def generate(self, condition, prompt_mel, style, steps, cfg, rng, step_done):
        """The flow-matching ODE, Euler steps with classifier-free guidance."""
        frames = condition.shape[1]
        prompt_frames = prompt_mel.shape[2]
        x = rng.standard_normal((1, 128, frames)).astype(np.float32)
        prompt_x = np.zeros_like(x)
        prompt_x[..., :prompt_frames] = prompt_mel
        x[..., :prompt_frames] = 0
        zeros_style, zeros_condition = np.zeros_like(style), np.zeros_like(condition)
        lengths = np.array([frames, frames], np.int64)
        t_span = np.linspace(0, 1, steps + 1, dtype=np.float32)
        t = t_span[0]
        for step in range(1, steps + 1):
            dt = t_span[step] - t_span[step - 1]
            velocity = self.run("dit_fp16",
                                x=np.concatenate([x, x]),
                                prompt_x=np.concatenate([prompt_x, np.zeros_like(prompt_x)]),
                                x_lens=lengths,
                                t=np.array([t, t], np.float32),
                                style=np.concatenate([style, zeros_style]),
                                condition=np.concatenate([condition, zeros_condition]))
            velocity = (1.0 + cfg) * velocity[:1] - cfg * velocity[1:]
            x = x + dt * velocity
            t = t + dt
            x[..., :prompt_frames] = 0
            step_done()
        return x


def crossfade(previous, chunk, overlap):
    fade_out = np.cos(np.linspace(0, np.pi / 2, overlap)) ** 2
    fade_in = np.cos(np.linspace(np.pi / 2, 0, overlap)) ** 2
    chunk = chunk.copy()
    if len(chunk) < overlap:
        chunk[:overlap] = chunk[:overlap] * fade_in[:len(chunk)] + (previous[-overlap:] * fade_out)[:len(chunk)]
    else:
        chunk[:overlap] = chunk[:overlap] * fade_in + previous[-overlap:] * fade_out
    return chunk


def convert(args):
    started = time.time()
    source = np.fromfile(args.source, dtype="<f4")
    source16k = np.fromfile(args.source16k, dtype="<f4")
    reference, reference16k = voice_sample(np.fromfile(args.reference, dtype="<f4"),
                                           np.fromfile(args.reference16k, dtype="<f4"))
    if len(source) < HOP * 4 or len(source16k) < 1600:
        raise RuntimeError("the song is too short")
    if len(reference) < SAMPLE_RATE or len(reference16k) < 16000:
        raise RuntimeError("the voice sample is too short: at least 1 second")

    engine = Engine(args.models, args.device)
    report(0, "voice")
    style = engine.style(reference16k)
    prompt_mel = mel44k(reference, engine.filters["mel44k"])
    prompt_content = engine.content(reference16k)
    prompt_f0 = engine.f0(reference16k)
    print(f"device {engine.device}", flush=True)
    report(3, "song")
    content = engine.content(source16k)
    report(6, "song")
    f0 = engine.f0(source16k)
    report(9, "song")
    source_frames = mel44k(source, engine.filters["mel44k"]).shape[2]

    # the melody stays in its key unless asked: --auto-pitch moves it to the voice's
    # median, --pitch shifts it by semitones
    voiced = f0 > 1
    log_f0 = np.log(f0 + 1e-5)
    if args.auto_pitch and voiced.any() and (prompt_f0 > 1).any():
        log_f0[voiced] = log_f0[voiced] - torch_median(np.log(f0[voiced] + 1e-5)) + \
            torch_median(np.log(prompt_f0[prompt_f0 > 1] + 1e-5))
    shifted_f0 = np.exp(log_f0)
    if args.pitch:
        shifted_f0[voiced] = shifted_f0[voiced] * 2 ** (args.pitch / 12)

    condition = engine.condition(content, shifted_f0, source_frames)
    prompt_condition = engine.condition(prompt_content, prompt_f0, prompt_mel.shape[2])
    engine.release("whisper_encoder", "rmvpe", "campplus", "length_regulator")
    report(10, "song")

    window = CONTEXT_FRAMES - prompt_mel.shape[2]
    overlap_wave = OVERLAP_FRAMES * HOP
    chunks = 1
    while (chunks * (window - OVERLAP_FRAMES) + OVERLAP_FRAMES) < condition.shape[1]:
        chunks += 1
    total_steps = chunks * args.steps
    done_steps = [0]

    def step_done():
        done_steps[0] += 1
        report(10 + 80 * done_steps[0] / total_steps, "voice-over")

    # every piece's mel first, then the vocoder, so that the two never share the card
    rng = np.random.default_rng(args.seed)
    mels, processed = [], 0
    while processed < condition.shape[1]:
        chunk = condition[:, processed:processed + window]
        mel = engine.generate(np.concatenate([prompt_condition, chunk], axis=1), prompt_mel, style,
                              args.steps, args.cfg, rng, step_done)[:, :, prompt_mel.shape[2]:]
        mels.append(mel)
        if processed + window >= condition.shape[1]:
            break
        processed += mel.shape[2] - OVERLAP_FRAMES
    engine.release("dit_fp16")

    pieces, previous = [], None
    for index, mel in enumerate(mels):
        last = index == len(mels) - 1
        wave = engine.vocode(mel)
        report(90 + 10 * (index + 1) / len(mels), "voice-over")
        if previous is None:
            if last:
                pieces.append(wave)
                break
            pieces.append(wave[:-overlap_wave])
        elif last:
            pieces.append(crossfade(previous, wave, overlap_wave))
            break
        else:
            pieces.append(crossfade(previous, wave[:-overlap_wave], overlap_wave))
        previous = wave[-overlap_wave:]
    output = np.concatenate(pieces)

    # Seed-VC sings at a level of its own, on "Кузнец" 12 dB above the vocal
    # stem and past full scale: the new voice goes in at the level of the old
    # one, so it sits in the mix where that was, its peaks under full scale
    target, level, peak = loudness(source), loudness(output), np.abs(output).max()
    if target > 0 and level > 0 and peak > 0:
        output = output * min(np.sqrt(target / level), 0.98 / peak)
    output.astype("<f4").tofile(args.output)
    report(100, "done")
    print(f"done {time.time() - started:.1f}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--models", required=True)
    parser.add_argument("--source", required=True)
    parser.add_argument("--source16k", required=True)
    parser.add_argument("--reference", required=True)
    parser.add_argument("--reference16k", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--steps", type=int, default=25)
    parser.add_argument("--cfg", type=float, default=0.7)
    parser.add_argument("--pitch", type=float, default=0)
    parser.add_argument("--auto-pitch", action="store_true")
    parser.add_argument("--device", choices=["auto", "gpu", "cpu"], default="auto")
    parser.add_argument("--seed", type=int, default=0)
    args = parser.parse_args()
    try:
        convert(args)
    except Exception as error:  # the plugin shows the message
        print(f"error: {error}", file=sys.stderr, flush=True)
        sys.exit(1)


if __name__ == "__main__":
    main()
