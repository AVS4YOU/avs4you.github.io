// Demucs over one span of audio: cut into 7.8 s segments with 25 % overlap,
// each run through the ONNX model, cross-faded back together.
//
// Adapted from demucs.onnx's src/model_apply.cpp (MIT, Sevag Hanssian), with
// the random time shift drawn from the caller's generator: the original's is
// one global, which several threads separating at once would race on.

#include "separate.h"

#include <algorithm>
#include <cmath>

namespace {
	void ReflectPadding(Eigen::MatrixXf& padded, int left, int right, int length) {
		for (int i = 0; i < left; ++i)
			padded.block(0, left - 1 - i, 2, 1) = padded.block(0, left + i, 2, 1);
		const int last = length + left - 1;
		for (int i = 0; i < right; ++i)
			padded.block(0, last + i + 1, 2, 1) = padded.block(0, last - i, 2, 1);
	}
} // namespace

Eigen::Tensor3dXf SeparateSpan(demucsonnx::demucs_model& model, const Eigen::MatrixXf& audio,
	std::mt19937& random, const std::function<void(float)>& progress) {
	using namespace demucsonnx;
	const int length = static_cast<int>(audio.cols());
	const int maxShift = static_cast<int>(MAX_SHIFT_SECS * SUPPORTED_SAMPLE_RATE);

	// normalize like Demucs does, by the mean and spread of the mono mix
	const Eigen::VectorXf mono = audio.colwise().mean();
	const float mean = mono.mean();
	float spread = std::sqrt((mono.array() - mean).square().sum() / (std::max)(1, (int)mono.size() - 1));
	if (spread < 1e-8f)
		spread = 1.0f;  // silence: nothing to scale

	Eigen::MatrixXf padded = Eigen::MatrixXf::Zero(2, length + 2 * maxShift);
	padded.block(0, maxShift, 2, length) = (audio.array() - mean) / spread;

	// a random shift of up to 0.5 s, as Demucs uses for time invariance
	const int shift = std::uniform_int_distribution<>(0, maxShift - 1)(random);
	const int shiftedLength = length + maxShift - shift;
	const Eigen::Ref<const Eigen::MatrixXf> shifted = padded.block(0, shift, 2, shiftedLength);

	const int segmentSamples = static_cast<int>(SEGMENT_LEN_SECS * SUPPORTED_SAMPLE_RATE);
	const int sources = model.nb_sources;
	demucs_segment_buffers buffers(2, segmentSamples, sources);
	stft_buffers stft(buffers.padded_segment_samples);
	const int stride = static_cast<int>((1.0f - OVERLAP) * segmentSamples);

	Eigen::Tensor3dXf out(sources, 2, shiftedLength);
	out.setZero();
	// triangular cross-fade weights
	Eigen::VectorXf weight(segmentSamples);
	const int half = segmentSamples / 2;
	weight.head(half) = Eigen::VectorXf::LinSpaced(half, 1, static_cast<float>(half));
	weight.tail(half) = weight.head(half).reverse();
	if (segmentSamples % 2)
		weight(half) = static_cast<float>(half);
	weight /= weight.maxCoeff();
	weight = weight.array().pow(TRANSITION_POWER);
	Eigen::VectorXf weightSum = Eigen::VectorXf::Zero(shiftedLength);

	const int chunks = static_cast<int>(std::ceil(static_cast<float>(shiftedLength) / stride));
	int done = 0;
	Eigen::Tensor3dXf chunkOut(sources, 2, segmentSamples);
	for (int offset = 0; offset < shiftedLength; offset += stride) {
		const int chunkLength = (std::min)(segmentSamples, shiftedLength - offset);
		const int symmetric = buffers.padded_segment_samples - chunkLength;
		const int start = symmetric / 2;
		buffers.padded_mix.setZero();
		buffers.padded_mix.block(0, buffers.pad + start, 2, chunkLength) =
			shifted.block(0, offset, 2, chunkLength);
		ReflectPadding(buffers.padded_mix, buffers.pad, buffers.pad_end, segmentSamples);

		model_inference(model, buffers, stft);

		chunkOut.setZero();
		const int last = static_cast<int>(buffers.targets_out.dimension(2)) - 1;
		for (int source = 0; source < sources; ++source)
			for (int channel = 0; channel < 2; ++channel)
				for (int k = 0; k < chunkLength; ++k)
					chunkOut(source, channel, k) = buffers.targets_out(source, channel, (std::min)(k + start, last));
		for (int source = 0; source < sources; ++source)
			for (int channel = 0; channel < 2; ++channel) {
				Eigen::Map<Eigen::ArrayXf> slice(&out(source, channel, offset), chunkLength);
				Eigen::Map<const Eigen::ArrayXf> chunk(&chunkOut(source, channel, 0), chunkLength);
				slice += weight.head(chunkLength).array() * chunk;
			}
		weightSum.segment(offset, chunkLength) += weight.head(chunkLength);
		progress(static_cast<float>(++done) / chunks);
	}

	for (int source = 0; source < sources; ++source)
		for (int channel = 0; channel < 2; ++channel) {
			Eigen::Map<Eigen::ArrayXf> slice(&out(source, channel, 0), shiftedLength);
			slice = slice / weightSum.array() * spread + mean;
		}

	const Eigen::array<Eigen::Index, 3> offsets = { 0, 0, maxShift - shift };
	const Eigen::array<Eigen::Index, 3> extents = { sources, 2, length };
	return out.slice(offsets, extents).eval();
}
