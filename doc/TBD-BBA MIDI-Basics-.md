**MIDI-concepts of TBD-BBA**

In contrast to the Eurorack- or AE Modular-version of the TBD, the TBD BBA (Black Box Audio) focuses on MIDI as input to control the various instruments and effects available for the TBD-platform. MIDI can be applied either via the classical 5-pin DIN-jacks or via USB.

As with the classical TBD, instruments and effects are, due to the character of their technical integration, also known as *Plugins* and can be mono or stereo. In mono operation-mode, two plugins can be used at once\!   
The TBD-BBA is backwards-compatible to the original TBD in many ways, so that you still can use the instruments and effects that previously had been designed for a module controlled by Gates, Triggers and CVs (control voltages) in mind. 

Basically the TBD-BBA is a TBD with a builtin MIDI-to-CV/Gate interface including different keyboard-modes to facilitate the aggregation of functional groups to voices.  
In case you are not familiar with the concept of CV/Gate, you may want to look here first:  
**https://en.wikipedia.org/wiki/CV/gate**

***Virtual CVs, Triggers and Gates:***

* Triggers or Gates are symbolized as switches on the Web-GUI  
* CVs are symbolized as sliders on the Web-GUI  
    
  When you open up the dropdown-listbox associated with any switch or slide of the Web-GUI, you’ll see a list of available MIDI-parameters for individual mappings.  
  ![][image1]     ![][image2]

	The available parameters that can be mapped as virtual Gates/Triggers or CVs are slightly different. Basically the MIDI-parameters for Gates are a subset of the parameters for CV, according to their original nature in the “MIDI-world”.  
Please note: some mappings of Voice A also are available with the Percussion voice.

***MIDI-Channels:***

* Midi-Channels have two main functions regarding the TBD-BBA:  
  * Switching Voice-modes  
  * Context-sensitive filtering of “CVs” and “Gates” for mapping of parameters

* The following voice-modes are available:  
  * Mono: Channel 1 (mimics the behavior of a classical mono-synth)  
  * Duophonic-AB: Channel 14  
  * Duophonic-CD: Channel 15  
  * Polyphonic (4-voice) Channel 16

For more details please refer to the *“Table of features per channel”* below\!  
Individual voices on Channels 2-5 (plus 6-9 and 10-13) optionally to be used via MPE  
For a good overview on MPE please refer to: **https://studiocode.dev/resources/mpe**  
***MIDI-EventTypes:***  
The following MIDI-Events are recognized:

* Note on (including pitch and velocity)  
* Note off   
* Pitch bend (bipolar or unipolar with logarithmic scaling)  
* Channel Pressure (aka Aftertouch)  
* Continuous Controls (aka CCs)  
* Bank Change, Subbank Change (CC 0 or CC 32\)  
* Program Change


***Note:*** Velocity only applies to note-ons, so you have to press several times for on/off actions.

***“All sounds/notes off”*** (CC 120/123) generates “gate off” for all notechannels A,B,C,D  
For more details please refer to *“Table of available MIDI-Parameters and features”* below\!

***Functional Groups, “Voices”:***  
There are 5 groups (“Slots”), with Prefixes G and A-D. A-D has the layout of mappable parameters as with A\_ below, Global parameters are slightly different, also shown below.  
Globals typically are associated with MIDI channel 1, “Voices” A-D typically with channel 2-5.   
             
           ![][image3]   ![][image4]           ![][image5] ![][image6]

Please note that the switching of voice modes is applied prior to the MIDI-Event mapping.  
So effectively, depending on the voice mode on any of the MIDI-Channels 1, 14, 15 or 16 MIDI-notes will be distributed as any of the combinations below: 

* *CH-1: Monophonic:* 		A\_NOTE, A\_VELO   
* *CH-14: Duophonic-AB:* 	A\_NOTE, A\_VELO, B\_NOTE, B\_VELO   
* *CH-15: Duophonic-CD:* 	C\_NOTE, C\_VELO, D\_NOTE, D\_VELO   
* *CH-16: Polyphonic:* 		A\_NOTE, A\_VELO, B\_NOTE, B\_VELO, 

  C\_NOTE, C\_VELO, D\_NOTE, D\_VELO 

