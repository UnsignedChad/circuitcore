// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("smoke: arithmetic works", "[smoke]") {
    REQUIRE(1 + 1 == 2);
}
