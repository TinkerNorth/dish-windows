// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// How a Qt transport failure is named to the user.
//
// Its own header, not HttpGateways.h's: that one forward-declares QNetworkReply
// so nothing downstream has to see Qt Network, and naming NetworkError needs the
// whole type. Only the gateways and their test include this.
//
// Pure: no reply, no state, no clock.

#pragma once

#include "core/reducer/UpdateMachine.h"

#include <QNetworkReply>

namespace dish::update {

// A transport failure that means "this machine cannot reach the internet right
// now" rather than "GitHub said no". The distinction only changes the copy in
// Settings; both back off identically, so the cost of getting it wrong is
// telling a user they are offline when the host answered, and sending them to
// look at the wrong thing.
reducer::UpdateError classifyNetworkError(QNetworkReply::NetworkError error);

} // namespace dish::update
