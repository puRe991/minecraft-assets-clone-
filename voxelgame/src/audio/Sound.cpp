// vg/audio/Sound.cpp — see Sound.hpp for the contract.
#include "vg/audio/Sound.hpp"

#include <cassert>

namespace vg::audio {

SoundId SoundRegistry::add(SoundType t) {
    assert(t.id == (SoundId)types_.size() && "sound ids must be dense/in order");
    types_.push_back(std::move(t));
    return types_.back().id;
}

const SoundType& SoundRegistry::get(SoundId id) const {
    static const SoundType kNone{};
    return id < types_.size() ? types_[id] : kNone;
}

void registerDefaultSounds(SoundRegistry& r) {
    auto S = [](SoundId id, const char* n, SoundCategory c) { SoundType t; t.id = id; t.name = n; t.category = c; return t; };
    r.add(S(snd::None, "none", SoundCategory::UI));

    r.add(S(snd::StepGrass, "step_grass", SoundCategory::Step));
    r.add(S(snd::StepGravel, "step_gravel", SoundCategory::Step));
    r.add(S(snd::StepSand, "step_sand", SoundCategory::Step));
    r.add(S(snd::StepStone, "step_stone", SoundCategory::Step));
    r.add(S(snd::StepWood, "step_wood", SoundCategory::Step));
    r.add(S(snd::StepGlass, "step_glass", SoundCategory::Step));
    r.add(S(snd::StepLiquid, "step_liquid", SoundCategory::Step));

    r.add(S(snd::DigGrass, "dig_grass", SoundCategory::Dig));
    r.add(S(snd::DigGravel, "dig_gravel", SoundCategory::Dig));
    r.add(S(snd::DigSand, "dig_sand", SoundCategory::Dig));
    r.add(S(snd::DigStone, "dig_stone", SoundCategory::Dig));
    r.add(S(snd::DigWood, "dig_wood", SoundCategory::Dig));
    r.add(S(snd::DigGlass, "dig_glass", SoundCategory::Dig));

    r.add(S(snd::AmbientCave, "ambient_cave", SoundCategory::Ambient));
    r.add(S(snd::AmbientWind, "ambient_wind", SoundCategory::Ambient));
    r.add(S(snd::AmbientRain, "ambient_rain", SoundCategory::Ambient));

    r.add(S(snd::MusicCalm1, "music_calm1", SoundCategory::Music));
    r.add(S(snd::MusicCalm2, "music_calm2", SoundCategory::Music));

    r.add(S(snd::UiClick, "ui_click", SoundCategory::UI));
}

}  // namespace vg::audio
