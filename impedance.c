#include "vex.h"
#include "impedance.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TWO_PI 6.28318530717958647692

;;GETTER
static inline d64 Impedance_sample_rate(const Impedance *self) { return (*self).sample_rate; }
;;SETTER
static inline fn Impedance_setSample_rate(Impedance *self, d64 sample_rate) { (*self).sample_rate = sample_rate; }
;;GETTER
static inline u32 Impedance_channels(const Impedance *self) { return (*self).channels; }
;;SETTER
static inline fn Impedance_setChannels(Impedance *self, u32 channels) { (*self).channels = channels; }
;;GETTER
static inline f32 Impedance_master_volume(const Impedance *self) { return (*self).master_volume; }
;;SETTER
static inline fn Impedance_setMaster_volume(Impedance *self, f32 master_volume) { (*self).master_volume = master_volume; }
;;GETTER
static inline u32 Impedance_track_count(const Impedance *self) { return (*self).track_count; }
;;SETTER
static inline fn Impedance_setTrack_count(Impedance *self, u32 track_count) { (*self).track_count = track_count; }
;;GETTER
static inline ImpedanceTrack *Impedance_tracks(Impedance *self) { return (*self).tracks; }
;;GETTER
static inline AudioHal *Impedance_hal_engine(const Impedance *self) { return (*self).hal_engine; }
;;SETTER
static inline fn Impedance_setHal_engine(Impedance *self, AudioHal *hal_engine) { (*self).hal_engine = hal_engine; }

;;GETTER
static inline char *ImpedanceTrack_name(ImpedanceTrack *self) { return (*self).name; }
;;GETTER
static inline f32 ImpedanceTrack_volume(const ImpedanceTrack *self) { return (*self).volume; }
;;SETTER
static inline fn ImpedanceTrack_setVolume(ImpedanceTrack *self, f32 volume) { (*self).volume = volume; }
;;GETTER
static inline f32 ImpedanceTrack_pan(const ImpedanceTrack *self) { return (*self).pan; }
;;SETTER
static inline fn ImpedanceTrack_setPan(ImpedanceTrack *self, f32 pan) { (*self).pan = pan; }
;;GETTER
static inline bool ImpedanceTrack_muted(const ImpedanceTrack *self) { return (*self).muted; }
;;SETTER
static inline fn ImpedanceTrack_setMuted(ImpedanceTrack *self, bool muted) { (*self).muted = muted; }
;;GETTER
static inline bool ImpedanceTrack_solo(const ImpedanceTrack *self) { return (*self).solo; }
;;SETTER
static inline fn ImpedanceTrack_setSolo(ImpedanceTrack *self, bool solo) { (*self).solo = solo; }
;;GETTER
static inline f32 ImpedanceTrack_speed_multiplier(const ImpedanceTrack *self) { return (*self).speed_multiplier; }
;;SETTER
static inline fn ImpedanceTrack_setSpeed_multiplier(ImpedanceTrack *self, f32 speed) { (*self).speed_multiplier = speed; }
;;GETTER
static inline u32 ImpedanceTrack_active_voices(const ImpedanceTrack *self) { return (*self).active_voices; }
;;SETTER
static inline fn ImpedanceTrack_setActive_voices(ImpedanceTrack *self, u32 active_voices) { (*self).active_voices = active_voices; }
;;SETTER
static inline fn ImpedanceTrack_setId(ImpedanceTrack *self, u32 id) { (*self).id = id; }
;;SETTER
static inline fn ImpedanceTrack_setCompute_mode(ImpedanceTrack *self, ImpedanceComputeMode mode) { (*self).compute_mode = mode; }
;;GETTER
static inline ImpedanceVoice *ImpedanceTrack_voices(ImpedanceTrack *self) { return (*self).voices; }

