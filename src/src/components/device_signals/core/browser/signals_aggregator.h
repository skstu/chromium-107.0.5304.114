// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_DEVICE_SIGNALS_CORE_BROWSER_SIGNALS_AGGREGATOR_H_
#define COMPONENTS_DEVICE_SIGNALS_CORE_BROWSER_SIGNALS_AGGREGATOR_H_

#include "base/callback_forward.h"
#include "components/keyed_service/core/keyed_service.h"

namespace device_signals {

struct SignalsAggregationRequest;
struct SignalsAggregationResponse;

class SignalsAggregator : public KeyedService {
 public:
  using GetSignalsCallback =
      base::OnceCallback<void(SignalsAggregationResponse)>;

  ~SignalsAggregator() override = default;

  // Will asynchronously collect signals whose names are specified in the
  // `request` object, and will also use its user context to validate that the
  // user has permissions to the device's signals. Invokes `callback` with the
  // collected signals. Currently only supports the collection of one signal
  // (only one entry in `request.signal_names`) for simplicity as no current use
  // case require supporting collecting multiple signals in one request.
  virtual void GetSignals(const SignalsAggregationRequest& request,
                          GetSignalsCallback callback) = 0;
};

}  // namespace device_signals

#endif  // COMPONENTS_DEVICE_SIGNALS_CORE_BROWSER_SIGNALS_AGGREGATOR_H_
