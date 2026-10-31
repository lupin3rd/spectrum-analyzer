// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 lupin3rd
//
// miniaudio implementation unit.
//
// miniaudio is a single-header library: every translation unit may include it
// for the declarations, but exactly one must define MINIAUDIO_IMPLEMENTATION.
// Keeping that unit separate means editing audio-sampler.cc does not recompile
// 97k lines of the audio backend.
//
// Only the pieces this program uses are enabled; decoding, encoding, the
// resource manager, the node graph and the mixing engine are all switched off,
// which cuts both the build time and the binary size.

#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_NO_GENERATION

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