;;GETTER
static inline bool ImpedanceVoice_active(const ImpedanceVoice *self) { return (*self).active; }
;;SETTER
static inline fn ImpedanceVoice_setActive(ImpedanceVoice *self, bool active) { (*self).active = active; }
;;GETTER
static inline u8 ImpedanceVoice_midi_note(const ImpedanceVoice *self) { return (*self).midi_note; }
;;SETTER
static inline fn ImpedanceVoice_setMidi_note(ImpedanceVoice *self, u8 midi_note) { (*self).midi_note = midi_note; }
;;SETTER
static inline fn ImpedanceVoice_setFrequency_hz(ImpedanceVoice *self, f32 frequency_hz) { (*self).frequency_hz = frequency_hz; }
;;SETTER
static inline fn ImpedanceVoice_setVelocity(ImpedanceVoice *self, f32 velocity) { (*self).velocity = velocity; }
;;GETTER
static inline f32 ImpedanceVoice_phase(const ImpedanceVoice *self) { return (*self).phase; }
;;SETTER
static inline fn ImpedanceVoice_setPhase(ImpedanceVoice *self, f32 phase) { (*self).phase = phase; }
;;GETTER
static inline f32 ImpedanceVoice_phase_increment(const ImpedanceVoice *self) { return (*self).phase_increment; }
;;SETTER
static inline fn ImpedanceVoice_setPhase_increment(ImpedanceVoice *self, f32 phase_increment) { (*self).phase_increment = phase_increment; }
;;GETTER
static inline f32 ImpedanceVoice_gain(const ImpedanceVoice *self) { return (*self).gain; }
;;SETTER
static inline fn ImpedanceVoice_setGain(ImpedanceVoice *self, f32 gain) { (*self).gain = gain; }

;;DEFINITION
/** Impedance is the R5 DAW's fixed-capacity mixer state and audio callback
 * bridge. It owns tracks and voices inline, while the R3 AudioHal is acquired
 * only by start_live and stopped before destruction. The offline render path
 * consumes caller-provided output storage; this file is not proof of a complete
 * DAW workflow or GPU compute backend. */
;;OVERVIEW
/** ========================================================================
 * CLASS: Impedance (src/impedance.c)
 * STRUCT FIELDS (Mirroring impedance.h)
 * d64 sample_rate             admitted render sample rate
 * u32 channels              configured output channel count
 * f32 master_volume            mixer output gain
 * ImpedanceTrack tracks[16]      fixed inline track/voice records
 * u32 track_count           initialized track prefix length
 * AudioHal *hal_engine           owned live audio engine when started
 * FUNCTION REGISTRY
 * Lifecycle: bool Impedance_init(d64 sample_rate,u32 channels,
 *   Impedance *daw), fn Impedance_destroy(Impedance *daw)
 * Mixer: u32 Impedance_add_track(const char *name,ImpedanceComputeMode mode,
 *   Impedance *daw); bool Impedance_set_track_volume/pan/scratch_speed(...)
 * Notes/render: fn Impedance_note_on/off(...),
 *   fn Impedance_render(Impedance *daw,f32 *interleaved_out,
 *   u32 frames,u32 channels)
 * Live lifecycle: bool Impedance_start_live(Impedance *daw),
 *   fn Impedance_stop_live(Impedance *daw)
 * Private helper: impedance_hal_callback forwards device output to render.
 * Ownership and failure contract:
 * Track and voice storage is inline and bounded by the declared track/voice
 * arrays. Invalid render/track inputs are rejected or ignored as implemented;
 * live engine ownership ends in stop_live/destroy. This API has no general DAW
 * session workflow.
 * ======================================================================== */

// Forward audio-device output requests to the DAW renderer when its context exists.
static fn impedance_hal_callback(
    f32 *interleaved_output,
    u32 frame_count,
    u32 channels,
    any user_data
) {
    Impedance *daw = (Impedance*) user_data;
    if (daw) {
        Impedance_render(daw, interleaved_output, frame_count, channels);
    }
}

// Initialize the mixer state, substituting default sample rate and channel count when invalid.
bool Impedance_init(d64 sample_rate, u32 channels, Impedance *daw) {
    if (!daw) {
        return false;
    }

    Impedance_setSample_rate(daw, (sample_rate > 0.0) ? sample_rate : 48000.0);
    Impedance_setChannels(daw, (channels > 0) ? channels : 2);
    Impedance_setMaster_volume(daw, 1.0f);
    Impedance_setTrack_count(daw, 0);
    Impedance_setHal_engine(daw, nullptr);

    memset(Impedance_tracks(daw), 0, sizeof(ImpedanceTrack) * IMPEDANCE_MAX_TRACKS);
    return true;
}

// Stop live audio and clear the active track count before releasing the mixer state.
fn Impedance_destroy(Impedance *daw) {
    if (!daw) {
        return;
    }
    Impedance_stop_live(daw);
    Impedance_setTrack_count(daw, 0);
}

