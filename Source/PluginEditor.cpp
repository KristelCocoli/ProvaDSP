/*
  ==============================================================================
    This file contains the basic framework code for a JUCE plugin editor.
  ==============================================================================
*/

//#include "PluginProcessor.h"
#include "PluginEditor.h"

void DSPSlider::paint(juce::Graphics &g)
{
    using namespace juce;
    
    auto InitialAngle = degreesToRadians(180.f + 45.f);
    auto FinalPosition = degreesToRadians(180.f - 45.f) + MathConstants<float>::twoPi;
    auto FullRangeOfSlider = getRange();
    auto SliderBoundaries = GetBoundariesOfSlider();
    
    getLookAndFeel().ShowSliders(g, SliderBoundaries.getX(), SliderBoundaries.getY(), SliderBoundaries.getWidth(), SliderBoundaries.getHeight(), jmap(getValue(), FullRangeOfSlider.getStart(), FullRangeOfSlider.getEnd(), 0.0, 1.0), InitialAngle, FinalPosition, *this);
    
    auto CentralCoordinate = SliderBoundaries.toFloat().getCentre();
    auto RadiusOfRoundSlider = SliderBoundaries.getWidth() * 0.5f;
    
    g.setColour(Colours::red);
    g.setFont(getTextHeight());
    
    auto Choices = listOfLabels.size();
    for( int index = 0; index < Choices; ++index )
    {
        auto Position = listOfLabels[index].pos;
        
        auto SelectedAngle = jmap(Position, 0.f, 1.f, InitialAngle, FinalPosition);
        
        auto c = CentralCoordinate.getPointOnCircumference(RadiusOfRoundSlider + getTextHeight() * 0.5f + 1, SelectedAngle);
        
        Rectangle<float> r;
        auto label = listOfLabels[index].label;
        r.setSize(g.getCurrentFont().getStringWidth(label), getTextHeight());
        r.setCentre(c);
        r.setY(r.getY() + getTextHeight());
        
        g.drawFittedText(label, r.toNearestInt(), juce::Justification::centred, 1);
    }
    
}

juce::Rectangle<int> DSPSlider::GetBoundariesOfSlider() const
{
    auto Boundaries = getLocalBounds();
    
    auto Size = juce::jmin(Boundaries.getWidth(), Boundaries.getHeight());
    
    Size -= getTextHeight() * 2;
    juce::Rectangle<int> r;
    r.setSize(Size, Size);
    r.setCentre(Boundaries.getCentreX(), 0);
    r.setY(2);
    
    return r;
    
}

juce::String DSPSlider::GetString() const
{
    if( auto* ParameterOfChoice = dynamic_cast<juce::AudioParameterChoice*>(parameters) )
        return ParameterOfChoice->getCurrentChoiceName();
    
    juce::String Value;
    bool addP = false;
    
    if( auto* FloatPrecisionPointVariable = dynamic_cast<juce::AudioParameterFloat*>(parameters) )
    {
        float ValueOfParameter = getValue();
        
        if( ValueOfParameter > 999.f )
        {
            ValueOfParameter /= 1000.f;
            addP = true;
        }
        
        Value = juce::String(ValueOfParameter, (addP ? 2 : 0));
    }

    if( unitOfMeasure.isNotEmpty() )
    {
        Value << " ";
        if( addP )
            Value << "k";
        
        Value << unitOfMeasure;
    }
    
    return Value;
}


ResponseCurveComponent::ResponseCurveComponent(ProvaDSPAudioProcessor& p) :
    DSPProcessor(p), LeftSignalChannel(DSPProcessor.leftChannelFifo), RightSignalChannel(DSPProcessor.rightChannelFifo)
{
    const auto& Params = DSPProcessor.getParameters();
    for( auto Param : Params )
        Param->addListener(this);

    UpdateBuffer();
    
    startTimerHz(60);
}

ResponseCurveComponent::~ResponseCurveComponent()
{
    const auto& Params = DSPProcessor.getParameters();
    for( auto Param : Params )
        Param->removeListener(this);
}