Global Parameters (Prefix *‘G\_’*) are also recognized for those voice modes and thus are applied on channels 1, 14, 15 or 16\.  
If you want to send notes to be associated with the “voices” on ‘A\_’, ‘B\_’, ‘C\_’ or ‘D\_’ directly you can do so by using the MIDI channels 2-5 (or 6-9 and 10-13 respectively).  
**Please note:** Some controls are mappable via Voice A or the Percussion-voice as well.

Of course you do not have to stick to the conventions used here to construct a voice-context.  
In other words, you can make use of the parameters with prefixes G or A-D with their associated MIDI-channels (typically 1 and 2-5) in any way that makes sense according to the use-case you want to achieve\!    
***Switching of Favourites:***  
On the global channels (1,14,15,16) Bank- and Subbank-selects or Program Changes are not GUI-mappable but instead Program Change events can be used to select favourites.   
Bankselect messages will be ignored in that context \- meaning that if a DAW or any other kind of MIDI controller forces the sending of Bankselect events along with a program change, only the latter will be used to decide which Favorite is to be taken.  
Favorites effectively are a quick access combination of 2 plugins and their presets.

Favorites are numbered as 0-9. The Program Change numbers will address those numbers with a modulo 10 logic, meaning Program Change 1-9 will address favorites 1-9. Program change 10 will trigger Favorite 0 and so on (Prg 11-19:  Favorite 1-9, Prg 20: Favorite 0, …).

Example of possible MIDI Parameter-Mappings for a TBD-Plugin 

For Plugin-Details see Documentation of WTOscDuo

![][image7]

This is a typical mapping of notes for the Duophonic-AB voicemode. 

Mappings can be applied more than once, so for instance in the context as above, G\_MW\_1 may be mapped to “Wave 1” and “Wave 2” at once, so that turning the modulation-wheel on your keyboard would change the current wave of the wavetable for both voices / oscillators. 

TBD BBA: Table of voicemodes and resulting mapping options of parameters

| Incoming MIDI-Channel | Resulting Voicemode | Distribution to Notes-Slots | Distribution to Controls-Slots |
| ----- | ----- | ----- | ----- |
|  |  |  |  |
| 1 | Monophonic | A | G |
|  |  |  |  |
| 2,6 (10) | Individual / MPE | A | A |
| 3,7 (11) | Individual / MPE | B | B |
| 4,8 (12) | Individual / MPE | C | C |
| 5,9 (13) | Individual / MPE | D | D |
|  |  |  |  |
| 10 | Percussion | P | (P) |
|  |  |  |  |
| 14 | Duophonic-AB | A📥 📤B | G |
|  |  |  |  |
|  |  |  |  |
| 15 | Duophonic-CD | C📥 📤D | G |
|  |  |  |  |
|  |  |  |  |
| 16 | 4-Voice Polyphonic | A📥 📤B📥 📤C📥 📤D | G |
|  |  |  |  |
|  |  |  |  |
|  |  |  |  |

***Please note:***   
Usage of Voice Modes, Notes-Slots and Controls-Slots is not mutually exclusive\!  
You can take advantage of that by combining MIDI-channels with your controller or DAW.  
For instance you may want to add controls (apart from MIDI-notes) by mapping them via lets say slot B to monophonic voice mode. To do so you would need to combine MIDI channels 1 and 3\. 

Or if you need to combine e.g. the Duophonic-CD voice mode with even more controls that are not part of the globals-section, you could add controls via let’s say MIDI-Channel 2, that would allow you to map those non-notes controls to slot A and then would end up combining MIDI-Channel 15 and 2 as your MIDI-input.

Only MIDI channels 2-5 are native MPE-channels, channels 6-9 may be used additionally at own risk. Channels 10-13 may work here as well, but are reserved for future use, but notes on channel 10 will only be recognized as percussion triggers and velocities\!   
The basic “control”-type associated with the Percussion voice is velocity, which of course effectively is sent as a characteristics of the identical note that is used as a trigger already\!   
Percussion Channel (CH-P)

