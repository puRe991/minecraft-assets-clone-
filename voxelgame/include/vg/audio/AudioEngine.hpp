// vg/audio/AudioEngine.hpp
//
// Gameplay audio logic: turns game events (walking, digging, placing, biome/
// weather changes, time passing) into SoundEvents sent to a backend. All rules
// are here and unit-testable; only the final playback is backend-specific.
#pragma once

#include "vg/audio/Sound.hpp"
#include "vg/world/BlockTypes.hpp"   // world::SoundGroup

namespace vg::audio {

enum class Weather : uint8_t { Clear, Rain, Snow };
enum class Biome   : uint8_t { Surface, Cave };   // simplified for ambience

class AudioEngine {
public:
    AudioEngine(const SoundRegistry& reg, ISoundBackend& backend);

    // Accumulate walked distance on a given ground material; emits a footstep
    // once per stride. Returns true if a step played.
    bool onMove(double distance, world::SoundGroup ground);

    void onBlockBreak(world::SoundGroup g);
    void onBlockPlace(world::SoundGroup g);

    // Update the ambient bed for the current environment (starts/stops loops
    // only when it actually changes).
    void setEnvironment(Biome biome, Weather weather);

    // Music scheduler: plays a track, waits a gap, plays the next. Advance with
    // the frame delta time.
    void updateMusic(double dt);

    float masterVolume{1.0f};
    double stride{1.6};          // metres between footsteps

private:
    SoundId stepFor(world::SoundGroup g) const;
    SoundId digFor(world::SoundGroup g) const;
    int voiceFor(SoundId id, float vol, float pitch, bool loop);

    const SoundRegistry& reg_;
    ISoundBackend& backend_;

    double walked_{0};
    unsigned stepSeed_{1};       // deterministic pitch variance

    // ambient state
    Biome curBiome_{Biome::Surface};
    Weather curWeather_{Weather::Clear};
    int ambientVoice_{-1};
    bool ambientStarted_{false};

    // music state
    double musicTimer_{0};
    bool musicPlaying_{false};
    int musicTrack_{0};
    double gap_{20.0};           // seconds between tracks
    double trackLen_{45.0};      // nominal track length
};

}  // namespace vg::audio
