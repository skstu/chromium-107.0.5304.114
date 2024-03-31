// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SERVICES_APP_SERVICE_PUBLIC_CPP_FEATURES_H_
#define COMPONENTS_SERVICES_APP_SERVICE_PUBLIC_CPP_FEATURES_H_

#include "base/component_export.h"
#include "base/feature_list.h"

namespace apps {

COMPONENT_EXPORT(APP_TYPES)
extern const base::Feature kAppServicePreferredAppsWithoutMojom;
COMPONENT_EXPORT(APP_TYPES)
extern const base::Feature kAppServiceLaunchWithoutMojom;
COMPONENT_EXPORT(APP_TYPES)
extern const base::Feature kAppServiceSetPermissionWithoutMojom;
COMPONENT_EXPORT(APP_TYPES)
extern const base::Feature kAppServiceUninstallWithoutMojom;
COMPONENT_EXPORT(APP_TYPES)
extern const base::Feature kAppServiceWithoutMojom;
COMPONENT_EXPORT(APP_TYPES)
extern const base::Feature kAppServiceGetMenuWithoutMojom;
COMPONENT_EXPORT(APP_TYPES)
extern const base::Feature kAppServiceCapabilityAccessWithoutMojom;
COMPONENT_EXPORT(APP_TYPES)
extern const base::Feature kStopMojomAppService;

}  // namespace apps

#endif  // COMPONENTS_SERVICES_APP_SERVICE_PUBLIC_CPP_FEATURES_H_
