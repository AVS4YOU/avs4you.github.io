#pragma once
#include "demucs.hpp"

#include <functional>
#include <random>

// Separates one span of 44.1 kHz stereo audio into the model's sources:
// (sources, 2, samples). progress gets 0-1. Safe to call from several threads
// on one model, each with its own random generator.
Eigen::Tensor3dXf SeparateSpan(demucsonnx::demucs_model& model, const Eigen::MatrixXf& audio,
	std::mt19937& random, const std::function<void(float)>& progress);