The 16 MIDI-Notes 36-51, incoming on MIDI Channel 10 can be mapped as Note-Triggers and/or Velocity-CVs (list see below). In contrast to the (melodic) Voices A-D, the (percussive) Voice P does not provide mapping of note-pitch to CV, but allows multiple Triggers and Velocity-CVs via this voice\! These notes are equivalent to the Notes with pitches C1 to D\#2, below is a typical visualisation of a MPC-style 4x4 matrix as e.g. to be found in Ableton Live:

![][image8]        ![][image9]

**The Percussion-Channel Notes are available combined with CCs of the Voices A-D as:**  
A\_75\_P\_C1, A\_76\_P\_C\#1, A\_77\_P\_D1, A\_78\_P\_D\#1  
B\_75\_P\_E1, B\_76\_P\_F1, B\_77\_P\_F\#1, B\_78\_P\_G1  
C\_75\_P\_G\#1, C\_76\_P\_A1, C\_77\_P\_A\#1, C\_78\_P\_B1  
D\_75\_P\_C2, D\_76\_P\_C\#2, D\_77\_P\_D2, D\_78\_P\_D\#2

**Tipp:** Such triggers also can be handy as clock-triggers for plugins that require such events\!

Below you can see the Note-numbers, names and typical mappings according to GM   
(General MIDI standard), which are of course optional with the TBD BBA.

| MIDI-Note-Number | Note-Value | MIDI GM Name |
| ----- | ----- | :---- |
| 36 | C1 | Bass Drum 1 |
| 37 | C\#1 | Side Stick |
| 38 | D1 | Acoustic Snare |
| 39 | D\#1 | Hand Clap |
| 40 | E1 | Electric Snare |
| 41 | F1 | Low Floor Tom |
| 42 | F\#1 | Closed Hi Hat |
| 43 | G1 | High Floor Tom |
| 44 | G\#1 | Pedal Hi-Hat |
| 45 | A1 | Low Tom |
| 46 | A\#1 (Bb1) | Open Hi-Hat |
| 47 | B1 | Low-Mid Tom |
| 48 | C2 | Hi Mid Tom |
| 49 | C\#2 | Crash Cymbal |
| 50 | D2 | High Tom |
| 51 | D\#2 | Ride Cymbal |

TBD BBA: Table of available MIDI-Parameters and features

