// vg/audio/AudioEngine.cpp — see AudioEngine.hpp for the contract.
#include "vg/audio/AudioEngine.hpp"

namespace vg::audio {

using world::SoundGroup;

AudioEngine::AudioEngine(const SoundRegistry& reg, ISoundBackend& backend)
    : reg_(reg), backend_(backend) {}

SoundId AudioEngine::stepFor(SoundGroup g) const {
    switch (g) {
        case SoundGroup::Grass:  return snd::StepGrass;
        case SoundGroup::Gravel: return snd::StepGravel;
        case SoundGroup::Sand:   return snd::StepSand;
        case SoundGroup::Stone:  return snd::StepStone;
        case SoundGroup::Wood:   return snd::StepWood;
        case SoundGroup::Glass:  return snd::StepGlass;
        case SoundGroup::Liquid: return snd::StepLiquid;
        default:                 return snd::None;
    }
}

SoundId AudioEngine::digFor(SoundGroup g) const {
    switch (g) {
        case SoundGroup::Grass:  return snd::DigGrass;
        case SoundGroup::Gravel: return snd::DigGravel;
        case SoundGroup::Sand:   return snd::DigSand;
        case SoundGroup::Stone:  return snd::DigStone;
        case SoundGroup::Wood:   return snd::DigWood;
        case SoundGroup::Glass:  return snd::DigGlass;
        case SoundGroup::Liquid: return snd::None;      // liquids have no dig sound
        default:                 return snd::DigStone;
    }
}

int AudioEngine::voiceFor(SoundId id, float vol, float pitch, bool loop) {
    if (id == snd::None) return -1;
    SoundEvent ev;
    ev.id = id; ev.volume = vol * masterVolume; ev.pitch = pitch; ev.loop = loop;
    return backend_.play(ev);
}

bool AudioEngine::onMove(double distance, SoundGroup ground) {
    if (distance > 0) walked_ += distance;
    if (walked_ < stride) return false;
    walked_ -= stride;
    // deterministic slight pitch variance so footsteps don't sound identical
    stepSeed_ = stepSeed_ * 1664525u + 1013904223u;
    float pitch = 0.9f + (stepSeed_ >> 24) / 255.0f * 0.2f;   // 0.9..1.1
    voiceFor(stepFor(ground), 0.4f, pitch, false);
    return true;
}

void AudioEngine::onBlockBreak(SoundGroup g) { voiceFor(digFor(g), 0.7f, 1.0f, false); }
void AudioEngine::onBlockPlace(SoundGroup g) { voiceFor(digFor(g), 0.6f, 1.1f, false); }

void AudioEngine::setEnvironment(Biome biome, Weather weather) {
    if (ambientStarted_ && biome == curBiome_ && weather == curWeather_) return;  // no change
    curBiome_ = biome; curWeather_ = weather; ambientStarted_ = true;

    if (ambientVoice_ >= 0) { backend_.stop(ambientVoice_); ambientVoice_ = -1; }

    SoundId id = snd::None;
    if (weather == Weather::Rain)      id = snd::AmbientRain;
    else if (biome == Biome::Cave)     id = snd::AmbientCave;
    else                               id = snd::AmbientWind;
    ambientVoice_ = voiceFor(id, 0.5f, 1.0f, /*loop=*/true);
}

void AudioEngine::updateMusic(double dt) {
    musicTimer_ += dt;
    if (musicPlaying_) {
        if (musicTimer_ >= trackLen_) { musicPlaying_ = false; musicTimer_ = 0; }
    } else {
        if (musicTimer_ >= gap_) {
            musicTimer_ = 0; musicPlaying_ = true;
            SoundId track = (musicTrack_++ % 2 == 0) ? snd::MusicCalm1 : snd::MusicCalm2;
            voiceFor(track, 0.35f, 1.0f, false);
        }
    }
}

}  // namespace vg::audio
