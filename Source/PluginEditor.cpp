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
    
    auto startAng = degreesToRadians(180.f + 45.f);
    auto endAng = degreesToRadians(180.f - 45.f) + MathConstants<float>::twoPi;
    
    auto range = getRange();
    
    auto sliderBounds = getSliderBounds();
    
//    g.setColour(Colours::red);
//    g.drawRect(getLocalBounds());
//    g.setColour(Colours::yellow);
//    g.drawRect(sliderBounds);
    
    getLookAndFeel().ShowSliders(g,
                                      sliderBounds.getX(),
                                      sliderBounds.getY(),
                                      sliderBounds.getWidth(),
                                      sliderBounds.getHeight(),
                                      jmap(getValue(), range.getStart(), range.getEnd(), 0.0, 1.0),
                                      startAng,
                                      endAng,
                                      *this);
    
    auto center = sliderBounds.toFloat().getCentre();
    auto radius = sliderBounds.getWidth() * 0.5f;
    
    g.setColour(Colours::red);
    g.setFont(getTextHeight());
    
    auto numChoices = listOfLabels.size();
    for( int i = 0; i < numChoices; ++i )
    {
        auto pos = listOfLabels[i].pos;
        jassert(0.f <= pos);
        jassert(pos <= 1.f);
        
        auto ang = jmap(pos, 0.f, 1.f, startAng, endAng);
        
        auto c = center.getPointOnCircumference(radius + getTextHeight() * 0.5f + 1, ang);
        
        Rectangle<float> r;
        auto str = listOfLabels[i].label;
        r.setSize(g.getCurrentFont().getStringWidth(str), getTextHeight());
        r.setCentre(c);
        r.setY(r.getY() + getTextHeight());
        
        g.drawFittedText(str, r.toNearestInt(), juce::Justification::centred, 1);
    }
    
}

juce::Rectangle<int> DSPSlider::getSliderBounds() const
{
    auto bounds = getLocalBounds();
    
    auto size = juce::jmin(bounds.getWidth(), bounds.getHeight());
    
    size -= getTextHeight() * 2;
    juce::Rectangle<int> r;
    r.setSize(size, size);
    r.setCentre(bounds.getCentreX(), 0);
    r.setY(2);
    
    return r;
    
}

juce::String DSPSlider::getDisplayString() const
{
    if( auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*>(parameters) )
        return choiceParam->getCurrentChoiceName();
    
    juce::String str;
    bool addK = false;
    
    if( auto* floatParam = dynamic_cast<juce::AudioParameterFloat*>(parameters) )
    {
        float val = getValue();
        
        if( val > 999.f )
        {
            val /= 1000.f; //1001 / 1000 = 1.001
            addK = true;
        }
        
        str = juce::String(val, (addK ? 2 : 0));
    }
    else
    {
        jassertfalse; //this shouldn't happen!
    }
    
    if( unitOfMeasure.isNotEmpty() )
    {
        str << " ";
        if( addK )
            str << "k";
        
        str << unitOfMeasure;
    }
    
    return str;
}


ResponseCurveComponent::ResponseCurveComponent(ProvaDSPAudioProcessor& p) :
DSPProcessor(p),
LeftSignalChannel(DSPProcessor.leftChannelFifo),
RightSignalChannel(DSPProcessor.rightChannelFifo)
{
    const auto& params = DSPProcessor.getParameters();
    for( auto param : params )
    {
        param->addListener(this);
    }

    UpdateBuffer();
    
    startTimerHz(60);
}

ResponseCurveComponent::~ResponseCurveComponent()
{
    const auto& params = DSPProcessor.getParameters();
    for( auto param : params )
    {
        param->removeListener(this);
    }
}