| *No* | *Voice \+ Function* | *MIDI chan* | *Control- Type* | *Can be trigger?* | *Comment / Special Option* |
| ----- | :---- | :---- | :---- | :---- | :---- |
| 1 | A\_NOTE | 2 (6) | note (pitch) | y |  |
| 2 | A\_VELO | 2 (6) | note (velo) | y |  |
| 3 | A\_BANK | 2 (6) | continuous | n | value 1 \=\> 0 |
| 4 | A\_SBNK | 2 (6) | continuous | n | value 1 \=\> 0 |
| 5 | A\_PRG | 2 (6) | progchng | y |  |
| 6 | A\_PB | 2 (6) | pitchbend | n | bipolar |
| 7 | A\_PB\_LG | 2 (6) | pitchbend | n | logarithithmic LUT |
| 8 | A\_AT | 2 (6) | aftertouch | y |  |
| 9 | A\_MW\_1 | 2 (6) | continuous | n |  |
| 10 | A\_BC\_2 | 2 (6) | continuous | n |  |
| 11 | B\_NOTE | 3 (7) | note (pitch) | y |  |
| 12 | B\_VELO | 3 (7) | note (velo) | y |  |
| 13 | B\_BANK | 3 (7) | continuous | n | value 1 \=\> 0 |
| 14 | B\_SBNK | 3 (7) | continuous | n | value 1 \=\> 0 |
| 15 | B\_PRG | 3 (7) | progchng | y |  |
| 16 | B\_PB | 3 (7) | pitchbend | n | bipolar |
| 17 | B\_PB\_LG | 3 (7) | pitchbend | n | logarithmic LUT |
| 18 | B\_AT | 3 (7) | aftertouch | y |  |
| 19 | B\_MW\_1 | 3 (7) | continuous | n |  |
| 20 | B\_BC\_2 | 3 (7) | continuous | n |  |
| 21 | C\_NOTE | 4 (8) | note (pitch) | y |  |
| 22 | C\_VELO | 4 (8) | note (velo) | y |  |
| 23 | C\_BANK | 4 (8) | continuous | n | value 1 \=\> 0 |
| 24 | C\_SBNK | 4 (8) | continuous | n | value 1 \=\> 0 |
| 25 | C\_PRG | 4 (8) | progchng | y |  |
| 26 | C\_PB | 4 (8) | pitchbend | n | bipolar |
| 27 | C\_PB\_LG | 4 (8) | pitchbend | n | logarithmic LUT |
| 28 | C\_AT | 4 (8) | aftertouch | y |  |
| 29 | C\_MW\_1 | 4 (8) | continuous | n |  |
| 30 | C\_BC\_2 | 4 (8) | continuous | n |  |
| 31 | D\_NOTE | 5 (9) | note (pitch) | y |  |
| 32 | D\_VELO | 5 (9) | note (velo) | y |  |
| 33 | D\_BANK | 5 (9) | continuous | n | value 1 \=\> 0 |
| 34 | D\_SBNK | 5 (9) | continuous | n | value 1 \=\> 0 |
| 35 | D\_PRG | 5 (9) | progchng | y |  |
| 36 | D\_PB | 5 (9) | pitchbend | n | bipolar |
| 37 | D\_PB\_LG | 5 (9) | pitchbend | n | logarithmic LUT |
| 38 | D\_AT | 5 (9) | aftertouch | y |  |
| 39 | D\_MW\_1 | 5 (9) | continuous | n |  |
| 40 | D\_BC\_2 | 5 (9) | continuous | n |  |
| 41 | A\_RES\_71 | 2 (6) | continuous | n |  |
| 42 | A\_REL\_72 | 2 (6) | continuous | n |  |
| 43 | A\_ATK\_73 | 2 (6) | continuous | n |  |
| 44 | A\_CUT\_74 | 2 (6) | continuous | n |  |
| 45 | A\_75\_P\_C1 | 2 (6) | continuous | y | PercussionNote |
| 46 | A\_76\_P\_C\#1 | 2 (6) | continuous | y | PercussionNote |
| 47 | A\_77\_P\_D1 | 2 (6) | continuous | y | Percussion-Note |
| 48 | A\_78\_P\_D\#1 | 2 (6) | continuous | y | Percussion-Note |
| 49 | B\_RES\_71 | 3 (7) | continuous | n |  |
| 50 | B\_REL\_72 | 3 (7) | continuous | n |  |
| 51 | B\_ATK\_73 | 3 (7) | continuous | n |  |
| 52 | B\_CUT\_74 | 3 (7) | continuous | n |  |
| 53 | B\_75\_P\_E1 | 3 (7) | continuous | y | Percussion-Note |
| 54 | B\_76\_P\_F1 | 3 (7) | continuous | y | Percussion-Note |
| 55 | B\_77\_P\_F\#1 | 3 (7) | continuous | y | Percussion-Note |
| 56 | B\_78\_P\_G1 | 3 (7) | continuous | y | Percussion-Note |
| 57 | C\_RES\_71 | 4 (8) | continuous | n |  |
| 58 | C\_REL\_72 | 4 (8) | continuous | n |  |
| 59 | C\_ATK\_73 | 4 (8) | continuous | n |  |
| 60 | C\_CUT\_74 | 4 (8) | continuous | n |  |
| 61 | C\_75\_P\_G\#1 | 4 (8) | continuous | y | Percussion-Note |
| 62 | C\_76\_P\_A1 | 4 (8) | continuous | y | Percussion-Note |
| 63 | C\_77\_P\_A\#1 | 4 (8) | continuous | y | Percussion-Note |
| 64 | C\_78\_P\_B1 | 4 (8) | continuous | y | Percussion-Note |
| 65 | D\_RES\_71 | 5 (9) | continuous | n |  |
| 66 | D\_REL\_72 | 5 (9) | continuous | n |  |
| 67 | D\_ATK\_73 | 5 (9) | continuous | n |  |
| 68 | D\_CUT\_74 | 5 (9) | continuous | n |  |
| 69 | D\_75\_P\_C2 | 5 (9) | continuous | y | Percussion-Note |
| 70 | D\_76\_P\_C\#2 | 5 (9) | continuous | y | Percussion-Note |
| 71 | D\_77\_P\_D2 | 5 (9) | continuous | y | Percussion-Note |
| 72 | D\_78\_P\_D\#2 | 5 (9) | continuous | y | Percussion-Note |
| 73 | G\_PB | 1,14,15,16 | pitchbend | n | bipolar |
| 74 | G\_PB\_LG | 1,14,15,16 | pitchbend | n | logarithmic LUT |
| 75 | G\_AT | 1,14,15,16 | aftertouch | y |  |
| 76 | G\_MW\_1 | 1,14,15,16 | continuous | n |  |
| 77 | G\_BC\_2 | 1,14,15,16 | continuous | n |  |
| 78 | G\_FOOT\_4 | 1,14,15,16 | continuous | n |  |
| 79 | G\_DAT\_6 | 1,14,15,16 | continuous | n |  |
| 80 | G\_VOL\_7 | 1,14,15,16 | continuous | n |  |
| 81 | G\_BAL\_8 | 1,14,15,16 | continuous | n | bipolar |
| 82 | G\_PAN\_10 | 1,14,15,16 | continuous | n | bipolar |
| 83 | G\_XPR\_11 | 1,14,15,16 | continuous | n |  |
| 84 | G\_FX1\_12 | 1,14,15,16 | continuous | y |  |
| 85 | G\_FX2\_13 | 1,14,15,16 | continuous | y |  |
| 86 | G\_SUST\_64 | 1,14,15,16 | continuous | y |  |
| 87 | G\_PORT\_65 | 1,14,15,16 | continuous | y |  |
| 88 | G\_SSTN\_66 | 1,14,15,16 | continuous | y |  |
| 89 | G\_SOFT\_67 | 1,14,15,16 | continuous | y |  |
| 89 | G\_HOLD\_69 | 1,14,15,16 | continuous | y |  |
| 90 | G\_HOLD\_69 | 1,14,15,16 | continuous | y |  |

