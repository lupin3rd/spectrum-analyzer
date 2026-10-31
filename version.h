// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 lupin3rd
#ifndef __SPECTRUM_ANALYZER_VERSION_H__
#define __SPECTRUM_ANALYZER_VERSION_H__

// Single source of truth for the version number.  Both front ends print it,
// the packaging scripts read it, and the CI reads it for artifact names, so it
// must not be duplicated anywhere else.

#define SPECTRUM_ANALYZER_VERSION "1.1.0"

#endif
