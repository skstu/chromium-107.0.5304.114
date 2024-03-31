// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SERVICES_IME_PUBLIC_CPP_RULEBASED_DEF_TH_TIS_H_
#define ASH_SERVICES_IME_PUBLIC_CPP_RULEBASED_DEF_TH_TIS_H_

namespace th_tis {

// The id of this IME/keyboard.
extern const char* kId;

// Whether this keyboard layout is a 102 or 101 keyboard.
extern bool kIs102;

// The key mapping definitions under various modifier states.
extern const char** kKeyMap[8];

}  // namespace th_tis

#endif  // ASH_SERVICES_IME_PUBLIC_CPP_RULEBASED_DEF_TH_TIS_H_