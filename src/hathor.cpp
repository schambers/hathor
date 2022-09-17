/*

Master knob: A8
Left Out: A9
Right Out: A10
Detune knob: A0
Shape knob: A1
Filter switch: A2
Filter knob: A3

*/

#include "daisysp.h"
#include "daisy_seed.h"

using namespace daisysp;
using namespace daisy;

static DaisySeed  hw;
static MoogLadder filter;
static Oscillator osc;
static Oscillator osc2;
static bool filterEnabled;

enum AdcChannel {
    masterKnob = 0,
    shapeKnob,
    detuneKnob,
    filterSwitch,
    filterKnob,
    NUM_ADC_CHANNELS
};

static void AudioCallback(AudioHandle::InterleavingInputBuffer  in,
                          AudioHandle::InterleavingOutputBuffer out,
                          size_t                                size)
{
    float sig, sig2, oscOutput, output;
    for(size_t i = 0; i < size; i += 2)
    {
        sig = osc.Process();
        sig2 = osc2.Process();

        oscOutput = sig + sig2;

        output = filterEnabled ? filter.Process(oscOutput) : oscOutput * 0.5;

        // left & right combined, mix the oscillators
        out[i] = out[i + 1] = output;
    }
}

// Return the inverted value of a pin
float getFloat(uint8_t pin) {
    return 1.0 - hw.adc.GetFloat(pin);
}

int main(void)
{
    // initialize seed hardware and oscillator daisysp module
    float sample_rate;
    hw.Configure();
    hw.Init();

    // Send log messages and enable serial messages
    hw.StartLog();

    AdcChannelConfig adcConfig[NUM_ADC_CHANNELS];
    adcConfig[masterKnob].InitSingle(seed::A8);
    adcConfig[detuneKnob].InitSingle(seed::A0);
    adcConfig[shapeKnob].InitSingle(seed::A1);
    adcConfig[filterSwitch].InitSingle(seed::A2);
    adcConfig[filterKnob].InitSingle(seed::A3);
    hw.adc.Init(adcConfig, NUM_ADC_CHANNELS);

    hw.SetAudioBlockSize(4);
    sample_rate = hw.AudioSampleRate();

    // TODO: move to function
    float shapeValue = 1.0 - hw.adc.GetFloat(shapeKnob);
    uint8_t mappedShape = fmap(shapeValue, 0, 5);

    // Oscillator 1
    
    osc.SetWaveform(mappedShape);
    osc.SetFreq(440);
    osc.SetAmp(getFloat(masterKnob));
    osc.Init(sample_rate);

    // Oscillator 2
    osc2.SetWaveform(mappedShape);
    osc2.SetFreq(400 * getFloat(detuneKnob));
    osc2.SetAmp(getFloat(masterKnob));
    osc2.Init(sample_rate);

    // MoogLadder Filter
    filter.Init(sample_rate);
    filter.SetRes(0.25);

    // Initialize the GPIO object
    // Mode: INPUT - because we want to read from the button
    // Pullup: Internal resistor to prevent needing extra components
    GPIO filterSwitch;
    filterSwitch.Init(seed::A2, GPIO::Mode::INPUT, GPIO::Pull::PULLUP);

    // start callback
    hw.adc.Start();
    hw.StartAudio(AudioCallback);

    while(1) {
        System::Delay(1000);

        // Master knob
        float ampValue = getFloat(masterKnob);
        osc.SetAmp(ampValue);
        osc2.SetAmp(ampValue);

        // Detune knob
        float detuneAmount = getFloat(detuneKnob);
        osc2.SetFreq(400 * detuneAmount);

        float shapeValue = 1.0 - hw.adc.GetFloat(shapeKnob);
        uint8_t mappedShape = fmap(shapeValue, 0, 5);
        osc.SetWaveform(mappedShape);
        osc2.SetWaveform(mappedShape);

        filterEnabled = !filterSwitch.Read();
        float filterFreq = fmap(getFloat(filterKnob), 0, 5000, Mapping::EXP);
        filter.SetFreq(filterFreq);

        //hw.Print("filterSwitchValue:" FLT_FMT3 "\n", FLT_VAR3(filterSwitchValue));
        hw.Print("filterSwitchValue:%d\n", filterSwitch.Read());
        hw.Print("filterEnabled:%d\n", filterEnabled);
        hw.Print("filterKnob:" FLT_FMT3 "\n", FLT_VAR3(filterFreq));
        //hw.Print("ampValue:" FLT_FMT3 "\n", FLT_VAR3(ampValue));
        //hw.Print("shapeValue (" FLT_FMT3 ") mappedShape %d)\n", FLT_VAR3(shapeValue), FLT_VAR3(mappedShape));
        //hw.Print("mappedShape:" FLT_FMT3 "\n", );
    }
}