/*

Master knob     : A8
Left Out        : A9
Right Out       : A10
Detune knob     : A0
Shape knob      : A1
Filter switch   : A2
Filter knob     : A3
Filter Res knob : A4
LFO switch      : A5
LFO rate knob   : A6
LFO amount knob : A7

*/

#include "daisysp.h"
#include "daisy_seed.h"

using namespace daisysp;
using namespace daisy;

static DaisySeed  hw;
static MoogLadder filter;
static Oscillator osc, osc2, lfo;
static bool filterEnabled;
static bool lfoEnabled;

float oscFreq = 440;
float lfoFreq;
float lfoAmount;
float lfoOutput;
float filterFreq;

enum AdcChannel {
    masterKnob = 0,
    shapeKnob,
    detuneKnob,
    filterSwitch,
    filterKnob,
    resKnob,
    lfoSwitch,
    lfoRateKnob,
    lfoAmountKnob,
    NUM_ADC_CHANNELS
};

static void AudioCallback(AudioHandle::InterleavingInputBuffer  in,
                          AudioHandle::InterleavingOutputBuffer out,
                          size_t                                size)
{
    float sig, sig2, oscOutput, output;
    for(size_t i = 0; i < size; i += 2)
    {
        // Default to filterFreq in case lfo is not enabled
        float filterMixFreq = filterFreq;

        lfoOutput = fmap(lfo.Process() + 0.5, 0, 5000);

        // Determine filter frequency, mixing in LFO parameters
        if (lfoEnabled) {
            filterMixFreq = filterFreq * (1 - lfoAmount) + lfoOutput * lfoAmount;
            filter.SetFreq(filterMixFreq);
        }

        // Determine oscillator 1, 2 frequencies, mixing in LFO parameters
        // TODO: this is a bit much, may need to revisit later
        //, osc1MixFreq, osc2MixFreq;
        // float detuneAmount = 1 - hw.adc.GetFloat(detuneKnob);
        // osc1MixFreq = oscFreq * (1 - lfoAmount) + lfoOutput * lfoAmount;
        // osc2MixFreq = (oscFreq * detuneAmount) * (1 - lfoAmount) + lfoOutput * lfoAmount;
        // osc.SetFreq(osc1MixFreq);
        // osc2.SetFreq(osc2MixFreq);

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
    adcConfig[resKnob].InitSingle(seed::A4);
    adcConfig[lfoSwitch].InitSingle(seed::A5);
    adcConfig[lfoRateKnob].InitSingle(seed::A6);
    adcConfig[lfoAmountKnob].InitSingle(seed::A7);
    hw.adc.Init(adcConfig, NUM_ADC_CHANNELS);

    hw.SetAudioBlockSize(4);
    sample_rate = hw.AudioSampleRate();

    // TODO: move to function
    float shapeValue = 1.0 - hw.adc.GetFloat(shapeKnob);
    uint8_t mappedShape = fmap(shapeValue, 0, 5);

    // Oscillator 1
    osc.SetWaveform(mappedShape);
    osc.SetFreq(oscFreq);
    osc.SetAmp(getFloat(masterKnob));
    osc.Init(sample_rate);

    // Oscillator 2
    osc2.SetWaveform(mappedShape);
    osc2.SetFreq(oscFreq * getFloat(detuneKnob));
    osc2.SetAmp(getFloat(masterKnob));
    
    osc2.Init(sample_rate);

    // MoogLadder Filter
    filter.Init(sample_rate);
    filter.SetRes(getFloat(resKnob));

    // LFO that affects oscillators 1, 2
    lfo.SetWaveform(Oscillator::WAVE_SIN);
    lfo.SetFreq(0.4);
    lfo.SetAmp(0.2);
    lfo.Init(sample_rate);

    // Initialize the GPIO object
    // Mode: INPUT - because we want to read from the button
    // Pullup: Internal resistor to prevent needing extra components
    GPIO filterSwitch;
    filterSwitch.Init(seed::A2, GPIO::Mode::INPUT, GPIO::Pull::PULLUP);

    GPIO lfoSwitch;
    lfoSwitch.Init(seed::A5, GPIO::Mode::INPUT, GPIO::Pull::PULLUP);

    // start callback
    hw.adc.Start();
    hw.StartAudio(AudioCallback);

    while(1) {        
        //System::Delay(1000);

        // Master knob
        float ampValue = getFloat(masterKnob);
        osc.SetAmp(ampValue);
        osc2.SetAmp(ampValue);

        // Detune knob
        osc2.SetFreq(oscFreq * getFloat(detuneKnob));

        uint8_t mappedShape = fmap(getFloat(shapeKnob), 0, 5);
        osc.SetWaveform(mappedShape);
        osc2.SetWaveform(mappedShape);

        filterEnabled = !filterSwitch.Read();
        float filterRes = fmap(getFloat(resKnob), 0, 0.8);
        filter.SetRes(filterRes);
        filterFreq = fmap(getFloat(filterKnob), 0, 5000); // This is now set dynamically during AudioCallback depending on LFO amount/rate
        // TODO, play with the mapping curve for different filter applications

        // LFO updates
        lfoEnabled = !lfoSwitch.Read();
        lfoFreq = fmap(getFloat(lfoRateKnob), 0, 64);
        lfo.SetFreq(lfoFreq);
        lfoAmount = getFloat(lfoAmountKnob);

        //hw.Print("filterSwitchValue:" FLT_FMT3 "\n", FLT_VAR3(filterSwitchValue));
        //hw.Print("filterSwitchValue:%d\n", filterSwitch.Read());
        //hw.Print("filterEnabled:%d\n", filterEnabled);
        //hw.Print("filterKnob:" FLT_FMT3 "\n", FLT_VAR3(filterFreq));
        //hw.Print("lfoAmount:" FLT_FMT3 "\n", FLT_VAR3(lfoAmount));
        //hw.Print("ampValue:" FLT_FMT3 "\n", FLT_VAR3(ampValue));
        //hw.Print("resValue:" FLT_FMT3 "\n", FLT_VAR3(getFloat(resKnob)));
        //hw.Print("lfoRate:" FLT_FMT3 "\n", FLT_VAR3(getFloat(lfoRateKnob)));
        //hw.Print("lfoOutput:" FLT_FMT3 "\n", FLT_VAR3(lfoOutput));
        //hw.Print("shapeValue (" FLT_FMT3 ") mappedShape %d)\n", FLT_VAR3(shapeValue), FLT_VAR3(mappedShape));
        //hw.Print("mappedShape:" FLT_FMT3 "\n", );
    }
}

// Midi processing
// In headers
// static MidiUsbHandler midi;
// =================================
// In main setup loop:
// // Setup Midi over USB
// MidiUsbHandler::Config midi_cfg;
// midi_cfg.transport_config.periph = MidiUsbTransport::Config::INTERNAL;
// midi.Init(midi_cfg);
// =================================
// in loop
// midi.Listen();
// while(midi.HasEvents())
// {
//     auto msg = midi.PopEvent();
//     switch(msg.type)
//     {
//         case NoteOn:
//         {
//             auto note_msg = msg.AsNoteOn();
//             if(note_msg.velocity != 0) {
//                 osc.SetFreq(mtof(note_msg.note));
//                 osc2.SetFreq(mtof(note_msg.note) * getFloat(detuneKnob));
//             }
                
//         }
//         break;
//         default: break;
//     }
// }