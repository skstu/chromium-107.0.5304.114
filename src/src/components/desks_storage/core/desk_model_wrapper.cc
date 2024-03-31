// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/desks_storage/core/desk_model_wrapper.h"

#include "ash/public/cpp/desk_template.h"
#include "base/guid.h"
#include "base/logging.h"
#include "components/account_id/account_id.h"
#include "components/desks_storage/core/desk_model.h"
#include "desk_sync_bridge.h"
#include "local_desk_data_manager.h"

namespace desks_storage {

DeskModelWrapper::DeskModelWrapper(
    desks_storage::DeskModel* save_and_recall_desks_model)
    : save_and_recall_desks_model_(save_and_recall_desks_model) {}

DeskModelWrapper::~DeskModelWrapper() = default;

DeskModel::GetAllEntriesResult DeskModelWrapper::GetAllEntries() {
  DeskModel::GetAllEntriesResult templates_result =
      desk_template_model_->GetAllEntries();

  if (templates_result.status != DeskModel::GetAllEntriesStatus::kOk) {
    return templates_result;
  }

  DeskModel::GetAllEntriesResult save_and_recall_result =
      save_and_recall_desks_model_->GetAllEntries();

  if (save_and_recall_result.status != DeskModel::GetAllEntriesStatus::kOk) {
    return save_and_recall_result;
  }

  std::vector<const ash::DeskTemplate*>& all_entries = templates_result.entries;

  for (auto* const entry : save_and_recall_result.entries)
    all_entries.push_back(entry);

  for (const auto& it : policy_entries_)
    all_entries.push_back(it.get());

  return DeskModel::GetAllEntriesResult(DeskModel::GetAllEntriesStatus::kOk,
                                        std::move(all_entries));
}

DeskModel::GetEntryByUuidResult DeskModelWrapper::GetEntryByUUID(
    const base::GUID& uuid) {
  // Check if this is an admin template uuid first.
  std::unique_ptr<ash::DeskTemplate> policy_entry =
      GetAdminDeskTemplateByUUID(uuid);

  if (policy_entry) {
    return DeskModel::GetEntryByUuidResult(GetEntryByUuidStatus::kOk,
                                           std::move(policy_entry));
  }

  if (GetDeskTemplateModel()->HasUuid(uuid)) {
    return GetDeskTemplateModel()->GetEntryByUUID(uuid);
  } else {
    return save_and_recall_desks_model_->GetEntryByUUID(uuid);
  }
}

void DeskModelWrapper::AddOrUpdateEntry(
    std::unique_ptr<ash::DeskTemplate> new_entry,
    DeskModel::AddOrUpdateEntryCallback callback) {
  if (new_entry->type() == ash::DeskTemplateType::kTemplate) {
    GetDeskTemplateModel()->AddOrUpdateEntry(std::move(new_entry),
                                             std::move(callback));
  } else {
    save_and_recall_desks_model_->AddOrUpdateEntry(std::move(new_entry),
                                                   std::move(callback));
  }
}

void DeskModelWrapper::DeleteEntry(const base::GUID& uuid,
                                   DeskModel::DeleteEntryCallback callback) {
  auto status = std::make_unique<DeskModel::DeleteEntryStatus>();
  if (GetDeskTemplateModel()->HasUuid(uuid)) {
    GetDeskTemplateModel()->DeleteEntry(uuid, std::move(callback));
  } else {
    save_and_recall_desks_model_->DeleteEntry(uuid, std::move(callback));
  }
}

void DeskModelWrapper::DeleteAllEntries(
    DeskModel::DeleteEntryCallback callback) {
  DeskModel::DeleteEntryStatus desk_template_delete_status =
      GetDeskTemplateModel()->DeleteAllEntriesSync();
  if (desk_template_delete_status != DeskModel::DeleteEntryStatus::kOk) {
    std::move(callback).Run(desk_template_delete_status);
    return;
  }
  save_and_recall_desks_model_->DeleteAllEntries(
      base::BindOnce(&DeskModelWrapper::OnDeleteAllEntries,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

// TODO(crbug.com/1320805): Remove this function once both desk models support
// desk type counts.
size_t DeskModelWrapper::GetEntryCount() const {
  return GetSaveAndRecallDeskEntryCount() + GetDeskTemplateEntryCount();
}

size_t DeskModelWrapper::GetSaveAndRecallDeskEntryCount() const {
  return save_and_recall_desks_model_->GetSaveAndRecallDeskEntryCount();
}

size_t DeskModelWrapper::GetDeskTemplateEntryCount() const {
  return GetDeskTemplateModel()->GetDeskTemplateEntryCount() +
         policy_entries_.size();
}

size_t DeskModelWrapper::GetMaxEntryCount() const {
  return GetMaxSaveAndRecallDeskEntryCount() + GetMaxDeskTemplateEntryCount();
}

size_t DeskModelWrapper::GetMaxSaveAndRecallDeskEntryCount() const {
  return save_and_recall_desks_model_->GetMaxSaveAndRecallDeskEntryCount();
}

size_t DeskModelWrapper::GetMaxDeskTemplateEntryCount() const {
  return GetDeskTemplateModel()->GetMaxDeskTemplateEntryCount() +
         policy_entries_.size();
}

std::vector<base::GUID> DeskModelWrapper::GetAllEntryUuids() const {
  std::vector<base::GUID> keys;

  for (const auto& it : policy_entries_)
    keys.push_back(it.get()->uuid());

  for (const auto& save_and_recall_uuid :
       save_and_recall_desks_model_->GetAllEntryUuids()) {
    keys.emplace_back(save_and_recall_uuid);
  }

  for (const auto& desk_template_uuid :
       GetDeskTemplateModel()->GetAllEntryUuids()) {
    keys.emplace_back(desk_template_uuid);
  }
  return keys;
}

bool DeskModelWrapper::IsReady() const {
  return save_and_recall_desks_model_->IsReady() &&
         GetDeskTemplateModel()->IsReady();
}

bool DeskModelWrapper::IsSyncing() const {
  return GetDeskTemplateModel()->IsSyncing();
}

ash::DeskTemplate* DeskModelWrapper::FindOtherEntryWithName(
    const std::u16string& name,
    ash::DeskTemplateType type,
    const base::GUID& uuid) const {
  if (type == ash::DeskTemplateType::kTemplate) {
    return GetDeskTemplateModel()->FindOtherEntryWithName(name, type, uuid);
  } else {
    return save_and_recall_desks_model_->FindOtherEntryWithName(name, type,
                                                                uuid);
  }
}

desks_storage::DeskSyncBridge* DeskModelWrapper::GetDeskTemplateModel() const {
  DCHECK(desk_template_model_);
  return desk_template_model_;
}

void DeskModelWrapper::OnDeleteAllEntries(
    DeskModel::DeleteEntryCallback callback,
    desks_storage::DeskModel::DeleteEntryStatus status) {
  std::move(callback).Run(status);
}

}  // namespace desks_storage