void ResponseCurveComponent::TraceTransferFunctionFrequencyCurve()
{
    using namespace juce;
    auto Area = GetAnalyzerAres();
    
    auto Width = Area.getWidth();
    
    auto& LowCutoffFreq = MonoChannelChain.get<ChainPositions::LowCut>();
    auto& PeakFreq = MonoChannelChain.get<ChainPositions::Peak>();
    auto& HighFreqCutOff = MonoChannelChain.get<ChainPositions::HighCut>();
    
    auto sampleRate = DSPProcessor.getSampleRate();
    
    std::vector<double> mags;
    
    mags.resize(Width);
    
    for( int i = 0; i < Width; ++i )
    {
        double mag = 1.f;
        auto Frequencies = mapToLog10(double(i) / double(Width), 20.0, 20000.0);
        
        if(! MonoChannelChain.isBypassed<ChainPositions::Peak>() )
            mag *= PeakFreq.coefficients->getMagnitudeForFrequency(Frequencies, sampleRate);
        
        if( !MonoChannelChain.isBypassed<ChainPositions::LowCut>() )
        {
            if( !LowCutoffFreq.isBypassed<0>() )
                mag *= LowCutoffFreq.get<0>().coefficients->getMagnitudeForFrequency(Frequencies, sampleRate);
            if( !LowCutoffFreq.isBypassed<1>() )
                mag *= LowCutoffFreq.get<1>().coefficients->getMagnitudeForFrequency(Frequencies, sampleRate);
            if( !LowCutoffFreq.isBypassed<2>() )
                mag *= LowCutoffFreq.get<2>().coefficients->getMagnitudeForFrequency(Frequencies, sampleRate);
            if( !LowCutoffFreq.isBypassed<3>() )
                mag *= LowCutoffFreq.get<3>().coefficients->getMagnitudeForFrequency(Frequencies, sampleRate);
        }
        
        if( !MonoChannelChain.isBypassed<ChainPositions::HighCut>() )
        {
            if( !HighFreqCutOff.isBypassed<0>() )
                mag *= HighFreqCutOff.get<0>().coefficients->getMagnitudeForFrequency(Frequencies, sampleRate);
            if( !HighFreqCutOff.isBypassed<1>() )
                mag *= HighFreqCutOff.get<1>().coefficients->getMagnitudeForFrequency(Frequencies, sampleRate);
            if( !HighFreqCutOff.isBypassed<2>() )
                mag *= HighFreqCutOff.get<2>().coefficients->getMagnitudeForFrequency(Frequencies, sampleRate);
            if( !HighFreqCutOff.isBypassed<3>() )
                mag *= HighFreqCutOff.get<3>().coefficients->getMagnitudeForFrequency(Frequencies, sampleRate);
        }
            
        mags[i] = Decibels::gainToDecibels(mag);
    }
    
    TransferFunctionCurve.clear();
    
    const double MinimumOutput = Area.getBottom();
    const double MaximumOutput = Area.getY();
    auto map = [MinimumOutput, MaximumOutput](double input)
    {
        return jmap(input, -24.0, 24.0, MinimumOutput, MaximumOutput);
    };
    
    TransferFunctionCurve.startNewSubPath(Area.getX(), map(mags.front()));
    
    for( size_t i = 1; i < mags.size(); ++i )
        TransferFunctionCurve.lineTo(Area.getX() + i, map(mags[i]));
}