> **Note:** The TBD BBA firmware extends the CV slot range with 10 virtual ModEngine slots (indices 90-99). Slots 90-97 are ModEngine dynamic CC outputs (CC1..CC8), and slots 98-99 are LFO1/LFO2 outputs. These appear in the UI's modulation source dropdowns (MODE_MAP and PANEL_MOD) but are not driven by MIDI — they are generated internally at audio block rate.

TBD BBA: Table of features per channel

| *Input-Channel / VoiceMode-Selector* | *Voice, VoiceMode, Distribution Logic* | *Comment* |
| :---- | :---- | :---- |
| CH-01 | A \- Monophonic: Map notes with legato+lowkey-prio to Voice-A | For Monosynths, similar to Moog Minimoog: low note priority, keyed legato-option |
| CH-02 | A \- Individual / MPE: Route monophonically to Voice-A | Lastnote-priority, only one note will be routed |
| CH-03 | B \- Individual / MPE: Route monophonically to Voice-B | Lastnote-priority, only one note will be routed |
| CH-04 | C \- Individual / MPE: Route monophonically to Voice-C | Lastnote-priority, only one note will be routed |
| CH-05 | D \- Individual / MPE: Route monophonically to Voice-D | Lastnote-priority, only one note will be routed |
| CH-06 | *(A \- Individual / MPE: Route monophonically to Voice-A)* | *Reduce voices of MPE to 4, caution: this may cause collisions\!* |
| CH-07 | *(B \- Individual / MPE: Route monophonically to Voice-B)* | *This may behave differently/not as intended when used with untested MPE-Controllers.* |
| CH-08 | *(C \- Individual / MPE: Route monophonically to Voice-B)* | *Collisions may also occur for CC74, Pitchbend and Pressure...* |
| CH-09 | *(D \- Individual / MPE: Route monophonically to Voice-C)* | *...when we have to reduce voices for an MPE-controller\!* |
| CH-10 | Percussion Channel \- 16 Notes (36-51) can be mapped | Percussion-mappings similar to MIDI GM may be used for convenience. ***Non-Note data will be mapped to Ch. 2 for now, but this may become obsolete in the future\!*** |
| CH-11 | *(Reserverved for possible future usage)* | *Will be mapped to channel 3 for now, but this may become obsolete in the future\!* |
| CH-12 | *(Reserverved for possible future usage)* | *Will be mapped to channel 4 for now, but this may become obsolete in the future\!* |
| CH-13 | *(Reserverved for possible future usage)* | *Will be mapped to channel 5 for now, but this may become obsolete in the future\!* |
| CH-14 | A,B \- Duophonic: Distribute notes to A and B | For Duophonic Synths, using low note priority \+ "roundrobin" with special legato |
| CH-15 | C,D \- Duophonic: Distribute notes to C and D | For Duophonic Synths, also using low note priority \+ "roundrobin" with special legato |
| CH-16 | A,B,C,D \- Polyphonic: Distribute notes to A,B,C,D | For Polysynths up to 4 voices, also using low note priority \+ "roundrobin" with special legato |
|   |  |  |
| ***Channels per Event-Classes*** | ***Events according to Voicemodes (Mode-Details see above)*** | ***Comment*** |
| CH-01 | Note on, Note off (including velocity) | Monophonic Note-Mapping to Voice A, also Masterchannel for primary MPE voicerange |
| CH-01 \+ CH-14, CH-15, CH-16 | G\_\*: Global Pitchbend, Modwheel, Sustain-Pedal and Volume-CC | Available on WebGUI for assignment as G\_PB, G\_MW, G\_SUST, G\_VOL |
| CH-02 \- CH-05 | No mappings, passes through unfiltered\! | MPE or individual voice-channels, e.g. for sequencers and alike |
| CH-06 \- CH-09 | Note on, Note off (including velocity) PB, AT, CC74 | MPE, additional voices to be reduced to max. four\! |
| CH-10 | Note on, Note off (including velocity) note-selective, uses MIDI-notes 36-51 | This typically may be used percussions: P36-P51 are combined with A\_75-A\_78...D\_75-D\_79 |
| CH-14 | Note on, Note off (including velocity) PB, AT, CC74 | Duophonic Note-Mapping and also MPE for controls in case CH-14 is transmitted... |
| CH-15 | Note on, Note off (including velocity) PB, AT, CC74 | Duophonic Note-Mapping and also MPE for controls in case CH-15 is transmitted... |
| CH-16 | Note on, Note off (including velocity) | Polyphonic (4-voice) Note-Mapping |
|  |  |  |
| ***Channels per Event-Classes*** | ***Events to be associated with entries from GUI-List*** | ***Comment*** |
| CH-01 \+ CH-14, CH-15, CH-16 | G\_\* Entries will be recognized | Exactly the 4 defined G\_\* Events when from Channel-1 will be processed, |
| CH-02 \- CH-05 | A\_\* \- D\_\* Entries will be recognized | all events that can't be associated to the GUI-List are simply ignored\! |
|  |  |  |
| ***Logarithmic PitchBends*** | ***Especially useful if mapped to Cutoff or similar (HiRes+Log.)*** | ***Comment*** |
| CH-02 \- CH-05 (CH 03-CH 09\) | In addition to normal pitchbends logarithmically scaled unipolar is available | In contrast to normal pitchbends logarithmically scaled ones are not bipolar |
| CH-01, CH-14, CH-15, CH-16 | In addition to pitchbends for Ch-1+14,15,16, Logarithmic pitchbend provided | Exclusively logarithmic on CH-14, CH-14 is MPE for Voice B otherwise |
|   |  |  |
| ***Polyphonic Legato*** | ***Special handling for Duophonic and 4-voice Polyphonic*** | ***Comment*** |
| CH-14 | *Duophonic: Legago may occur when more than 2 notes played* | If a playing note is released, and more than 2, respectively 4 notes are pressed, ... |
| CH-15 | *Duophonic: Legago may occur when more than 2 notes played* | ...the lowest not yet sounding additional note will play, but no EG will be triggered. |
| CH-16 | *Polyphonic: Legago may occur when more than 2 notes played* | Like Duophonic also low-note-priority, round-robin voice distribution and legato on release. |
|  |  |  |
| ***Global Channels Mappings*** | ***Special handling for Duophonic and 4-voice Polyphonic*** | ***Comment*** |
| CH-01 (notes mapped to Voice-A) | *Monophonic: Main Master \- Events from Channel 16 or 15 work too* | Channel 14, 15 and 16 are equivilently handled as Channel 1, but with different Voice-Modes |
| CH-14 (notes mapped to Voice-A+B) | *Duophonic: PitchBend, ModW., Sustain etc. also available as G\_\** | Duophonic, round-robin voice-distribution, low-note priority, legato on release where possible |
| CH-15 (notes mapped to Voice-C+D) | *Duophonic: PitchBend, ModW., Sustain etc. also available as G\_\** | Same as above, but mappted to C and D instead of A and B |
| CH-16 (notes mapped to Voice A-D) | *Polyphonic: PitchBend, ModW., Sustain etc. also available as G\_\** | Voice-Logic similar to Duophonic \- Channel 16 is secondary Master for MPE anyhow |
|  |  |  |
| ***Percussion Channels Mappings*** | ***Special handling for 16 Percussion/Drum sound notes and velocities*** | ***Comment*** |
| CH-10 (notes mapped via Voice-P) | *Notes of Voice-P are to be found as doublicates to CCs with Voices A-D* | Percussion-CH 10 only allows MIDI-notes 36-51, but every note can trigger a different sound |

