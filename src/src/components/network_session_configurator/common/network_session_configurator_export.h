// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_NETWORK_SESSION_CONFIGURATOR_COMMON_NETWORK_SESSION_CONFIGURATOR_EXPORT_H_
#define COMPONENTS_NETWORK_SESSION_CONFIGURATOR_COMMON_NETWORK_SESSION_CONFIGURATOR_EXPORT_H_

#if defined(COMPONENT_BUILD)

#if defined(WIN32)

#if defined(NETWORK_SESSION_CONFIGURATOR_IMPLEMENTATION)
#define NETWORK_SESSION_CONFIGURATOR_EXPORT __declspec(dllexport)
#else
#define NETWORK_SESSION_CONFIGURATOR_EXPORT __declspec(dllimport)
#endif  // defined(NETWORK_SESSION_CONFIGURATOR_IMPLEMENTATION)

#else  // defined(WIN32)

#if defined(NETWORK_SESSION_CONFIGURATOR_IMPLEMENTATION)
#define NETWORK_SESSION_CONFIGURATOR_EXPORT \
  __attribute__((visibility("default")))
#else
#define NETWORK_SESSION_CONFIGURATOR_EXPORT
#endif  // defined(NETWORK_SESSION_CONFIGURATOR_IMPLEMENTATION)

#endif  // defined(WIN32)

#else  // defined(COMPONENT_BUILD)

#define NETWORK_SESSION_CONFIGURATOR_EXPORT

#endif  // defined(COMPONENT_BUILD)

#endif  // COMPONENTS_NETWORK_SESSION_CONFIGURATOR_COMMON_NETWORK_SESSION_CONFIGURATOR_EXPORT_H_
