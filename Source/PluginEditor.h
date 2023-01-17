/*
  ==============================================================================
    This file contains the basic framework code for a JUCE plugin editor.
  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

enum FFTOrder
{
    order2048 = 11,
    order4096 = 12,
    order8192 = 13
};

template<typename BlockType>
struct SpectrumAnalyzer
{
    void ApplyFftAnalysis(const juce::AudioBuffer<float>& audioData, const float negativeInfinity)
    {
        const auto SpectrumSize = GetSpectrumSize();
        
        ProcessedData.assign(ProcessedData.size(), 0);
        auto* readIndex = audioData.getReadPointer(0);
        std::copy(readIndex, readIndex + SpectrumSize, ProcessedData.begin());
        Boundary->multiplyWithWindowingTable (ProcessedData.data(), SpectrumSize);
        Fft->performFrequencyOnlyForwardTransform (ProcessedData.data());
        
        int TotalBins = (int)SpectrumSize / 2;
        
        for( int index = 0; index < TotalBins; ++index )
        {
            auto v = ProcessedData[index];
            if( !std::isinf(v) && !std::isnan(v) ) v /= float(TotalBins);
            else v = 0.f;
            ProcessedData[index] = v;
        }
        
        for( int index = 0; index < TotalBins; ++index )
            ProcessedData[index] = juce::Decibels::gainToDecibels(ProcessedData[index], negativeInfinity);
        
        FftBuffer.push(ProcessedData);
    }
    
    void Rearrange(FFTOrder NewOrder)
    {
        OrderedData = NewOrder;
        auto fftSize = GetSpectrumSize();
        
        Fft = std::make_unique<juce::dsp::FFT>(OrderedData);
        Boundary = std::make_unique<juce::dsp::WindowingFunction<float>>(fftSize, juce::dsp::WindowingFunction<float>::blackmanHarris);
        
        ProcessedData.clear();
        ProcessedData.resize(fftSize * 2, 0);

        FftBuffer.prepare(ProcessedData.size());
    }

    int GetSpectrumSize() const { return 1 << OrderedData; }
    int GetNumberOfDataBlocks() const { return FftBuffer.getNumAvailableForReading(); }

    bool GetData(BlockType& fftData) { return FftBuffer.pull(fftData); }
private:
    FFTOrder OrderedData;
    BlockType ProcessedData;
    std::unique_ptr<juce::dsp::FFT> Fft;
    std::unique_ptr<juce::dsp::WindowingFunction<float>> Boundary;
    
    Buffer<BlockType> FftBuffer;
};

template<typename PathType>
struct FFTSignalComponent
{
    void GenerateSignal(const std::vector<float>& renderData,juce::Rectangle<float> fftBounds,int fftSize,float binWidth,float negativeInfinity)
    {
        auto UpperValue = fftBounds.getY();
        auto LowerValue = fftBounds.getHeight();
        auto Length = fftBounds.getWidth();

        int TotalNumberOfBins = (int)fftSize / 2;

        PathType p;
        p.preallocateSpace(3 * (int)fftBounds.getWidth());

        auto map = [LowerValue, UpperValue, negativeInfinity](float v)
        {
            return juce::jmap(v, negativeInfinity, 0.f, float(LowerValue+10),   UpperValue);
        };

        auto y = map(renderData[0]);
        if( std::isnan(y) || std::isinf(y) ) y = LowerValue;
        
        p.startNewSubPath(0, y);

        const int LineThickness = 2;

        for( int IndexOfBin = 1; IndexOfBin < TotalNumberOfBins; IndexOfBin += LineThickness )
        {
            y = map(renderData[IndexOfBin]);

            if( !std::isnan(y) && !std::isinf(y) )
            {
                auto Frequency = IndexOfBin * binWidth;
                auto XCoordinateNormalized = juce::mapFromLog10(Frequency, 20.f, 20000.f);
                int binX = std::floor(XCoordinateNormalized * Length);
                p.lineTo(binX, y);
            }
        }

        Buffer.push(p);
    }

    int getNumPathsAvailable() const { return Buffer.getNumAvailableForReading();}

    bool getPath(PathType& path) { return Buffer.pull(path); }
    
private:
    Buffer<PathType> Buffer;
};

struct CustomLayout : juce::LookAndFeel_V4
{
    void ShowSliders (juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional,float rotaryStartAngle,float rotaryEndAngle, juce::Slider& ) override {};
    
    void ShowToggleButtons (juce::Graphics &g, juce::ToggleButton & toggleButton, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override {};
};

struct DSPSlider : juce::Slider
{
    DSPSlider(juce::RangedAudioParameter& rap, const juce::String& unitSuffix) :
    juce::Slider(juce::Slider::SliderStyle::LinearHorizontal,
                 juce::Slider::TextEntryBoxPosition::NoTextBox),
    parameters(&rap),
    unitOfMeasure(unitSuffix)
    {
    }
    
    ~DSPSlider()
    {
        setLookAndFeel(nullptr);
    }
    
    struct LabelPosition
    {
        float pos;
        juce::String label;
    };
    
    // List where we will store our labels
    juce::Array<LabelPosition> listOfLabels;
    
    // Default functions that come from inheritance from the juce library
    void paint(juce::Graphics& g) override;
    juce::Rectangle<int> getSliderBounds() const;
    int getTextHeight() const { return 14; }
    juce::String getDisplayString() const;
    //Private memebrs of the struct
private:
    CustomLayout layout;
    juce::RangedAudioParameter* parameters;
    juce::String unitOfMeasure;
};

struct SignlarTracer
{
    SignlarTracer(SingleChannelBuffer<ProvaDSPAudioProcessor::BlockType>& SingleChannelBuffer) :
    leftChannelFifo(&SingleChannelBuffer)
    {
        LeftChannelFFTAnalyzer.Rearrange(FFTOrder::order2048);
        AudioBuffer.setSize(1, LeftChannelFFTAnalyzer.GetSpectrumSize());
    }
    void process(juce::Rectangle<float> fftBounds, double sampleRate);
    juce::Path getPath() { return LeftChannelFFTSignal; }
    // Private members of the struct
private:
    SingleChannelBuffer<ProvaDSPAudioProcessor::BlockType>* leftChannelFifo;
    
    juce::AudioBuffer<float> AudioBuffer;
    SpectrumAnalyzer<std::vector<float>> LeftChannelFFTAnalyzer;
    
    FFTSignalComponent<juce::Path> signalTracer;
    
    juce::Path LeftChannelFFTSignal;
};

struct ResponseCurveComponent: juce::Component,
juce::AudioProcessorParameter::Listener,
juce::Timer
{
    ResponseCurveComponent(ProvaDSPAudioProcessor&);
    ~ResponseCurveComponent();
    
    // Default functions from inheritance, not to be changed
    void parameterValueChanged (int parameterIndex, float newValue) override;
    void parameterGestureChanged (int parameterIndex, bool gestureIsStarting) override { }
    void timerCallback() override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    
    void toggleAnalysisEnablement(bool enabled)
    {
        EnableFFT = enabled;
    }
private:
    ProvaDSPAudioProcessor& DSPProcessor;
    bool EnableFFT = true;
    juce::Atomic<bool> RealoadParametersEvent { false };
    MonoChain MonoChannelChain;

    void TraceTransferFunctionFrequencyCurve();
    
    juce::Path TransferFunctionCurve;

    void UpdateBuffer();
    void ShowPlotGrid(juce::Graphics& g);
    void ShowPlotLabels(juce::Graphics& g);
    
    std::vector<float> GetAllFrequncies();
    std::vector<float> GetAllGains();
    std::vector<float> GetXCoordinates(const std::vector<float>& Frequencies, float LeftChannel, float Width);

    juce::Rectangle<int> GetArea();
    juce::Rectangle<int> GetAnalyzerAres();
    
    SignlarTracer LeftSignalChannel, RightSignalChannel;
};

struct ToggleButton : juce::ToggleButton { };

struct DSPToggleButton : juce::ToggleButton
{
    void resized() override
    {
        auto Boundaries = getLocalBounds();
        auto Intersecion = Boundaries.reduced(4);
        
        RandomSignalGenerator.clear();
        juce::Random r;
        
        RandomSignalGenerator.startNewSubPath(Intersecion.getX(), Intersecion.getY() + Intersecion.getHeight() * r.nextFloat());
        auto initialValue = Intersecion.getX() + 1;
        
        for( auto j = initialValue; j < Intersecion.getRight(); j = j + 2 )
            RandomSignalGenerator.lineTo(j, Intersecion.getY() + Intersecion.getHeight() * r.nextFloat());
    }
    
    juce::Path RandomSignalGenerator;
};


class ProvaDSPAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    ProvaDSPAudioProcessorEditor (ProvaDSPAudioProcessor&);
    ~ProvaDSPAudioProcessorEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    ProvaDSPAudioProcessor& audioProcessor;

    // Declaration of the sliders
    DSPSlider
        CentralFrequencySlider, CentralFrequencyGainSlider,TransferFunctionQualityFactorSlider,
        LowFrequencyCutoffSlider,HighFrequncyCutoffSlider,
        LowFrequncyCutoffSlopeSlider, HighFrequencyCutoffSlider;
    
    ResponseCurveComponent ProcessedCurveFourierTransformComponent;
    
    using TreeState = juce::AudioProcessorValueTreeState;
    using Attachments = TreeState::SliderAttachment;
    
    Attachments
        PeakFrequencySliderAttatch, PeakGainSliderAttach, PeakQualityAttach,
        LowCutoffAttach, HightCutoffAttach, LowSlopeAttatch, HighSlopeAttach;

    std::vector<juce::Component*> GetComponents();
    
    ToggleButton ToogleLowPassButton, ToggleCentralFrequncyButton, ToggleHighPassButon;
    DSPToggleButton analyzerEnabledButton;
    
    using ButtonAttachment = TreeState::ButtonAttachment;
    
    ButtonAttachment LowButtonAttatch, CentralButtonAttatch, HighButtonAttach, ToggleAnalyzerButton;
    
    CustomLayout layout;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProvaDSPAudioProcessorEditor)
};