void ResponseCurveComponent::TraceTransferFunctionFrequencyCurve()
{
    using namespace juce;
    auto responseArea = GetAnalyzerAres();
    
    auto w = responseArea.getWidth();
    
    auto& lowcut = MonoChannelChain.get<ChainPositions::LowCut>();
    auto& peak = MonoChannelChain.get<ChainPositions::Peak>();
    auto& highcut = MonoChannelChain.get<ChainPositions::HighCut>();
    
    auto sampleRate = DSPProcessor.getSampleRate();
    
    std::vector<double> mags;
    
    mags.resize(w);
    
    for( int i = 0; i < w; ++i )
    {
        double mag = 1.f;
        auto freq = mapToLog10(double(i) / double(w), 20.0, 20000.0);
        
        if(! MonoChannelChain.isBypassed<ChainPositions::Peak>() )
            mag *= peak.coefficients->getMagnitudeForFrequency(freq, sampleRate);
        
        if( !MonoChannelChain.isBypassed<ChainPositions::LowCut>() )
        {
            if( !lowcut.isBypassed<0>() )
                mag *= lowcut.get<0>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
            if( !lowcut.isBypassed<1>() )
                mag *= lowcut.get<1>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
            if( !lowcut.isBypassed<2>() )
                mag *= lowcut.get<2>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
            if( !lowcut.isBypassed<3>() )
                mag *= lowcut.get<3>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        }
        
        if( !MonoChannelChain.isBypassed<ChainPositions::HighCut>() )
        {
            if( !highcut.isBypassed<0>() )
                mag *= highcut.get<0>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
            if( !highcut.isBypassed<1>() )
                mag *= highcut.get<1>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
            if( !highcut.isBypassed<2>() )
                mag *= highcut.get<2>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
            if( !highcut.isBypassed<3>() )
                mag *= highcut.get<3>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        }
            
        mags[i] = Decibels::gainToDecibels(mag);
    }
    
    TransferFunctionCurve.clear();
    
    const double outputMin = responseArea.getBottom();
    const double outputMax = responseArea.getY();
    auto map = [outputMin, outputMax](double input)
    {
        return jmap(input, -24.0, 24.0, outputMin, outputMax);
    };
    
    TransferFunctionCurve.startNewSubPath(responseArea.getX(), map(mags.front()));
    
    for( size_t i = 1; i < mags.size(); ++i )
        TransferFunctionCurve.lineTo(responseArea.getX() + i, map(mags[i]));
}

void ResponseCurveComponent::paint (juce::Graphics& g)
{
    using namespace juce;
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (Colours::whitesmoke);

    ShowPlotGrid(g);
    
    auto responseArea = GetAnalyzerAres();
    
    if( EnableFFT )
    {
        auto leftChannelFFTPath = LeftSignalChannel.getPath();
        leftChannelFFTPath.applyTransform(AffineTransform().translation(responseArea.getX(), responseArea.getY()));
        
        g.setColour(Colours::darkblue);
        g.strokePath(leftChannelFFTPath, PathStrokeType(1.f));
        
        auto rightChannelFFTPath = RightSignalChannel.getPath();
        rightChannelFFTPath.applyTransform(AffineTransform().translation(responseArea.getX(), responseArea.getY()));
        
        g.setColour(Colours::cyan);
        g.strokePath(rightChannelFFTPath, PathStrokeType(1.f));
    }
    
    // Colour of transfer function
    g.setColour(Colours::red);
    g.strokePath(TransferFunctionCurve, PathStrokeType(2.f));
    
    Path border;
    
    border.setUsingNonZeroWinding(false);
    
    border.addRoundedRectangle(GetArea(), 4);
    border.addRectangle(getLocalBounds());
    
    g.setColour(Colours::whitesmoke);
    
    g.fillPath(border);
    
    ShowPlotLabels(g);
    
    g.setColour(Colours::orange);
    g.drawRoundedRectangle(GetArea().toFloat(), 4.f, 1.f);
}