Usage of MPE Controllers

First of all, it is important to know that ***Polyphonic Aftertouch is not supported***. Polyphonic Aftertouch still can be found on some old keyboards, but usually not on modern MPE controllers, so it should be no issue that this concept in general is not compatible with the TBD BBA. In terms of MPE controllers, any device making use of MIDI Channel 1 as the Master Channel and at least MIDI channels 2-5 as Member Channels for the Lower Zone should work.

In principle MPE Master-Channel events can be understood via the TBD BBA Global channels 1,14,15 or 16\. 16 typically being the Master-Channel of the Upper Zone, though.   
It would be ideal, if Member Channels from your MPE-controller can be restricted to the aforementioned channels 2-5, or at least restrict the Lower Zone to 8 voices, meaning member-channels would be 2-9 which in return can be understood by the TBD BBA voices A, B, C and D. Upper Zone Member Channels are not supported by the TBD BBA.

Release-Velocity and RPNs will not be processed by the TBD BBA\! But there are plenty of mappable controls to be set globally or with the voice-channels to achieve similar results.

**Excerpts from “MIDI Polyphonic Expression”, Version 1.0, March-12-2018**   
**by “The MIDI Association”:**

**Overview:**

* Pitch Bend is, by default, set to a range of ±48 semitones for per-note bend and ±2 semitones for Master bend. Either range may be changed to a number of semitones between 0 and ±96 using RPN  
    
