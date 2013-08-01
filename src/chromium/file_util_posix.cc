// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "file_util.hh"

#include <unistd.h>
#include <stdio.h>

namespace chromium {

bool PathExists(const std::string& path) {
  return access(path.c_str(), F_OK) == 0;
}

}  // namespace chromium

// -----------------------------------------------------------------------------

namespace file_util {

FILE* OpenFile(const std::string& filename, const char* mode) {
  FILE* result = NULL;
  do {
    result = fopen(filename.c_str(), mode);
  } while (!result && errno == EINTR);
  return result;
}

}  // namespace file_util