std::vector<float> ResponseCurveComponent::GetAllFrequncies()
{
    return std::vector<float>
    {
        20, /*30, 40,*/ 50, 100,
        200, /*300, 400,*/ 500, 1000,
        2000, /*3000, 4000,*/ 5000, 10000,
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

std::vector<float> ResponseCurveComponent::GetXCoordinates(const std::vector<float> &freqs, float left, float width)
{
    std::vector<float> xs;
    for( auto f : freqs )
    {
        auto normX = juce::mapFromLog10(f, 20.f, 20000.f);
        xs.push_back( left + width * normX );
    }
    
    return xs;
}

void ResponseCurveComponent::ShowPlotGrid(juce::Graphics &g)
{
    using namespace juce;
    auto freqs = GetAllFrequncies();
    
    auto renderArea = GetAnalyzerAres();
    auto left = renderArea.getX();
    auto right = renderArea.getRight();
    auto top = renderArea.getY();
    auto bottom = renderArea.getBottom();
    auto width = renderArea.getWidth();
    
    auto xs = GetXCoordinates(freqs, left, width);
    
    g.setColour(Colours::dimgrey);
    for( auto x : xs )
    {
        g.drawVerticalLine(x, top, bottom);
    }
    
    auto gain = GetAllGains();
    
    for( auto gDb : gain )
    {
        auto y = jmap(gDb, -24.f, 24.f, float(bottom), float(top));
        
        g.setColour(gDb == 0.f ? Colour(0u, 172u, 1u) : Colours::darkgrey );
        g.drawHorizontalLine(y, left, right);
    }
}

void ResponseCurveComponent::ShowPlotLabels(juce::Graphics &g)
{
    using namespace juce;
    g.setColour(Colours::lightgrey);
    const int fontHeight = 10;
    g.setFont(fontHeight);
    
    auto renderArea = GetAnalyzerAres();
    auto left = renderArea.getX();
    
    auto top = renderArea.getY();
    auto bottom = renderArea.getBottom();
    auto width = renderArea.getWidth();
    
    auto freqs = GetAllFrequncies();
    auto xs = GetXCoordinates(freqs, left, width);
    
    for( int i = 0; i < freqs.size(); ++i )
    {
        auto f = freqs[i];
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

        r.setSize(textWidth, fontHeight);
        r.setCentre(x, 0);
        r.setY(1);
        
        g.drawFittedText(str, r, juce::Justification::centred, 1);
    }
    
    auto gain = GetAllGains();

    for( auto gDb : gain )
    {
        auto y = jmap(gDb, -24.f, 24.f, float(bottom), float(top));
        
        String str;
        if( gDb > 0 )
            str << "+";
        str << gDb;
        
        auto textWidth = g.getCurrentFont().getStringWidth(str);
        
        Rectangle<int> r;
        r.setSize(textWidth, fontHeight);
        r.setX(getWidth() - textWidth);
        r.setCentre(r.getCentreX(), y);
        
        g.setColour(gDb == 0.f ? Colour(0u, 172u, 1u) : Colours::lightgrey );
        
        g.drawFittedText(str, r, juce::Justification::centredLeft, 1);
        
        str.clear();
        str << (gDb - 24.f);

        r.setX(1);
        textWidth = g.getCurrentFont().getStringWidth(str);
        r.setSize(textWidth, fontHeight);
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
    juce::AudioBuffer<float> tempIncomingBuffer;
    while( leftChannelFifo->getNumCompleteBuffersAvailable() > 0 )
    {
        if( leftChannelFifo->getAudioBuffer(tempIncomingBuffer) )
        {
            auto size = tempIncomingBuffer.getNumSamples();

            juce::FloatVectorOperations::copy(AudioBuffer.getWritePointer(0, 0),
                                              AudioBuffer.getReadPointer(0, size),
                                              AudioBuffer.getNumSamples() - size);

            juce::FloatVectorOperations::copy(AudioBuffer.getWritePointer(0, AudioBuffer.getNumSamples() - size),
                                              tempIncomingBuffer.getReadPointer(0, 0),
                                              size);
            
            LeftChannelFFTAnalyzer.ApplyFftAnalysis(AudioBuffer, -48.f);
        }
    }
    
    const auto fftSize = LeftChannelFFTAnalyzer.GetSpectrumSize();
    const auto binWidth = sampleRate / double(fftSize);

    while( LeftChannelFFTAnalyzer.GetNumberOfDataBlocks() > 0 )
    {
        std::vector<float> fftData;
        if( LeftChannelFFTAnalyzer.GetData( fftData) )
        {
            signalTracer.GenerateSignal(fftData, fftBounds, fftSize, binWidth, -48.f);
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
        auto fftBounds = GetAnalyzerAres().toFloat();
        auto sampleRate = DSPProcessor.getSampleRate();
        
        LeftSignalChannel.process(fftBounds, sampleRate);
        RightSignalChannel.process(fftBounds, sampleRate);
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
    auto chainSettings = getChainSettings(DSPProcessor.apvts);
    
    MonoChannelChain.setBypassed<ChainPositions::LowCut>(chainSettings.lowCutBypassed);
    MonoChannelChain.setBypassed<ChainPositions::Peak>(chainSettings.peakBypassed);
    MonoChannelChain.setBypassed<ChainPositions::HighCut>(chainSettings.highCutBypassed);
    
    auto peakCoefficients = makePeakFilter(chainSettings, DSPProcessor.getSampleRate());
    updateCoefficients(MonoChannelChain.get<ChainPositions::Peak>().coefficients, peakCoefficients);
    
    auto lowCutCoefficients = makeLowCutFilter(chainSettings, DSPProcessor.getSampleRate());
    auto highCutCoefficients = makeHighCutFilter(chainSettings, DSPProcessor.getSampleRate());
    
    updateCutFilter(MonoChannelChain.get<ChainPositions::LowCut>(),
                    lowCutCoefficients,
                    chainSettings.lowCutSlope);
    
    updateCutFilter(MonoChannelChain.get<ChainPositions::HighCut>(),
                    highCutCoefficients,
                    chainSettings.highCutSlope);
}

juce::Rectangle<int> ResponseCurveComponent::GetArea()
{
    auto bounds = getLocalBounds();
    
    bounds.removeFromTop(12);
    bounds.removeFromBottom(2);
    bounds.removeFromLeft(20);
    bounds.removeFromRight(20);
    
    return bounds;
}


juce::Rectangle<int> ResponseCurveComponent::GetAnalyzerAres()
{
    auto bounds = GetArea();
    bounds.removeFromTop(4);
    bounds.removeFromBottom(4);
    return bounds;
}
//==============================================================================
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
    
    setSize (480, 500);
}

ProvaDSPAudioProcessorEditor::~ProvaDSPAudioProcessorEditor()
{
    ToggleCentralFrequncyButton.setLookAndFeel(nullptr);
    ToggleHighPassButon.setLookAndFeel(nullptr);
    ToogleLowPassButton.setLookAndFeel(nullptr);

    analyzerEnabledButton.setLookAndFeel(nullptr);
}

//==============================================================================
void ProvaDSPAudioProcessorEditor::paint(juce::Graphics &g)
{
    using namespace juce;
    
    g.fillAll (Colours::darkcyan);
    
    Path curve;
    
    auto bounds = getLocalBounds();
    auto center = bounds.getCentre();
    
//    g.setFont(Font("Iosevka Term Slab", 30, 0)); //https://github.com/be5invis/Iosevka
//
//    String title { "DAW BY KRISTEL COCOLI" };
//    g.setFont(30);
//    auto titleWidth = g.getCurrentFont().getStringWidth(title);
//
//    curve.startNewSubPath(center.x, 32);
//    curve.lineTo(center.x - titleWidth * 0.45f, 32);
    
    auto cornerSize = 20;
    auto curvePos = curve.getCurrentPosition();
    curve.quadraticTo(curvePos.getX() - cornerSize, curvePos.getY(),
                      curvePos.getX() - cornerSize, curvePos.getY() - 16);
    curvePos = curve.getCurrentPosition();
    curve.quadraticTo(curvePos.getX(), 2,
                      curvePos.getX() - cornerSize, 2);
    
    curve.lineTo({0.f, 2.f});
    curve.lineTo(0.f, 0.f);
    curve.lineTo(center.x, 0.f);
    curve.closeSubPath();
    
    g.setColour(Colour(97u, 18u, 167u));
    g.fillPath(curve);
    
    curve.applyTransform(AffineTransform().scaled(-1, 1));
    curve.applyTransform(AffineTransform().translated(getWidth(), 0));
    g.fillPath(curve);
    
    
    g.setColour(Colour(255u, 154u, 1u));
//    g.drawFittedText(title, bounds, juce::Justification::centredTop, 1);
    
    g.setColour(Colours::grey);
    g.setFont(14);
    g.drawFittedText("LowCut", LowFrequncyCutoffSlopeSlider.getBounds(), juce::Justification::centredBottom, 1);
    g.drawFittedText("Peak", TransferFunctionQualityFactorSlider.getBounds(), juce::Justification::centredBottom, 1);
    g.drawFittedText("HighCut", HighFrequencyCutoffSlider.getBounds(), juce::Justification::centredBottom, 1);
    
//    auto buildDate = Time::getCompilationDate().toString(true, false);
//    auto buildTime = Time::getCompilationDate().toString(false, true);
//    g.setFont(12);
//    g.drawFittedText("Build: " + buildDate + "\n" + buildTime, highCutSlopeSlider.getBounds().withY(6), Justification::topRight, 2);
}

void ProvaDSPAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop(4);
    
    auto analyzerEnabledArea = bounds.removeFromTop(25);
    
    analyzerEnabledArea.setWidth(50);
    analyzerEnabledArea.setX(5);
    analyzerEnabledArea.removeFromTop(2);
    
    analyzerEnabledButton.setBounds(analyzerEnabledArea);
    
    bounds.removeFromTop(5);
    
    float hRatio = 25.f / 100.f; //JUCE_LIVE_CONSTANT(25) / 100.f;
    auto responseArea = bounds.removeFromTop(bounds.getHeight() * hRatio); //change from 0.33 to 0.25 because I needed peak hz text to not overlap the slider thumb

    ProcessedCurveFourierTransformComponent.setBounds(responseArea);
    
    bounds.removeFromTop(5);
    
    auto lowCutArea = bounds.removeFromLeft(bounds.getWidth() * 0.33);
    auto highCutArea = bounds.removeFromRight(bounds.getWidth() * 0.5);
    
    ToogleLowPassButton.setBounds(lowCutArea.removeFromTop(25));
    LowFrequencyCutoffSlider.setBounds(lowCutArea.removeFromTop(lowCutArea.getHeight() * 0.5));
    LowFrequncyCutoffSlopeSlider.setBounds(lowCutArea);
    
    ToggleHighPassButon.setBounds(highCutArea.removeFromTop(25));
    HighFrequncyCutoffSlider.setBounds(highCutArea.removeFromTop(highCutArea.getHeight() * 0.5));
    HighFrequencyCutoffSlider.setBounds(highCutArea);
    
    ToggleCentralFrequncyButton.setBounds(bounds.removeFromTop(25));
    CentralFrequencySlider.setBounds(bounds.removeFromTop(bounds.getHeight() * 0.33));
    CentralFrequencyGainSlider.setBounds(bounds.removeFromTop(bounds.getHeight() * 0.5));
    TransferFunctionQualityFactorSlider.setBounds(bounds);
}

std::vector<juce::Component*> ProvaDSPAudioProcessorEditor::GetComponents()
{
    return
    {
        &CentralFrequencySlider,
        &CentralFrequencyGainSlider,
        &TransferFunctionQualityFactorSlider,
        &LowFrequencyCutoffSlider,
        &HighFrequncyCutoffSlider,
        &LowFrequncyCutoffSlopeSlider,
        &HighFrequencyCutoffSlider,
        &ProcessedCurveFourierTransformComponent,
        
        &ToogleLowPassButton,
        &ToggleCentralFrequncyButton,
        &ToggleHighPassButon,
        &analyzerEnabledButton
    };
}
