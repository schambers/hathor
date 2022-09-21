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

#define NUM_VOICES 6

using namespace daisysp;
using namespace daisy;

static MidiUsbHandler midi;
static DaisySeed  hw;
static MoogLadder filter;
static Oscillator lfo;
static Chorus chorus;
static bool filterEnabled;
static bool lfoEnabled;
static ReverbSc DSY_SDRAM_BSS verb;

static Oscillator osc[NUM_VOICES];
static Adsr env[NUM_VOICES];
bool gate[NUM_VOICES];
uint8_t note[NUM_VOICES];

float oscFreq = 440;
float masterAmp;
float lfoFreq;
float lfoAmount;
float lfoOutput;
float filterFreq;
float verbDry;
float verbWet;
float detuneAmount;
uint8_t next;

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
    attackKnob,
    drKnob,
    verbAmountKnob,
    chrFreqKnob,
    chrAmountKnob,
    NUM_ADC_CHANNELS
};

static void AudioCallback(AudioHandle::InterleavingInputBuffer  in,
                          AudioHandle::InterleavingOutputBuffer out,
                          size_t                                size)
{
    float envOut, output, sigWet;
    for(size_t i = 0; i < size; i += 2)
    {
        // Default to filterFreq in case lfo is not enabled
        float filterMixFreq = filterFreq;
        lfoOutput = fmap(lfo.Process() + 0.5, 0, 5000);

        // Determine filter frequency, mixing in LFO parameters
        if (lfoEnabled)
        {
            filterMixFreq = filterFreq * (1 - lfoAmount) + lfoOutput * lfoAmount;
            filter.SetFreq(filterMixFreq);
        }

        for (uint8_t i = 0; i < NUM_VOICES; i++)
        {
            envOut = env[i].Process(gate[i]);
            osc[i].SetAmp(envOut);

            if (i == 0) {
                output = osc[0].Process();
            }
            else {
                output = output + osc[i].Process();
            }
        }

        output = filterEnabled ? filter.Process(output) * 3 : output;

        output = chorus.Process(output);
        verb.Process( output, 0, &sigWet, 0);

        // left & right combined from output and added reverb mix then add amp value for volume
        out[i] = out[i + 1] = (output * verbDry + sigWet * verbWet) * masterAmp;
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

    // Setup midi handler
    MidiUsbHandler::Config midi_cfg;
    midi_cfg.transport_config.periph = MidiUsbTransport::Config::INTERNAL;
    midi.Init(midi_cfg);

    // Send log messages and enable serial messages
    hw.StartLog();

    AdcChannelConfig adcConfig[NUM_ADC_CHANNELS];
    adcConfig[masterKnob].InitSingle(seed::A8);
    adcConfig[detuneKnob].InitSingle(seed::A0);
    adcConfig[shapeKnob].InitSingle(seed::A1);
    adcConfig[filterSwitch].InitSingle(seed::D14);
    adcConfig[filterKnob].InitSingle(seed::A3);
    adcConfig[resKnob].InitSingle(seed::A4);
    adcConfig[lfoSwitch].InitSingle(seed::D13);
    adcConfig[lfoRateKnob].InitSingle(seed::A6);
    adcConfig[lfoAmountKnob].InitSingle(seed::A7);
    adcConfig[attackKnob].InitSingle(seed::A5);
    adcConfig[drKnob].InitSingle(seed::A2);
    adcConfig[verbAmountKnob].InitSingle(seed::A9);
    adcConfig[chrFreqKnob].InitSingle(seed::A10);
    adcConfig[chrAmountKnob].InitSingle(seed::A11);
    hw.adc.Init(adcConfig, NUM_ADC_CHANNELS);

    hw.SetAudioBlockSize(4);
    sample_rate = hw.AudioSampleRate();

    // TODO: move to function
    float shapeValue = 1.0 - hw.adc.GetFloat(shapeKnob);
    uint8_t mappedShape = fmap(shapeValue, 0, 5);

    // Initialize Oscillators & Envelopes
    for (uint8_t i = 0; i < NUM_VOICES; i++) {
        osc[i].Init(sample_rate);
        osc[i].SetWaveform(mappedShape);
        osc[i].SetAmp(0.0f);

        env[i].Init(sample_rate);
        env[i].SetTime(ADSR_SEG_ATTACK, getFloat(attackKnob));
        env[i].SetTime(ADSR_SEG_DECAY, getFloat(drKnob));
	    env[i].SetTime(ADSR_SEG_RELEASE, getFloat(drKnob));
        env[i].SetSustainLevel(.25);
    }
    // Oscillator 1
    //osc.SetWaveform(mappedShape);
    //osc.Init(sample_rate);
    //osc.SetAmp(0.0f);

    // MoogLadder Filter
    filter.Init(sample_rate);
    filter.SetRes(getFloat(resKnob));

    // LFO that affects oscillators 1, 2
    lfo.SetWaveform(Oscillator::WAVE_SIN);
    lfo.SetFreq(0.4);
    lfo.SetAmp(0.0f);
    lfo.Init(sample_rate);

    verb.Init(sample_rate);
    verb.SetFeedback(0.95f);
    verb.SetLpFreq(16000.0f);

    chorus.Init(sample_rate);
    chorus.SetLfoFreq(getFloat(chrFreqKnob), .2f);
    chorus.SetLfoDepth(getFloat(chrAmountKnob), getFloat(chrAmountKnob));
    chorus.SetDelay(.75f, .9f);

    // Initialize the GPIO object
    // Mode: INPUT - because we want to read from the button
    // Pullup: Internal resistor to prevent needing extra components
    GPIO filterSwitch;
    GPIO lfoSwitch;
    filterSwitch.Init(seed::D14, GPIO::Mode::INPUT, GPIO::Pull::PULLUP);
    lfoSwitch.Init(seed::D13, GPIO::Mode::INPUT, GPIO::Pull::PULLUP);

    // start callback
    hw.adc.Start();
    hw.StartAudio(AudioCallback);

    while(1) {
        midi.Listen();


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

        detuneAmount = getFloat(detuneKnob);

        float chrFreq = getFloat(chrFreqKnob);
        float chrDepth = getFloat(chrAmountKnob);
        chorus.SetLfoFreq(chrFreq, (chrFreq * 0.9));
        chorus.SetLfoDepth(chrDepth, chrDepth); 

        uint8_t mappedShape = fmap(getFloat(shapeKnob), 0, 5);
        for (uint8_t i = 0; i < NUM_VOICES; i++) {
            osc[i].SetWaveform(mappedShape);
            env[i].SetTime(ADSR_SEG_ATTACK, getFloat(attackKnob));
	        env[i].SetTime(ADSR_SEG_RELEASE, getFloat(drKnob));
        }

        while(midi.HasEvents())
        {
            auto msg = midi.PopEvent();
            switch(msg.type)
            {
                case NoteOn:
                {
                    auto note_msg = msg.AsNoteOn();
                    if(note_msg.velocity != 0) {
                        next = (next + 1) % NUM_VOICES;
                        note[next] = note_msg.note;
                        osc[next].SetFreq(mtof(note_msg.note) );
                        gate[next] = true;
                        
                    }
                }
                break;
                case NoteOff:
                {
                    auto note_msg = msg.AsNoteOn();
                    for (uint8_t i = 0; i < NUM_VOICES; i++) {
                        if (note[i] == note_msg.note) {
                            note[i] = 0;
                            gate[i] = false;
                        }
                    }
                }
                break;
                default: break;
            }

            verbWet = (fmap(getFloat(verbAmountKnob), 0, 1));
            verbDry = (-1 + verbWet) * -1;

            // Update master knob
            masterAmp = getFloat(masterKnob);
        }

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