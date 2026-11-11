#include "impedance.h"
#include "annotation/annotation.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TWO_PI 6.28318530717958647692

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
 *   Impedance *daw), void Impedance_destroy(Impedance *daw)
 * Mixer: u32 Impedance_add_track(const char *name,ImpedanceComputeMode mode,
 *   Impedance *daw); bool Impedance_set_track_volume/pan/scratch_speed(...)
 * Notes/render: void Impedance_note_on/off(...),
 *   void Impedance_render(Impedance *daw,f32 *interleaved_out,
 *   u32 frames,u32 channels)
 * Live lifecycle: bool Impedance_start_live(Impedance *daw),
 *   void Impedance_stop_live(Impedance *daw)
 * Private helper: impedance_hal_callback forwards device output to render.
 * Ownership and failure contract:
 * Track and voice storage is inline and bounded by the declared track/voice
 * arrays. Invalid render/track inputs are rejected or ignored as implemented;
 * live engine ownership ends in stop_live/destroy. This API has no general DAW
 * session workflow.
 * ======================================================================== */

// Forward audio-device output requests to the DAW renderer when its context exists.
static void impedance_hal_callback(
    f32 *interleaved_output,
    u32 frame_count,
    u32 channels,
    void *user_data
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

    (*daw).sample_rate = (sample_rate > 0.0) ? sample_rate : 48000.0;
    (*daw).channels = (channels > 0) ? channels : 2;
    (*daw).master_volume = 1.0f;
    (*daw).track_count = 0;
    (*daw).hal_engine = nullptr;

    memset((*daw).tracks, 0, sizeof((*daw).tracks));
    return true;
}

// Stop live audio and clear the active track count before releasing the mixer state.
void Impedance_destroy(Impedance *daw) {
    if (!daw) {
        return;
    }
    Impedance_stop_live(daw);
    (*daw).track_count = 0;
}

// Add an initialized track and return its index, or the sentinel when the mixer is full.
u32 Impedance_add_track(const char *name, ImpedanceComputeMode mode, Impedance *daw) {
    if (!daw || (*daw).track_count >= IMPEDANCE_MAX_TRACKS) {
        return 0xFFFFFFFFu;
    }

    u32 idx = (*daw).track_count++;
    ImpedanceTrack *track = &(*daw).tracks[idx];

    (*track).id = idx;
    snprintf((*track).name, sizeof((*track).name), "%s", name ? name : "Track");
    (*track).volume = 1.0f;
    (*track).pan = 0.0f;
    (*track).muted = false;
    (*track).solo = false;
    (*track).speed_multiplier = 1.0f;
    (*track).compute_mode = mode;
    (*track).active_voices = 0;

    for (u32 v = 0; v < IMPEDANCE_MAX_VOICES; v++) {
        (*track).voices[v].active = false;
        (*track).voices[v].midi_note = 0;
        (*track).voices[v].frequency_hz = 0.0f;
        (*track).voices[v].velocity = 0.0f;
        (*track).voices[v].phase = 0.0f;
        (*track).voices[v].phase_increment = 0.0f;
        (*track).voices[v].gain = 0.0f;
    }

    return idx;
}

// Set a track's nonnegative volume when the requested index is active.
bool Impedance_set_track_volume(u32 track_idx, f32 volume, Impedance *daw) {
    if (!daw || track_idx >= (*daw).track_count) {
        return false;
    }
    (*daw).tracks[track_idx].volume = (volume < 0.0f) ? 0.0f : volume;
    return true;
}

// Set a track's pan after clamping it to the supported left-to-right range.
bool Impedance_set_track_pan(u32 track_idx, f32 pan, Impedance *daw) {
    if (!daw || track_idx >= (*daw).track_count) {
        return false;
    }
    if (pan < -1.0f) {
        pan = -1.0f;
    }
    if (pan > 1.0f) {
        pan = 1.0f;
    }
    (*daw).tracks[track_idx].pan = pan;
    return true;
}

// Set the playback-speed multiplier for an active track.
bool Impedance_set_track_scratch_speed(u32 track_idx, f32 speed, Impedance *daw) {
    if (!daw || track_idx >= (*daw).track_count) {
        return false;
    }
    (*daw).tracks[track_idx].speed_multiplier = speed;
    return true;
}