* Aftertouch is sent using the Channel Pressure message.  
     
* In addition to being able to express per-note pitch (Pitch Bend) and pressure (Channel Pressure), a third dimension of per-note control may be expressed using MIDI CC \#74.


**Terminology:**

* Lower Zone. The Zone that encompasses Master Channel 1, and is allocated MIDI Channels increasing from Channel 2\.  
    
* Member Channel. Any MIDI Channel within a Channel Zone that is not a Master Channel.  
    
* Master Channel. A MIDI Channel reserved for conveying messages that apply to the entire Zone.

A link to the MPE-Specification and further information on the topic also can be found here:  
https://support.roli.com/support/solutions/articles/36000027933-what-is-mpe-

## Example use-case duo/polyphonic wavetable oscillator 

## using two **WTOscDuo** instances

### Description

Two voiced version of the plugin called WTOsc. For additional information on the features, thus you may want to have a look at the documentation of WTOsc, too.

The special feature of WTOscDuo is that it can be used duophonically or even as a four voice synth.  
For duophonic mode you can put it in any of the two slots for plugins: *Plugin Channel 0* or *Plugin Channel 1\.* To achieve four-voice polyphony, you need to put WTOscDuo into both Plugin slots.

Using the dropdownlist of assignable MIDI-parameters there are 5 categories available, symbolized by prefixes to the parameters: G (global) or A-D (voices). For more information on the basic concepts of MIDI-mappings for the TBD please refer to the chapter ***“MIDI-concepts of TBD-BBA”***

