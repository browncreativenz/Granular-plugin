/**
 * @file daisy_seed.h (host stub)
 * @brief Just enough of libDaisy to compile Capicola's src/audio/audio_engine.cpp
 *        on a desktop, so tests/engine_golden.cpp can run the firmware engine.
 */

#pragma once

/* SDRAM placement has no meaning on a host: the rings become ordinary globals. */
#define DSY_SDRAM_BSS