void ResponseCurveComponent::paint (juce::Graphics& g)
{
    using namespace juce;
    // Set background color
    g.fillAll (Colours::whitesmoke);

    ShowPlotGrid(g);
    
    auto TransferFunctionArea = GetAnalyzerAres();
    
    if( EnableFFT )
    {
        auto LeftSamplesFFT = LeftSignalChannel.getPath();
        LeftSamplesFFT.applyTransform(AffineTransform().translation(TransferFunctionArea.getX(), TransferFunctionArea.getY()));
        
        g.setColour(Colours::darkblue);
        g.strokePath(LeftSamplesFFT, PathStrokeType(1.f));
        
        auto RightChannelFFT = RightSignalChannel.getPath();
        RightChannelFFT.applyTransform(AffineTransform().translation(TransferFunctionArea.getX(), TransferFunctionArea.getY()));
        
        g.setColour(Colours::darkorange);
        g.strokePath(RightChannelFFT, PathStrokeType(1.f));
    }
    
    // Colour of frequency response of transfer function
    g.setColour(Colours::red);
    g.strokePath(TransferFunctionCurve, PathStrokeType(2.f));
    
    Path Boundaries;
    
    Boundaries.setUsingNonZeroWinding(false);
    
    Boundaries.addRoundedRectangle(GetArea(), 4);
    Boundaries.addRectangle(getLocalBounds());
    
    g.setColour(Colours::whitesmoke);
    
    g.fillPath(Boundaries);
    
    ShowPlotLabels(g);
    
    g.setColour(Colours::orange);
    g.drawRoundedRectangle(GetArea().toFloat(), 4.f, 1.f);
}

std::vector<float> ResponseCurveComponent::GetAllFrequncies()
{
    return std::vector<float>
    {
        20, 50, 100,
        200, 500, 1000,
        2000, 5000, 10000,
        20000
    };
}

std::vector<float> ResponseCurveComponent::GetAllGains()
{
    return std::vector<float>
    {
        -24, -12, 0, 12, 24
    };
}

std::vector<float> ResponseCurveComponent::GetXCoordinates(const std::vector<float> &frequencies, float left, float width)
{
    std::vector<float> XCoordinates;
    for( auto f : frequencies )
    {
        auto NormalizedXCoordinates = juce::mapFromLog10(f, 20.f, 20000.f);
        XCoordinates.push_back( left + width * NormalizedXCoordinates );
    }
    
    return XCoordinates;
}

void ResponseCurveComponent::ShowPlotGrid(juce::Graphics &g)
{
    using namespace juce;
    auto Frequencies = GetAllFrequncies();
    
    auto Area = GetAnalyzerAres();
    auto XAxis = Area.getX();
    auto MaxXCoordinate = Area.getRight();
    auto YAxis = Area.getY();
    auto MinimumYCoordinate = Area.getBottom();
    auto SpectrumSize = Area.getWidth();
    
    auto xs = GetXCoordinates(Frequencies, XAxis, SpectrumSize);
    
    g.setColour(Colours::dimgrey);
    for( auto x : xs )
    {
        g.drawVerticalLine(x, YAxis, MinimumYCoordinate);
    }
    
    auto gain = GetAllGains();
    
    for( auto gDb : gain )
    {
        auto y = jmap(gDb, -24.f, 24.f, float(MinimumYCoordinate), float(YAxis));
        
        g.setColour(gDb == 0.f ? Colour(0u, 172u, 1u) : Colours::darkgrey );
        g.drawHorizontalLine(y, XAxis, MaxXCoordinate);
    }
}

void ResponseCurveComponent::ShowPlotLabels(juce::Graphics &g)
{
    using namespace juce;
    g.setColour(Colours::lightgrey);
    const int FontSize = 12;
    g.setFont(FontSize);
    
    auto Area = GetAnalyzerAres();
    auto XAxis = Area.getX();
    
    auto YAxis = Area.getY();
    auto MinimumYCoordinate = Area.getBottom();
    auto MaximumXCoordinate = Area.getWidth();
    
    auto Frequencies = GetAllFrequncies();
    auto xs = GetXCoordinates(Frequencies, XAxis, MaximumXCoordinate);
    
    for( int i = 0; i < Frequencies.size(); ++i )
    {
        auto f = Frequencies[i];
        auto x = xs[i];

        bool addK = false;
        String str;
        if( f > 999.f )
        {
            addK = true;
            f /= 1000.f;
        }

        str << f;
        if( addK )
            str << "k";
        str << "Hz";
        
        auto textWidth = g.getCurrentFont().getStringWidth(str);

        Rectangle<int> r;

        r.setSize(textWidth, FontSize);
        r.setCentre(x, 0);
        r.setY(1);
        
        g.drawFittedText(str, r, juce::Justification::centred, 1);
    }
    
    auto gain = GetAllGains();

    for( auto gDb : gain )
    {
        auto y = jmap(gDb, -24.f, 24.f, float(MinimumYCoordinate), float(YAxis));
        
        String str;
        if( gDb > 0 )
            str << "+";
        str << gDb;
        
        auto textWidth = g.getCurrentFont().getStringWidth(str);
        
        Rectangle<int> r;
        r.setSize(textWidth, FontSize);
        r.setX(getWidth() - textWidth);
        r.setCentre(r.getCentreX(), y);
        
        g.setColour(gDb == 0.f ? Colour(0u, 172u, 1u) : Colours::lightgrey );
        
        g.drawFittedText(str, r, juce::Justification::centredLeft, 1);
        
        str.clear();
        str << (gDb - 24.f);

        r.setX(1);
        textWidth = g.getCurrentFont().getStringWidth(str);
        r.setSize(textWidth, FontSize);
        g.setColour(Colours::lightgrey);
        g.drawFittedText(str, r, juce::Justification::centredLeft, 1);
    }
}