// Add an initialized track and return its index, or the sentinel when the mixer is full.
u32 Impedance_add_track(const char *name, ImpedanceComputeMode mode, Impedance *daw) {
    if (!daw || Impedance_track_count(daw) >= IMPEDANCE_MAX_TRACKS) {
        return 0xFFFFFFFFu;
    }

    u32 idx = Impedance_track_count(daw);
    Impedance_setTrack_count(daw, idx + 1);
    ImpedanceTrack *track = &Impedance_tracks(daw)[idx];

    ImpedanceTrack_setId(track, idx);
    snprintf(ImpedanceTrack_name(track), sizeof(((ImpedanceTrack) {0}).name), "%s", name ? name : "Track");
    ImpedanceTrack_setVolume(track, 1.0f);
    ImpedanceTrack_setPan(track, 0.0f);
    ImpedanceTrack_setMuted(track, false);
    ImpedanceTrack_setSolo(track, false);
    ImpedanceTrack_setSpeed_multiplier(track, 1.0f);
    ImpedanceTrack_setCompute_mode(track, mode);
    ImpedanceTrack_setActive_voices(track, 0);

    ImpedanceVoice *voices = ImpedanceTrack_voices(track);
    for (u32 v = 0; v < IMPEDANCE_MAX_VOICES; v++) {
        ImpedanceVoice *voice = &voices[v];
        ImpedanceVoice_setActive(voice, false);
        ImpedanceVoice_setMidi_note(voice, 0);
        ImpedanceVoice_setFrequency_hz(voice, 0.0f);
        ImpedanceVoice_setVelocity(voice, 0.0f);
        ImpedanceVoice_setPhase(voice, 0.0f);
        ImpedanceVoice_setPhase_increment(voice, 0.0f);
        ImpedanceVoice_setGain(voice, 0.0f);
    }

    return idx;
}

// Set a track's nonnegative volume when the requested index is active.
bool Impedance_set_track_volume(u32 track_idx, f32 volume, Impedance *daw) {
    if (!daw || track_idx >= Impedance_track_count(daw)) {
        return false;
    }
    ImpedanceTrack_setVolume(&Impedance_tracks(daw)[track_idx], (volume < 0.0f) ? 0.0f : volume);
    return true;
}

// Set a track's pan after clamping it to the supported left-to-right range.
bool Impedance_set_track_pan(u32 track_idx, f32 pan, Impedance *daw) {
    if (!daw || track_idx >= Impedance_track_count(daw)) {
        return false;
    }
    if (pan < -1.0f) {
        pan = -1.0f;
    }
    if (pan > 1.0f) {
        pan = 1.0f;
    }
    ImpedanceTrack_setPan(&Impedance_tracks(daw)[track_idx], pan);
    return true;
}

// Set the playback-speed multiplier for an active track.
bool Impedance_set_track_scratch_speed(u32 track_idx, f32 speed, Impedance *daw) {
    if (!daw || track_idx >= Impedance_track_count(daw)) {
        return false;
    }
    ImpedanceTrack_setSpeed_multiplier(&Impedance_tracks(daw)[track_idx], speed);
    return true;
}

// Start the first available voice on a track for the requested MIDI note.
fn Impedance_note_on(u32 track_idx, u8 midi_note, f32 velocity, Impedance *daw) {
    if (!daw || track_idx >= Impedance_track_count(daw)) {
        return;
    }

    ImpedanceTrack *track = &Impedance_tracks(daw)[track_idx];
    ImpedanceVoice *voices = ImpedanceTrack_voices(track);
    f32 freq = PianoKey_equal_temperament_hz(midi_note);

    for (u32 v = 0; v < IMPEDANCE_MAX_VOICES; v++) {
        ImpedanceVoice *voice = &voices[v];
        if (!ImpedanceVoice_active(voice)) {
            ImpedanceVoice_setActive(voice, true);
            ImpedanceVoice_setMidi_note(voice, midi_note);
            ImpedanceVoice_setFrequency_hz(voice, freq);
            ImpedanceVoice_setVelocity(voice, velocity);
            ImpedanceVoice_setPhase(voice, 0.0f);
            ImpedanceVoice_setPhase_increment(voice, (f32) ((TWO_PI * freq) / Impedance_sample_rate(daw)));
            ImpedanceVoice_setGain(voice, velocity);
            ImpedanceTrack_setActive_voices(track, ImpedanceTrack_active_voices(track) + 1);
            return;
        }
    }
}

