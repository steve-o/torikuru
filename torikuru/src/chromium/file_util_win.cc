// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "file_util.hh"

#include <share.h>

namespace file_util {

FILE* OpenFile(const std::string& filename, const char* mode) {
  return _fsopen(filename.c_str(), mode, _SH_DENYNO);
}

}  // namespace file_util