void ResponseCurveComponent::resized()
{
    using namespace juce;
    
    TransferFunctionCurve.preallocateSpace(getWidth() * 3);
    TraceTransferFunctionFrequencyCurve();
}

void ResponseCurveComponent::parameterValueChanged(int parameterIndex, float newValue)
{
    RealoadParametersEvent.set(true);
}

void SignlarTracer::process(juce::Rectangle<float> fftBounds, double sampleRate)
{
    juce::AudioBuffer<float> TempBuffer;
    while( leftChannelFifo->getNumCompleteBuffersAvailable() > 0 )
    {
        if( leftChannelFifo->getAudioBuffer(TempBuffer) )
        {
            auto size = TempBuffer.getNumSamples();

            juce::FloatVectorOperations::copy(AudioBuffer.getWritePointer(0, 0),AudioBuffer.getReadPointer(0, size), AudioBuffer.getNumSamples() - size);

            juce::FloatVectorOperations::copy(AudioBuffer.getWritePointer(0, AudioBuffer.getNumSamples() - size),TempBuffer.getReadPointer(0, 0), size);
            
            LeftChannelFFTAnalyzer.ApplyFftAnalysis(AudioBuffer, -48.f);
        }
    }
    
    const auto SpectrumSize = LeftChannelFFTAnalyzer.GetSpectrumSize();
    const auto BinWidth = sampleRate / double(SpectrumSize);

    while( LeftChannelFFTAnalyzer.GetNumberOfDataBlocks() > 0 )
    {
        std::vector<float> fftData;
        if( LeftChannelFFTAnalyzer.GetData( fftData) )
        {
            signalTracer.GenerateSignal(fftData, fftBounds, SpectrumSize, BinWidth, -48.f);
        }
    }
    
    while( signalTracer.getNumPathsAvailable() > 0 )
    {
        signalTracer.getPath( LeftChannelFFTSignal );
    }
}

void ResponseCurveComponent::timerCallback()
{
    if( EnableFFT )
    {
        auto FftSize = GetAnalyzerAres().toFloat();
        auto SamplingFrequency = DSPProcessor.getSampleRate();
        
        LeftSignalChannel.process(FftSize, SamplingFrequency);
        RightSignalChannel.process(FftSize, SamplingFrequency);
    }

    if( RealoadParametersEvent.compareAndSetBool(false, true) )
    {
        UpdateBuffer();
        TraceTransferFunctionFrequencyCurve();
    }
    
    repaint();
}

