// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// FontStacks.h is header-only (the probes are `inline` so a consumer needs no
// library entry, and `pickFamily` stays constant-foldable). This translation
// unit exists solely to assert the header compiles standalone — it is the
// self-containment check, not a second definition site.

#include "UI/FontStacks.h"
