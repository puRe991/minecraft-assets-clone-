// vg/audio/Sound.hpp
//
// Sound registry + the backend abstraction. The engine decides *what* to play
// (a SoundEvent); an ISoundBackend decides *how* (WinMM in the app, or a
// recording stub in tests). This keeps all the gameplay-audio logic testable
// without an audio device (DIP).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vg::audio {

using SoundId = uint16_t;

enum class SoundCategory : uint8_t { Step, Dig, Block, Ambient, Music, UI };

struct SoundType {
    SoundId       id{0};
    std::string   name{"none"};
    SoundCategory category{SoundCategory::Block};
};

// Well-known sounds (registerDefaultSounds assigns them in this order).
namespace snd {
enum : SoundId {
    None = 0,
    StepGrass, StepGravel, StepSand, StepStone, StepWood, StepGlass, StepLiquid,
    DigGrass,  DigGravel,  DigSand,  DigStone,  DigWood,  DigGlass,
    AmbientCave, AmbientWind, AmbientRain,
    MusicCalm1, MusicCalm2,
    UiClick,
    Count
};
}  // namespace snd

// A request to play a sound. `loop` is used for ambient/music beds.
struct SoundEvent {
    SoundId id{snd::None};
    float   volume{1.0f};
    float   pitch{1.0f};
    bool    loop{false};
    bool    hasPos{false};
    float   x{0}, y{0}, z{0};
};

// Backend interface. play() returns an opaque voice handle (>=0) or -1.
class ISoundBackend {
public:
    virtual ~ISoundBackend() = default;
    virtual int  play(const SoundEvent& ev) = 0;
    virtual void stop(int voice) = 0;
    virtual bool isPlaying(int voice) const = 0;
};

// Test/inspection backend: records every play() and tracks stop().
class RecordingBackend final : public ISoundBackend {
public:
    struct Voice { SoundEvent ev; bool playing; };
    int play(const SoundEvent& ev) override { voices_.push_back({ev, true}); return (int)voices_.size() - 1; }
    void stop(int v) override { if (v >= 0 && v < (int)voices_.size()) voices_[v].playing = false; }
    bool isPlaying(int v) const override { return v >= 0 && v < (int)voices_.size() && voices_[v].playing; }

    const std::vector<Voice>& voices() const { return voices_; }
    int countOf(SoundId id) const {
        int n = 0; for (auto& v : voices_) if (v.ev.id == id) ++n; return n;
    }
    void clear() { voices_.clear(); }
private:
    std::vector<Voice> voices_;
};

class SoundRegistry {
public:
    SoundId add(SoundType t);
    const SoundType& get(SoundId id) const;
    size_t count() const { return types_.size(); }
private:
    std::vector<SoundType> types_;
};

void registerDefaultSounds(SoundRegistry& reg);

}  // namespace vg::audio