void ResponseCurveComponent::UpdateBuffer()
{
    auto ChainConfigurations = getChainSettings(DSPProcessor.apvts);
    
    MonoChannelChain.setBypassed<ChainPositions::LowCut>(ChainConfigurations.lowCutBypassed);
    MonoChannelChain.setBypassed<ChainPositions::Peak>(ChainConfigurations.peakBypassed);
    MonoChannelChain.setBypassed<ChainPositions::HighCut>(ChainConfigurations.highCutBypassed);
    
    auto peakCoefficients = makePeakFilter(ChainConfigurations, DSPProcessor.getSampleRate());
    updateCoefficients(MonoChannelChain.get<ChainPositions::Peak>().coefficients, peakCoefficients);
    
    auto LowCutoffParameters = makeLowCutFilter(ChainConfigurations, DSPProcessor.getSampleRate());
    auto HighFrequencyCutoffParameters = makeHighCutFilter(ChainConfigurations, DSPProcessor.getSampleRate());
    
    updateCutFilter(MonoChannelChain.get<ChainPositions::LowCut>(),
                    LowCutoffParameters,
                    ChainConfigurations.lowCutSlope);
    
    updateCutFilter(MonoChannelChain.get<ChainPositions::HighCut>(),
                    HighFrequencyCutoffParameters,
                    ChainConfigurations.highCutSlope);
}

juce::Rectangle<int> ResponseCurveComponent::GetArea()
{
    auto Boundaries = getLocalBounds();
    
    Boundaries.removeFromTop(12);
    Boundaries.removeFromBottom(2);
    Boundaries.removeFromLeft(20);
    Boundaries.removeFromRight(20);
    
    return Boundaries;
}


juce::Rectangle<int> ResponseCurveComponent::GetAnalyzerAres()
{
    auto Boundaries = GetArea();
    Boundaries.removeFromTop(4);
    Boundaries.removeFromBottom(4);
    return Boundaries;
}

ProvaDSPAudioProcessorEditor::ProvaDSPAudioProcessorEditor (ProvaDSPAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p),
CentralFrequencySlider(*audioProcessor.apvts.getParameter("Peak Freq"), "Hz"),
CentralFrequencyGainSlider(*audioProcessor.apvts.getParameter("Peak Gain"), "dB"),
TransferFunctionQualityFactorSlider(*audioProcessor.apvts.getParameter("Peak Quality"), ""),
LowFrequencyCutoffSlider(*audioProcessor.apvts.getParameter("LowCut Freq"), "Hz"),
HighFrequncyCutoffSlider(*audioProcessor.apvts.getParameter("HighCut Freq"), "Hz"),
LowFrequncyCutoffSlopeSlider(*audioProcessor.apvts.getParameter("LowCut Slope"), "dB/Oct"),
HighFrequencyCutoffSlider(*audioProcessor.apvts.getParameter("HighCut Slope"), "db/Oct"),

ProcessedCurveFourierTransformComponent(audioProcessor),

PeakFrequencySliderAttatch(audioProcessor.apvts, "Peak Freq", CentralFrequencySlider),
PeakGainSliderAttach(audioProcessor.apvts, "Peak Gain", CentralFrequencyGainSlider),
PeakQualityAttach(audioProcessor.apvts, "Peak Quality", TransferFunctionQualityFactorSlider),
LowCutoffAttach(audioProcessor.apvts, "LowCut Freq", LowFrequencyCutoffSlider),
HightCutoffAttach(audioProcessor.apvts, "HighCut Freq", HighFrequncyCutoffSlider),
LowSlopeAttatch(audioProcessor.apvts, "LowCut Slope", LowFrequncyCutoffSlopeSlider),
HighSlopeAttach(audioProcessor.apvts, "HighCut Slope", HighFrequencyCutoffSlider),