// Deactivate active voices matching the MIDI note on the selected track.
fn Impedance_note_off(u32 track_idx, u8 midi_note, Impedance *daw) {
    if (!daw || track_idx >= Impedance_track_count(daw)) {
        return;
    }

    ImpedanceTrack *track = &Impedance_tracks(daw)[track_idx];
    ImpedanceVoice *voices = ImpedanceTrack_voices(track);
    for (u32 v = 0; v < IMPEDANCE_MAX_VOICES; v++) {
        ImpedanceVoice *voice = &voices[v];
        if (ImpedanceVoice_active(voice) && ImpedanceVoice_midi_note(voice) == midi_note) {
            ImpedanceVoice_setActive(voice, false);
            u32 active = ImpedanceTrack_active_voices(track);
            if (active > 0) {
                ImpedanceTrack_setActive_voices(track, active - 1);
            }
        }
    }
}

// Clear and mix active, unmuted voices into the caller's interleaved output buffer.
fn Impedance_render(Impedance *daw, f32 *interleaved_out, u32 frames, u32 channels) {
    if (!daw || !interleaved_out || frames == 0 || channels == 0) {
        return;
    }

    bytes total_samples = (bytes) frames * channels;
    memset(interleaved_out, 0, total_samples * sizeof(f32));

    u32 track_count = Impedance_track_count(daw);
    ImpedanceTrack *tracks = Impedance_tracks(daw);
    bool any_solo = false;
    for (u32 t = 0; t < track_count; t++) {
        if (ImpedanceTrack_solo(&tracks[t])) {
            any_solo = true;
            break;
        }
    }

    for (u32 t = 0; t < track_count; t++) {
        ImpedanceTrack *track = &tracks[t];
        if (ImpedanceTrack_muted(track)) {
            continue;
        }
        if (any_solo && !ImpedanceTrack_solo(track)) {
            continue;
        }

        f32 pan = ImpedanceTrack_pan(track);
        f32 left_gain = 0.7071f * (1.0f - pan);
        f32 right_gain = 0.7071f * (1.0f + pan);
        f32 track_vol = ImpedanceTrack_volume(track) * Impedance_master_volume(daw);
        f32 speed = ImpedanceTrack_speed_multiplier(track);
        ImpedanceVoice *voices = ImpedanceTrack_voices(track);

        for (u32 v = 0; v < IMPEDANCE_MAX_VOICES; v++) {
            ImpedanceVoice *voice = &voices[v];
            if (!ImpedanceVoice_active(voice)) {
                continue;
            }

            f32 phase = ImpedanceVoice_phase(voice);
            f32 inc = ImpedanceVoice_phase_increment(voice) * speed;
            f32 voice_gain = ImpedanceVoice_gain(voice) * track_vol;

            for (u32 f = 0; f < frames; f++) {
                f32 sample = sinf(phase) * voice_gain;
                phase += inc;
                if (phase > (f32) TWO_PI) {
                    phase -= (f32) TWO_PI;
                } else if (phase < 0.0f) {
                    phase += (f32) TWO_PI;
                }

                if (channels == 1) {
                    interleaved_out[f] += sample;
                } else {
                    interleaved_out[f * channels + 0] += sample * left_gain;
                    interleaved_out[f * channels + 1] += sample * right_gain;
                }
            }

            ImpedanceVoice_setPhase(voice, phase);
        }
    }
}

// Create and start the audio HAL using the mixer's configured output format.
bool Impedance_start_live(Impedance *daw) {
    if (!daw) {
        return false;
    }
    if (Impedance_hal_engine(daw)) {
        return true;
    }

    AudioHalConfig cfg = AudioHalConfig(impedance_hal_callback, (any) daw);
    AudioHalConfig_set_sample_rate(&cfg, Impedance_sample_rate(daw));
    AudioHalConfig_set_channels(&cfg, Impedance_channels(daw));
    AudioHalConfig_set_buffer_frames(&cfg, 256);
    AudioHal *hal = AudioHal(&cfg);
    if (!hal) {
        return false;
    }

    if (!AudioHal_start(hal)) {
        AudioHal_destroy(hal);
        return false;
    }

    Impedance_setHal_engine(daw, hal);
    return true;
}

// Stop and destroy the live audio device, if one is attached.
fn Impedance_stop_live(Impedance *daw) {
    AudioHal *hal = daw ? Impedance_hal_engine(daw) : nullptr;
    if (!hal) {
        return;
    }
    AudioHal_stop(hal);
    AudioHal_destroy(hal);
    Impedance_setHal_engine(daw, nullptr);
}
