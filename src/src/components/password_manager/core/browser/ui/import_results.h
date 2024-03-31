// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PASSWORD_MANAGER_CORE_BROWSER_UI_IMPORT_RESULTS_H_
#define COMPONENTS_PASSWORD_MANAGER_CORE_BROWSER_UI_IMPORT_RESULTS_H_

#include <string>
#include <vector>

namespace password_manager {

struct ImportEntry {
  // Matches api::passwords_private::ImportEntryStatus.
  enum Status {
    // Should not be used.
    NONE = 0,
    // Any other error state.
    UNKNOWN_ERROR = 1,
    // Missing password field.
    MISSING_PASSWORD = 2,
    // Missing url field.
    MISSING_URL = 3,
    // Bad url formatting.
    INVALID_URL = 4,
    // URL contains non-ASCII chars.
    NON_ASCII_URL = 5,
    // URL is too long.
    LONG_URL = 6,
    // Password is too long.
    LONG_PASSWORD = 7,
    // Username is too long.
    LONG_USERNAME = 8,
    // Credential is already stored in profile store.
    CONFLICT_PROFILE = 9,
    // Credential is already stored in account store.
    CONFLICT_ACCOUNT = 10,
    kMaxValue = CONFLICT_ACCOUNT
  };

  // The status of parsing for individual row that represents a credential
  // during import process.
  Status status;

  // The url of the credential.
  std::string url;

  // The username of the credential.
  std::string username;

  ImportEntry();
  ImportEntry(const ImportEntry& other);
  ImportEntry(ImportEntry&& other);
  ~ImportEntry();

  ImportEntry& operator=(const ImportEntry& entry);
  ImportEntry& operator=(ImportEntry&& entry);
};

struct ImportResults {
  // Matches api::passwords_private::ImportResultsStatus.
  enum Status {
    // Should not be used.
    NONE = 0,
    // Any other error state.
    UNKNOWN_ERROR = 1,
    // Data was fully or partially imported.
    SUCCESS = 2,
    // Failed to read provided file.
    IO_ERROR = 3,
    // Header is missing, invalid or could not be read.
    BAD_FORMAT = 4,
    // File selection dismissed.
    DISMISSED = 5,
    // Size of the chosen file exceeds the limit.
    MAX_FILE_SIZE = 6,
    // User has already started the import flow in a different window.
    IMPORT_ALREADY_ACTIVE = 7,
    // User tried to import too many passwords from one file.
    NUM_PASSWORDS_EXCEEDED = 8,
    kMaxValue = NUM_PASSWORDS_EXCEEDED
  };

  // General status of the triggered password import process.
  Status status;

  // Number of successfully imported passwords.
  size_t number_imported = 0;

  // Possibly empty, list of credentials that couldn't be imported.
  std::vector<ImportEntry> failed_imports;

  // Possibly not set, name of file that user has chosen for the import.
  std::string file_name;

  ImportResults();
  ImportResults(const ImportResults& other);
  ImportResults(ImportResults&& other);
  ~ImportResults();

  ImportResults& operator=(const ImportResults& entry);
  ImportResults& operator=(ImportResults&& entry);
};

}  // namespace password_manager

#endif  // COMPONENTS_PASSWORD_MANAGER_CORE_BROWSER_UI_IMPORT_RESULTS_H_