// Start the first available voice on a track for the requested MIDI note.
void Impedance_note_on(u32 track_idx, u8 midi_note, f32 velocity, Impedance *daw) {
    if (!daw || track_idx >= (*daw).track_count) {
        return;
    }

    ImpedanceTrack *track = &(*daw).tracks[track_idx];
    f32 freq = PianoKey_equal_temperament_hz(midi_note);

    for (u32 v = 0; v < IMPEDANCE_MAX_VOICES; v++) {
        if (!(*track).voices[v].active) {
            (*track).voices[v].active = true;
            (*track).voices[v].midi_note = midi_note;
            (*track).voices[v].frequency_hz = freq;
            (*track).voices[v].velocity = velocity;
            (*track).voices[v].phase = 0.0f;
            (*track).voices[v].phase_increment = (f32) ((TWO_PI * freq) / (*daw).sample_rate);
            (*track).voices[v].gain = velocity;
            (*track).active_voices++;
            return;
        }
    }
}

// Deactivate active voices matching the MIDI note on the selected track.
void Impedance_note_off(u32 track_idx, u8 midi_note, Impedance *daw) {
    if (!daw || track_idx >= (*daw).track_count) {
        return;
    }

    ImpedanceTrack *track = &(*daw).tracks[track_idx];
    for (u32 v = 0; v < IMPEDANCE_MAX_VOICES; v++) {
        if ((*track).voices[v].active && (*track).voices[v].midi_note == midi_note) {
            (*track).voices[v].active = false;
            if ((*track).active_voices > 0) {
                (*track).active_voices--;
            }
        }
    }
}

// Clear and mix active, unmuted voices into the caller's interleaved output buffer.
void Impedance_render(Impedance *daw, f32 *interleaved_out, u32 frames, u32 channels) {
    if (!daw || !interleaved_out || frames == 0 || channels == 0) {
        return;
    }

    bytes total_samples = (bytes) frames * channels;
    memset(interleaved_out, 0, total_samples * sizeof(f32));

    bool any_solo = false;
    for (u32 t = 0; t < (*daw).track_count; t++) {
        if ((*daw).tracks[t].solo) {
            any_solo = true;
            break;
        }
    }

    for (u32 t = 0; t < (*daw).track_count; t++) {
        ImpedanceTrack *track = &(*daw).tracks[t];
        if ((*track).muted) {
            continue;
        }
        if (any_solo && !(*track).solo) {
            continue;
        }

        f32 pan = (*track).pan;
        f32 left_gain = 0.7071f * (1.0f - pan);
        f32 right_gain = 0.7071f * (1.0f + pan);
        f32 track_vol = (*track).volume * (*daw).master_volume;
        f32 speed = (*track).speed_multiplier;

        for (u32 v = 0; v < IMPEDANCE_MAX_VOICES; v++) {
            ImpedanceVoice *voice = &(*track).voices[v];
            if (!(*voice).active) {
                continue;
            }

            f32 phase = (*voice).phase;
            f32 inc = (*voice).phase_increment * speed;
            f32 voice_gain = (*voice).gain * track_vol;

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

            (*voice).phase = phase;
        }
    }
}

// Create and start the audio HAL using the mixer's configured output format.
bool Impedance_start_live(Impedance *daw) {
    if (!daw) {
        return false;
    }
    if ((*daw).hal_engine) {
        return true;
    }

    AudioHalConfig cfg = AudioHalConfig(impedance_hal_callback, (void*) daw);
    AudioHalConfig_set_sample_rate(&cfg, (*daw).sample_rate);
    AudioHalConfig_set_channels(&cfg, (*daw).channels);
    AudioHalConfig_set_buffer_frames(&cfg, 256);
    AudioHal *hal = AudioHal(&cfg);
    if (!hal) {
        return false;
    }

    if (!AudioHal_start(hal)) {
        AudioHal_destroy(hal);
        return false;
    }

    (*daw).hal_engine = hal;
    return true;
}

// Stop and destroy the live audio device, if one is attached.
void Impedance_stop_live(Impedance *daw) {
    if (!daw || !(*daw).hal_engine) {
        return;
    }
    AudioHal_stop((*daw).hal_engine);
    AudioHal_destroy((*daw).hal_engine);
    (*daw).hal_engine = nullptr;
}