LowButtonAttatch(audioProcessor.apvts, "LowCut Bypassed", ToogleLowPassButton),
CentralButtonAttatch(audioProcessor.apvts, "Peak Bypassed", ToggleCentralFrequncyButton),
HighButtonAttach(audioProcessor.apvts, "HighCut Bypassed", ToggleHighPassButon),
ToggleAnalyzerButton(audioProcessor.apvts, "Analyzer Enabled", analyzerEnabledButton)
{
    CentralFrequencySlider.listOfLabels.add({0.f, "20Hz"});
    CentralFrequencySlider.listOfLabels.add({1.f, "20kHz"});
    
    CentralFrequencyGainSlider.listOfLabels.add({0.f, "-24dB"});
    CentralFrequencyGainSlider.listOfLabels.add({1.f, "+24dB"});
    
    TransferFunctionQualityFactorSlider.listOfLabels.add({0.f, "0.1"});
    TransferFunctionQualityFactorSlider.listOfLabels.add({1.f, "10.0"});
    
    LowFrequencyCutoffSlider.listOfLabels.add({0.f, "20Hz"});
    LowFrequencyCutoffSlider.listOfLabels.add({1.f, "20kHz"});
    
    HighFrequncyCutoffSlider.listOfLabels.add({0.f, "20Hz"});
    HighFrequncyCutoffSlider.listOfLabels.add({1.f, "20kHz"});
    
    LowFrequncyCutoffSlopeSlider.listOfLabels.add({0.0f, "12"});
    LowFrequncyCutoffSlopeSlider.listOfLabels.add({1.f, "48"});
    
    HighFrequencyCutoffSlider.listOfLabels.add({0.0f, "12"});
    HighFrequencyCutoffSlider.listOfLabels.add({1.f, "48"});
    
    for( auto* comp : GetComponents() )
    {
        addAndMakeVisible(comp);
    }
    
    ToggleCentralFrequncyButton.setLookAndFeel(&layout);
    ToggleHighPassButon.setLookAndFeel(&layout);
    ToogleLowPassButton.setLookAndFeel(&layout);

    analyzerEnabledButton.setLookAndFeel(&layout);
    
    auto safePtr = juce::Component::SafePointer<ProvaDSPAudioProcessorEditor>(this);
    ToggleCentralFrequncyButton.onClick = [safePtr]()
    {
        if( auto* comp = safePtr.getComponent() )
        {
            auto bypassed = comp->ToggleCentralFrequncyButton.getToggleState();
            
            comp->CentralFrequencySlider.setEnabled( !bypassed );
            comp->CentralFrequencyGainSlider.setEnabled( !bypassed );
            comp->TransferFunctionQualityFactorSlider.setEnabled( !bypassed );
        }
    };
    

    ToogleLowPassButton.onClick = [safePtr]()
    {
        if( auto* comp = safePtr.getComponent() )
        {
            auto bypassed = comp->ToogleLowPassButton.getToggleState();
            
            comp->LowFrequencyCutoffSlider.setEnabled( !bypassed );
            comp->LowFrequncyCutoffSlopeSlider.setEnabled( !bypassed );
        }
    };
    
    ToggleHighPassButon.onClick = [safePtr]()
    {
        if( auto* comp = safePtr.getComponent() )
        {
            auto bypassed = comp->ToggleHighPassButon.getToggleState();
            
            comp->HighFrequncyCutoffSlider.setEnabled( !bypassed );
            comp->HighFrequencyCutoffSlider.setEnabled( !bypassed );
        }
    };

    analyzerEnabledButton.onClick = [safePtr]()
    {
        if( auto* comp = safePtr.getComponent() )
        {
            auto enabled = comp->analyzerEnabledButton.getToggleState();
            comp->ProcessedCurveFourierTransformComponent.toggleAnalysisEnablement(enabled);
        }
    };
    
    setSize (1000, 500);
}

ProvaDSPAudioProcessorEditor::~ProvaDSPAudioProcessorEditor()
{
    ToggleCentralFrequncyButton.setLookAndFeel(nullptr);
    ToggleHighPassButon.setLookAndFeel(nullptr);
    ToogleLowPassButton.setLookAndFeel(nullptr);

    analyzerEnabledButton.setLookAndFeel(nullptr);
}