![][image10]   ![][image11]          ![][image3]   ![][image12]

For duophonic you have to use the voices A and B, which are recognized via MIDI-channels 2 and 3 directly (e.g. with MPE controller) or distributed accordingly via the voice mode *“Duophonic-AB”* which has to be applied via MIDI-channel 14\. The most basic mapping would be to assign notes and pitches like below. There is a preset available called *“DuoPhonic\_AB”* utilizing the exact same method.

![][image13]  
   
You could make use of WTOscDuo as two separate duophonic synths. For this, you load another instance of WTOscDuo to the plugin slot 1 and map to C- and D-voices via MIDI channels 4 and 5 or using the *“Duophonic-CD”* voice mode via MIDI-channel 15\. The basic notes-mapping, for instance using the preset *“DuoPhonic\_CD”* then need to be like this:

![][image14]

Finally to use WTOscDuo as a four-voice synth you simply would use the *“4-Voice”* polyphonic voice- mode via MIDI-channel 16 which in results triggers both instances assigned to A-D\!

The parameters as below are available. Parameters that make sense to be mapped individually to any of the available two distinctive channels per plugin are recognizable via a ***‘ 1’*** **respectively *‘ 2’* suffix**.

Parameters applied via MIDI substitute the original values, they get added instead when stated below.

General Settings:

* ***Gain 1*** (volume of voice one, MIDI-values, if applied get added)  
* ***Gain 2*** (volume of volume two, MIDI-values, if applied get added)  
* ***Gate 1*** (On/off switch for voice one)  
* ***Pitch 1*** (Pitch for voice one)  
* ***Gate 2*** (On/off switch for voice two)  
* ***Pitch 2*** (Pitch for voice two)  
* Quantization Scale (Select from different scales, none if 0 \- details see docu of WTOsc)  
* ***Tune 1*** (Finetune applied to voice one, typically used for pitch bend, MIDI-values get added)  
* ***Tune 2*** (Finetune applied to voice two, typically used for pitch bend, MIDI-values get added)  
* Wave Bank (Banks of different wavetables)  
* ***Wave 1*** (select specific wave from wavetable for voice one)  
* ***Wave 2*** (select specific wave from wavetable for voice two)

Filter:

* Filter Mode (0: Off, 1: LowPass, 2: BandPass, 3:HighPass)    	  
* ***Filter Cutoff 1*** (Filter cutoff-frequency applied to voice one, MIDI-values, if applied get added)  
* ***Filter Cutoff 2*** (Filter cutoff-frequency applied to voice one, MIDI-values, if applied get added)  
* Filter Resonance

Modulation:

* LFO Wave (Wave of Low Frequency Oscillator)  
* LFO AM (Amplitude (loudness) modulation of both voices)  
* LFO FM (Frequency (pitch) modulation of both voices)  
* LFO Filter FM (Modulation of filter cutoff-frequency of both voices)  
* EG Wave (Envelope Generator for waves of wavetables of both voices)  
* EG AM (Envelope Generator for Amplitude (loudness) of both voices)  
* EG FM (Envelope Generator for Pitch of both voices)  
* EG Filter FM (Envelope Generator for filter cutoff-frequency)

	*Please note:* EGs can be applied with positive or negative amounts\!

LFO (Low Frequency Oscillator):

* Speed (Frequency of Low Frequency Oscillator)  
* Vintage vibe (Increases slight fre-differences of the LFOs per voice)  
* ***Sync 1*** (Sync to gate 1: LFO of voice one will restart if retriggered)  
* ***Sync 2*** (Sync to gate 2: LFO of voice two will restart if retriggered)

   
ADSR Envelope Generator:

* Fast / Slow (Decreases or increases the actual times of Attack, Decay and Release)  
* Attack  
* Decay  
* Sustain  
* Release