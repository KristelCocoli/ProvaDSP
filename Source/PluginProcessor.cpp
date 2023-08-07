/*
  ==============================================================================
    This file contains the basic framework code for a JUCE plugin processor.
  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

ProvaDSPAudioProcessor::ProvaDSPAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
{
}

ProvaDSPAudioProcessor::~ProvaDSPAudioProcessor()
{
}


const juce::String ProvaDSPAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool ProvaDSPAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool ProvaDSPAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool ProvaDSPAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double ProvaDSPAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int ProvaDSPAudioProcessor::getNumPrograms()
{
    return 1;
}

int ProvaDSPAudioProcessor::getCurrentProgram()
{
    return 0;
}

void ProvaDSPAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String ProvaDSPAudioProcessor::getProgramName (int index)
{
    return {};
}

void ProvaDSPAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

void ProvaDSPAudioProcessor::prepareToPlay (double SamplingFrequency, int NumberOfSamplesPerPacket)
{
    juce::dsp::ProcessSpec Specifications;
    
    Specifications.maximumBlockSize = NumberOfSamplesPerPacket;
    
    Specifications.numChannels = 1;
    
    Specifications.sampleRate = SamplingFrequency;
    
    leftChain.prepare(Specifications);
    rightChain.prepare(Specifications);
    
    updateFilters();
    
    leftChannelFifo.prepare(NumberOfSamplesPerPacket);
    rightChannelFifo.prepare(NumberOfSamplesPerPacket);
    
    osc.initialise([](float x) { return std::sin(x); });
    
    Specifications.numChannels = getTotalNumOutputChannels();
    osc.prepare(Specifications);
    osc.setFrequency(440);
}

void ProvaDSPAudioProcessor::releaseResources()
{

}

#ifndef JucePlugin_PreferredChannelConfigurations
bool ProvaDSPAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else

    if (//layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()
        layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void ProvaDSPAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto InputChannels  = getTotalNumInputChannels();
    auto OutputChannels = getTotalNumOutputChannels();
    for (auto i = InputChannels; i < OutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    updateFilters();
    
    juce::dsp::AudioBlock<float> block(buffer);
    
    auto LeftBlock = block.getSingleChannelBlock(0);
    auto RightBlock = block.getSingleChannelBlock(1);
    
    juce::dsp::ProcessContextReplacing<float> leftContext(LeftBlock);
    juce::dsp::ProcessContextReplacing<float> rightContext(RightBlock);
    
    leftChain.process(leftContext);
    rightChain.process(rightContext);
    
    leftChannelFifo.update(buffer);
    rightChannelFifo.update(buffer);
    
}


bool ProvaDSPAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* ProvaDSPAudioProcessor::createEditor()
{
    return new ProvaDSPAudioProcessorEditor (*this);

}

void ProvaDSPAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream mos(destData, true);
    apvts.state.writeToStream(mos);
}

void ProvaDSPAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto PluginTree = juce::ValueTree::readFromData(data, sizeInBytes);
    if( PluginTree.isValid() )
    {
        apvts.replaceState(PluginTree);
        updateFilters();
    }
}

ChainSettings getChainSettings(juce::AudioProcessorValueTreeState& apvts)
{
    ChainSettings Configurations;
    
    Configurations.lowCutFreq = apvts.getRawParameterValue("LowCut Freq")->load();
    Configurations.highCutFreq = apvts.getRawParameterValue("HighCut Freq")->load();
    Configurations.peakFreq = apvts.getRawParameterValue("Peak Freq")->load();
    Configurations.peakGainInDecibels = apvts.getRawParameterValue("Peak Gain")->load();
    Configurations.peakQuality = apvts.getRawParameterValue("Peak Quality")->load();
    Configurations.lowCutSlope = static_cast<Slope>(apvts.getRawParameterValue("LowCut Slope")->load());
    Configurations.highCutSlope = static_cast<Slope>(apvts.getRawParameterValue("HighCut Slope")->load());
    
    Configurations.lowCutBypassed = apvts.getRawParameterValue("LowCut Bypassed")->load() > 0.5f;
    Configurations.peakBypassed = apvts.getRawParameterValue("Peak Bypassed")->load() > 0.5f;
    Configurations.highCutBypassed = apvts.getRawParameterValue("HighCut Bypassed")->load() > 0.5f;
    
    return Configurations;
}

Coefficients makePeakFilter(const ChainSettings& chainSettings, double sampleRate)
{
    return juce::dsp::IIR::Coefficients<float>::makePeakFilter(sampleRate, chainSettings.peakFreq, chainSettings.peakQuality, juce::Decibels::decibelsToGain(chainSettings.peakGainInDecibels));
}

void ProvaDSPAudioProcessor::updatePeakFilter(const ChainSettings &chainSettings)
{
    auto PeakFrequencyCoefficients = makePeakFilter(chainSettings, getSampleRate());
    
    leftChain.setBypassed<ChainPositions::Peak>(chainSettings.peakBypassed);
    rightChain.setBypassed<ChainPositions::Peak>(chainSettings.peakBypassed);
    
    updateCoefficients(leftChain.get<ChainPositions::Peak>().coefficients, PeakFrequencyCoefficients);
    updateCoefficients(rightChain.get<ChainPositions::Peak>().coefficients, PeakFrequencyCoefficients);
}

void updateCoefficients(Coefficients &old, const Coefficients &replacements)
{
    *old = *replacements;
}

void ProvaDSPAudioProcessor::updateLowCutFilters(const ChainSettings &chainSettings)
{
    auto CutoffCoefficients = makeLowCutFilter(chainSettings, getSampleRate());
    auto& LeftChannelLowCutoffFrequency = leftChain.get<ChainPositions::LowCut>();
    auto& RightChannelLowCutoffFrequency = rightChain.get<ChainPositions::LowCut>();
    
    leftChain.setBypassed<ChainPositions::LowCut>(chainSettings.lowCutBypassed);
    rightChain.setBypassed<ChainPositions::LowCut>(chainSettings.lowCutBypassed);
    
    updateCutFilter(RightChannelLowCutoffFrequency, CutoffCoefficients, chainSettings.lowCutSlope);
    updateCutFilter(LeftChannelLowCutoffFrequency, CutoffCoefficients, chainSettings.lowCutSlope);
}

void ProvaDSPAudioProcessor::updateHighCutFilters(const ChainSettings &chainSettings)
{
    auto highCutCoefficients = makeHighCutFilter(chainSettings, getSampleRate());
    
    auto& HightCutoofLeftChannel = leftChain.get<ChainPositions::HighCut>();
    auto& RightCutoffRightChannel = rightChain.get<ChainPositions::HighCut>();
    
    leftChain.setBypassed<ChainPositions::HighCut>(chainSettings.highCutBypassed);
    rightChain.setBypassed<ChainPositions::HighCut>(chainSettings.highCutBypassed);
    
    updateCutFilter(HightCutoofLeftChannel, highCutCoefficients, chainSettings.highCutSlope);
    updateCutFilter(RightCutoffRightChannel, highCutCoefficients, chainSettings.highCutSlope);
}

void ProvaDSPAudioProcessor::updateFilters()
{
    auto Settings = getChainSettings(apvts);
    
    updateLowCutFilters(Settings);
    updatePeakFilter(Settings);
    updateHighCutFilters(Settings);
}

juce::AudioProcessorValueTreeState::ParameterLayout ProvaDSPAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout PluginLayout;
    
    
    PluginLayout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID {"LowCut Freq", 1}, "LowCut Freq", juce::NormalisableRange<float>(20.f, 20000.f, 1.f, 0.25f), 20.f));
        
    PluginLayout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID {"HighCut Freq", 1}, "HighCut Freq", juce::NormalisableRange<float>(20.f, 20000.f, 1.f, 0.25f), 20000.f));
        
    PluginLayout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID {"Peak Freq", 1}, "Peak Freq", juce::NormalisableRange<float>(20.f, 20000.f, 1.f, 0.25f), 750.f));
        
    PluginLayout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID {"Peak Gain", 1}, "Peak Gain", juce::NormalisableRange<float>(-24.f, 24.f, 0.5f, 1.f), 0.0f));
        
    PluginLayout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID {"Peak Quality", 1}, "Peak Quality", juce::NormalisableRange<float>(0.1f, 10.f, 0.05f, 1.f), 1.f));
        
    juce::StringArray stringArray;
    for( int j = 0; j < 4; ++j )
    {
        juce::String value;
        value << (12 + j*12);
        value << " db/Oct";
        stringArray.add(value);
    }
        
    PluginLayout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID {"LowCut Slope", 1}, "LowCut Slope", stringArray, 0));
    PluginLayout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID {"HighCut Slope", 1}, "HighCut Slope", stringArray, 0));
        
    PluginLayout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID {"LowCut Bypassed", 1}, "LowCut Bypassed", false));
    PluginLayout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID {"Peak Bypassed", 1}, "Peak Bypassed", false));
    PluginLayout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID {"HighCut Bypassed", 1}, "HighCut Bypassed", false));
    PluginLayout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID {"Analyzer Enabled", 1}, "Analyzer Enabled", true));
        
        return PluginLayout;
    }

    juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
    {
        return new ProvaDSPAudioProcessor();
    }