void ProvaDSPAudioProcessorEditor::paint(juce::Graphics &g)
{
    using namespace juce;
    
    g.fillAll (Colours::darkcyan);
    
    Path Signal;
    
    auto bounds = getLocalBounds();
    auto center = bounds.getCentre();
    
    auto Corner = 20;
    auto CurvePosition = Signal.getCurrentPosition();
    Signal.quadraticTo(CurvePosition.getX() - Corner, CurvePosition.getY(),
                      CurvePosition.getX() - Corner, CurvePosition.getY() - 16);
    CurvePosition = Signal.getCurrentPosition();
    Signal.quadraticTo(CurvePosition.getX(), 2,
                      CurvePosition.getX() - Corner, 2);
    
    Signal.lineTo({0.f, 2.f});
    Signal.lineTo(0.f, 0.f);
    Signal.lineTo(center.x, 0.f);
    Signal.closeSubPath();
    
    g.setColour(Colour(97u, 18u, 167u));
    g.fillPath(Signal);
    
    Signal.applyTransform(AffineTransform().scaled(-1, 1));
    Signal.applyTransform(AffineTransform().translated(getWidth(), 0));
    g.fillPath(Signal);
    
    
    g.setColour(Colour(255u, 154u, 1u));
    
    g.setColour(Colours::red);
    g.setFont(14);
    g.drawFittedText("LowCut", LowFrequncyCutoffSlopeSlider.getBounds(), juce::Justification::centredBottom, 1);
    g.drawFittedText("Peak", TransferFunctionQualityFactorSlider.getBounds(), juce::Justification::centredBottom, 1);
    g.drawFittedText("HighCut", HighFrequencyCutoffSlider.getBounds(), juce::Justification::centredBottom, 1);

}

void ProvaDSPAudioProcessorEditor::resized()
{
    auto Boundaries = getLocalBounds();
    Boundaries.removeFromTop(4);
    
    auto analyzerEnabledArea = Boundaries.removeFromTop(25);
    
    analyzerEnabledArea.setWidth(50);
    analyzerEnabledArea.setX(5);
    analyzerEnabledArea.removeFromTop(2);
    
    analyzerEnabledButton.setBounds(analyzerEnabledArea);
    
    Boundaries.removeFromTop(5);
    
    // Declare ratios as variables to easily change them
    float HalfRatio = 50.f / 100.f;
    float OneThirdRatio = 33.f / 100.f;
    auto responseArea = Boundaries.removeFromTop(Boundaries.getHeight() * HalfRatio);

    ProcessedCurveFourierTransformComponent.setBounds(responseArea);
    
    Boundaries.removeFromTop(5);
    
    auto LowFrequencyCutoffBoundary = Boundaries.removeFromTop(Boundaries.getHeight() * OneThirdRatio);
    auto HighFrequencyCutoffBoundary = Boundaries.removeFromBottom(Boundaries.getHeight() * HalfRatio);
    
    ToogleLowPassButton.setBounds(LowFrequencyCutoffBoundary.removeFromLeft(25));
    LowFrequencyCutoffSlider.setBounds(LowFrequencyCutoffBoundary.removeFromLeft(LowFrequencyCutoffBoundary.getWidth() * HalfRatio));
    LowFrequncyCutoffSlopeSlider.setBounds(LowFrequencyCutoffBoundary);
    
    ToggleHighPassButon.setBounds(HighFrequencyCutoffBoundary.removeFromLeft(25));
    HighFrequncyCutoffSlider.setBounds(HighFrequencyCutoffBoundary.removeFromLeft(HighFrequencyCutoffBoundary.getWidth() *HalfRatio));
    HighFrequencyCutoffSlider.setBounds(HighFrequencyCutoffBoundary);
    
    ToggleCentralFrequncyButton.setBounds(Boundaries.removeFromLeft(25));
    CentralFrequencySlider.setBounds(Boundaries.removeFromLeft(Boundaries.getWidth() * OneThirdRatio));
    CentralFrequencyGainSlider.setBounds(Boundaries.removeFromLeft(Boundaries.getWidth() * HalfRatio));
    TransferFunctionQualityFactorSlider.setBounds(Boundaries);
}

std::vector<juce::Component*> ProvaDSPAudioProcessorEditor::GetComponents()
{
    return
    {
        &CentralFrequencySlider, &CentralFrequencyGainSlider, &TransferFunctionQualityFactorSlider,
        &LowFrequencyCutoffSlider, &HighFrequncyCutoffSlider, &LowFrequncyCutoffSlopeSlider,
        &HighFrequencyCutoffSlider, &ProcessedCurveFourierTransformComponent,
        &ToogleLowPassButton, &ToggleCentralFrequncyButton,&ToggleHighPassButon, &analyzerEnabledButton
    };
}
