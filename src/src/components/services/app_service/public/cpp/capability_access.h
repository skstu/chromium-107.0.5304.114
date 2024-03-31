// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SERVICES_APP_SERVICE_PUBLIC_CPP_CAPABILITY_ACCESS_H_
#define COMPONENTS_SERVICES_APP_SERVICE_PUBLIC_CPP_CAPABILITY_ACCESS_H_

#include <string>
#include <utility>

#include "base/component_export.h"
#include "components/services/app_service/public/mojom/types.mojom.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace apps {

// Information about whether an app is accessing some capability, e.g. camera,
// microphone.
struct COMPONENT_EXPORT(APP_TYPES) CapabilityAccess {
  explicit CapabilityAccess(const std::string& app_id);

  CapabilityAccess(const CapabilityAccess&) = delete;
  CapabilityAccess& operator=(const CapabilityAccess&) = delete;

  ~CapabilityAccess();

  std::unique_ptr<CapabilityAccess> Clone() const;

  std::string app_id;

  // Whether the app is accessing camera.
  absl::optional<bool> camera;

  // Whether the app is accessing microphone.
  absl::optional<bool> microphone;

  // When adding new fields, also update the Merge method and other helpers in
  // components/services/app_service/public/cpp/CapabilityAccessUpdate.*
};

using CapabilityAccessPtr = std::unique_ptr<CapabilityAccess>;

// TODO(crbug.com/1253250): Remove these functions after migrating to non-mojo
// AppService.
COMPONENT_EXPORT(APP_TYPES)
CapabilityAccessPtr ConvertMojomCapabilityAccessToCapabilityAccess(
    const apps::mojom::CapabilityAccessPtr& mojom_capability_access);

COMPONENT_EXPORT(APP_TYPES)
apps::mojom::CapabilityAccessPtr ConvertCapabilityAccessToMojomCapabilityAccess(
    const CapabilityAccessPtr& capability_access);

COMPONENT_EXPORT(APP_TYPES)
std::vector<CapabilityAccessPtr>
ConvertMojomCapabilityAccessesToCapabilityAccesses(
    const std::vector<apps::mojom::CapabilityAccessPtr>&
        mojom_capability_accesses);

COMPONENT_EXPORT(APP_TYPES)
std::vector<apps::mojom::CapabilityAccessPtr>
ConvertCapabilityAccessesToMojomCapabilityAccesses(
    const std::vector<CapabilityAccessPtr>& capability_accesses);

}  // namespace apps

#endif  // COMPONENTS_SERVICES_APP_SERVICE_PUBLIC_CPP_CAPABILITY_ACCESS_H_
