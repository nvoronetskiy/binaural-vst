#pragma once
#include "PluginProcessor.h"
#include "PluginEditor.h"


HrtfBiAuralAudioProcessor::HrtfBiAuralAudioProcessor()
	:
	currentHrir_(),
	crossfadeRate(0.2f),
	panAmount_(1.f),
	gain_(1.f),
	crossfading_(true),
	bypassed_(false)
{
	crossover_.set(44100, crossover_.f0);
}

HrtfBiAuralAudioProcessor::~HrtfBiAuralAudioProcessor()
{
}
const String HrtfBiAuralAudioProcessor::getName() const
{
	return JucePlugin_Name;
}

int HrtfBiAuralAudioProcessor::getNumParameters()
{
	return 0;
}

float HrtfBiAuralAudioProcessor::getParameter(int)
{
	return 0.0f;
}

void HrtfBiAuralAudioProcessor::setParameter(int, float)
{
}

const String HrtfBiAuralAudioProcessor::getParameterName(int)
{
	return String();
}

const String HrtfBiAuralAudioProcessor::getParameterText(int)
{
	return String();
}

const String HrtfBiAuralAudioProcessor::getInputChannelName(int channelIndex) const
{
	return String(channelIndex + 1);
}

const String HrtfBiAuralAudioProcessor::getOutputChannelName(int channelIndex) const
{
	return String(channelIndex + 1);
}

bool HrtfBiAuralAudioProcessor::isInputChannelStereoPair(int) const
{
	return true;
}

bool HrtfBiAuralAudioProcessor::isOutputChannelStereoPair(int) const
{
	return true;
}

bool HrtfBiAuralAudioProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
	return true;
#else
	return false;
#endif
}

bool HrtfBiAuralAudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
	return true;
#else
	return false;
#endif
}

bool HrtfBiAuralAudioProcessor::silenceInProducesSilenceOut() const
{
	return false;
}

double HrtfBiAuralAudioProcessor::getTailLengthSeconds() const
{
	return 0.0;
}

int HrtfBiAuralAudioProcessor::getNumPrograms()
{
	return 1;
}

int HrtfBiAuralAudioProcessor::getCurrentProgram()
{
	return 0;
}

void HrtfBiAuralAudioProcessor::setCurrentProgram(int)
{
}

const String HrtfBiAuralAudioProcessor::getProgramName(int)
{
	return String();
}

void HrtfBiAuralAudioProcessor::changeProgramName(int, const String&)
{
}

void HrtfBiAuralAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
	crossover_.set(sampleRate, crossover_.f0);
	loPassIn_.resize(samplesPerBlock);
	hiPassIn_.resize(samplesPerBlock);
	buffers_[0].resize(samplesPerBlock);
	buffers_[1].resize(samplesPerBlock);
	filters_[0].init(samplesPerBlock, HRIR_LENGTH);
	filters_[1].init(samplesPerBlock, HRIR_LENGTH);
}

void HrtfBiAuralAudioProcessor::releaseResources()
{
}

void HrtfBiAuralAudioProcessor::processBlock(AudioSampleBuffer& buffer, MidiBuffer&)
{
	// get a pointer to the left channel data
	auto in = buffer.getWritePointer(0);
	auto bufferLength = buffer.getNumSamples();

	if (bypassed_)
	{
		// just copy input from left channel to right
		buffer.copyFrom(1, 0, in, bufferLength);
		return;
	}

	if (crossfading_)
	{
		SpinLock::ScopedLockType lock(processLock_);

		auto& targetHrir = hrtfContainer_.hrir();
		float diff[2], totalDiff = 0.f;
		// linear interpolation, sample by sample
		for (int i = 0; i < targetHrir[0].size(); ++i)
		{
			diff[0] = targetHrir[0][i] - currentHrir_[0][i];
			diff[1] = targetHrir[1][i] - currentHrir_[1][i];
			currentHrir_[0][i] += diff[0] * crossfadeRate;
			currentHrir_[1][i] += diff[1] * crossfadeRate;

			totalDiff += std::fabsf(diff[0]) + std::fabsf(diff[1]);
		}

		// update impule response
		filters_[0].setImpulseResponse(currentHrir_[0].data());
		filters_[1].setImpulseResponse(currentHrir_[1].data());

		if (totalDiff < 1.f)
			crossfading_ = false;
	}

	// split the input signal into two bands, only freqs above crossover's f0
	// will be spatialized
	memcpy(loPassIn_.data(), in, bufferLength * sizeof(float));
	memcpy(hiPassIn_.data(), in, bufferLength * sizeof(float));
	crossover_.loPass.processSamples(loPassIn_.data(), bufferLength);
	crossover_.hiPass.processSamples(hiPassIn_.data(), bufferLength);

	// we need to copy the hi-pass input to buffers
	memcpy(buffers_[0].data(), hiPassIn_.data(), bufferLength * sizeof(float));
	memcpy(buffers_[1].data(), hiPassIn_.data(), bufferLength * sizeof(float));

	// actual hrir filtering
	filters_[0].process(buffers_[0].data());
	filters_[1].process(buffers_[1].data());

	// copy to output
	auto outL = in;
	auto outR = buffer.getWritePointer(1);
	float dryAmount = 1 - panAmount_;
	float dry;
	for (int i = 0; i < bufferLength; i++)
	{
		dry = in[i];
		outL[i] = panAmount_ * (loPassIn_[i] + buffers_[0][i]) + dryAmount * dry;
		outR[i] = panAmount_ * (loPassIn_[i] + buffers_[1][i]) + dryAmount * dry;
	}
	buffer.applyGain(gain_);
}

bool HrtfBiAuralAudioProcessor::hasEditor() const
{
	return true;
}

AudioProcessorEditor* HrtfBiAuralAudioProcessor::createEditor()
{
	return new HrtfBiAuralAudioProcessorEditor(*this);
}

void HrtfBiAuralAudioProcessor::getStateInformation(MemoryBlock&)
{
}

void HrtfBiAuralAudioProcessor::setStateInformation(const void*, int)
{
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
	return new HrtfBiAuralAudioProcessor();
}

void HrtfBiAuralAudioProcessor::updateHRTF(double azimuth, double elevation)
{
	hrtfContainer_.updateHRIR(azimuth, elevation);
	crossfading_ = true;
}

void HrtfBiAuralAudioProcessor::toggleBypass(bool bypass)
{
	bypassed_ = bypass;
	reset();
}

void HrtfBiAuralAudioProcessor::reset()
{
	crossover_.loPass.reset();
	crossover_.hiPass.reset();
	filters_[0].reset();
	filters_[1].reset();
	crossfading_ = true;
}
