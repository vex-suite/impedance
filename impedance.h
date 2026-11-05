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
    uint8_t midi_note;
    float frequency_hz;
    float velocity;
    float phase;
    float phase_increment;
    float gain;
} ImpedanceVoice;

typedef struct ImpedanceTrack {
    uint32_t id;
    char name[32];
    float volume;       // 0.0 .. 2.0 (1.0 = unity)
    float pan;          // -1.0 (left) .. 1.0 (right)
    bool muted;
    bool solo;
    float speed_multiplier; // 1.0 = normal, adjusted by turntable scratch
    ImpedanceComputeMode compute_mode;
    ImpedanceVoice voices[IMPEDANCE_MAX_VOICES];
    uint32_t active_voices;
} ImpedanceTrack;

typedef struct Impedance {
    double sample_rate;
    uint32_t channels;
    float master_volume;
    ImpedanceTrack tracks[IMPEDANCE_MAX_TRACKS];
    uint32_t track_count;
    AudioHal *hal_engine;
} Impedance;

bool Impedance_init(double sample_rate, uint32_t channels, Impedance *daw);
void Impedance_destroy(Impedance *daw);

uint32_t Impedance_add_track(const char *name, ImpedanceComputeMode mode, Impedance *daw);
bool Impedance_set_track_volume(uint32_t track_idx, float volume, Impedance *daw);
bool Impedance_set_track_pan(uint32_t track_idx, float pan, Impedance *daw);
bool Impedance_set_track_scratch_speed(uint32_t track_idx, float speed, Impedance *daw);

void Impedance_note_on(uint32_t track_idx, uint8_t midi_note, float velocity, Impedance *daw);
void Impedance_note_off(uint32_t track_idx, uint8_t midi_note, Impedance *daw);

void Impedance_render(Impedance *daw, float *interleaved_out, uint32_t frames, uint32_t channels);

bool Impedance_start_live(Impedance *daw);
void Impedance_stop_live(Impedance *daw);

#ifdef __cplusplus
}
#endif

#endif // IMPEDANCE_H
