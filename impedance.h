#ifndef IMPEDANCE_H
#define IMPEDANCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "audio/audio_hal.h"
#include "input/piano_key.h"
#include "input/turntable.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IMPEDANCE_MAX_TRACKS 16
#define IMPEDANCE_MAX_VOICES 32

typedef enum ImpedanceComputeMode {
    IMPEDANCE_COMPUTE_CPU = 0,
    IMPEDANCE_COMPUTE_GPU_VULKAN = 1
} ImpedanceComputeMode;

typedef struct ImpedanceVoice {
    bool active;
    u8 midi_note;
    f32 frequency_hz;
    f32 velocity;
    f32 phase;
    f32 phase_increment;
    f32 gain;
} ImpedanceVoice;

typedef struct ImpedanceTrack {
    u32 id;
    char name[32];
    f32 volume;       // 0.0 .. 2.0 (1.0 = unity)
    f32 pan;          // -1.0 (left) .. 1.0 (right)
    bool muted;
    bool solo;
    f32 speed_multiplier; // 1.0 = normal, adjusted by turntable scratch
    ImpedanceComputeMode compute_mode;
    ImpedanceVoice voices[IMPEDANCE_MAX_VOICES];
    u32 active_voices;
} ImpedanceTrack;

typedef struct Impedance {
    d64 sample_rate;
    u32 channels;
    f32 master_volume;
    ImpedanceTrack tracks[IMPEDANCE_MAX_TRACKS];
    u32 track_count;
    AudioHal *hal_engine;
} Impedance;

bool Impedance_init(d64 sample_rate, u32 channels, Impedance *daw);
void Impedance_destroy(Impedance *daw);

u32 Impedance_add_track(const char *name, ImpedanceComputeMode mode, Impedance *daw);
bool Impedance_set_track_volume(u32 track_idx, f32 volume, Impedance *daw);
bool Impedance_set_track_pan(u32 track_idx, f32 pan, Impedance *daw);
bool Impedance_set_track_scratch_speed(u32 track_idx, f32 speed, Impedance *daw);

void Impedance_note_on(u32 track_idx, u8 midi_note, f32 velocity, Impedance *daw);
void Impedance_note_off(u32 track_idx, u8 midi_note, Impedance *daw);

void Impedance_render(Impedance *daw, f32 *interleaved_out, u32 frames, u32 channels);

bool Impedance_start_live(Impedance *daw);
void Impedance_stop_live(Impedance *daw);

#ifdef __cplusplus
}
#endif

#endif // IMPEDANCE_H
