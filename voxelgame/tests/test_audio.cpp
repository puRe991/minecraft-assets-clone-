// Unit tests for the audio engine (Module 9).
#include "TestFramework.hpp"
#include "vg/audio/AudioEngine.hpp"

using namespace vg::audio;
using vg::world::SoundGroup;

struct Kit {
    SoundRegistry reg;
    RecordingBackend backend;
    AudioEngine engine;
    Kit() : engine(reg, backend) { registerDefaultSounds(reg); }
};

VG_TEST(sound_registry) {
    Kit k;
    CHECK(k.reg.count() == snd::Count);
    CHECK(k.reg.get(snd::StepGrass).category == SoundCategory::Step);
    CHECK(k.reg.get(snd::MusicCalm1).category == SoundCategory::Music);
}

VG_TEST(footsteps_by_stride_and_material) {
    Kit k;
    k.engine.stride = 1.6;
    // walk 1.0 m: no step yet
    CHECK(!k.engine.onMove(1.0, SoundGroup::Grass));
    CHECK(k.backend.voices().empty());
    // cross the stride threshold: one grass step
    CHECK(k.engine.onMove(1.0, SoundGroup::Grass));
    CHECK(k.backend.countOf(snd::StepGrass) == 1);
    // keep walking on stone: next step is a stone step
    k.engine.onMove(1.6, SoundGroup::Stone);
    CHECK(k.backend.countOf(snd::StepStone) == 1);
}

VG_TEST(block_break_and_place_sounds) {
    Kit k;
    k.engine.onBlockBreak(SoundGroup::Wood);
    k.engine.onBlockPlace(SoundGroup::Stone);
    CHECK(k.backend.countOf(snd::DigWood) == 1);
    CHECK(k.backend.countOf(snd::DigStone) == 1);
    // liquids produce no dig sound
    k.backend.clear();
    k.engine.onBlockBreak(SoundGroup::Liquid);
    CHECK(k.backend.voices().empty());
}

VG_TEST(ambient_bed_changes_and_loops) {
    Kit k;
    k.engine.setEnvironment(Biome::Surface, Weather::Clear);
    CHECK(k.backend.countOf(snd::AmbientWind) == 1);
    CHECK(k.backend.voices().back().ev.loop == true);
    int startVoices = (int)k.backend.voices().size();

    // same environment again: nothing new
    k.engine.setEnvironment(Biome::Surface, Weather::Clear);
    CHECK((int)k.backend.voices().size() == startVoices);

    // go underground: cave ambience starts, previous loop stopped
    k.engine.setEnvironment(Biome::Cave, Weather::Clear);
    CHECK(k.backend.countOf(snd::AmbientCave) == 1);

    // rain overrides biome ambience
    k.engine.setEnvironment(Biome::Cave, Weather::Rain);
    CHECK(k.backend.countOf(snd::AmbientRain) == 1);
}

VG_TEST(music_scheduler_alternates_with_gaps) {
    Kit k;
    // advance well past the first gap -> a track starts
    for (int i = 0; i < 250; ++i) k.engine.updateMusic(0.1);   // 25s > gap(20)
    int music1 = k.backend.countOf(snd::MusicCalm1);
    CHECK(music1 == 1);
    // advance through the track + next gap -> the second track plays
    for (int i = 0; i < 700; ++i) k.engine.updateMusic(0.1);   // +70s
    CHECK(k.backend.countOf(snd::MusicCalm2) == 1);
}
