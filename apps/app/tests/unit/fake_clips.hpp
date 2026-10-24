// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "sound_sequence_player.hpp"

// A clip player for the tests: no sound card, no files. The clips that exist are the
// ones it is given; a clip that is started ends when the test says so. Test support:
// nothing in libs or apps links it.

namespace wjs::app::test {

// Clips that exist are the ones in `have`; a started clip ends when the test says.
class FakeClips final : public wjs::app::ClipPlayer {
public:
    explicit FakeClips(std::set<std::string> have, std::vector<std::string>& log)
        : have_(std::move(have)), log_(log) {}

    bool play(const std::string& clip, double volume, std::function<void()> finished) override {
        if (!have_.contains(clip)) {
            log_.push_back("missing " + clip);
            return false;
        }
        log_.push_back("clip " + clip);
        last_volume = volume;
        finish_ = std::move(finished);
        return true;
    }

    void stop() override {
        log_.push_back("stop");
        finish_ = nullptr;
    }

    // The clip under way has ended.
    void end() {
        REQUIRE(finish_);
        auto finished = std::move(finish_);
        finish_ = nullptr;
        finished();
    }

    [[nodiscard]] bool playing() const { return static_cast<bool>(finish_); }

    // What the fake last saw the clip's volume be.
    double last_volume = -1.0;

    // A clip's end as it was handed over, kept by a test to call after the sequence
    // it belonged to has been replaced.
    [[nodiscard]] std::function<void()> take() { return std::exchange(finish_, nullptr); }

private:
    std::set<std::string> have_;
    std::vector<std::string>& log_;
    std::function<void()> finish_;
};

}  // namespace wjs::app::test
