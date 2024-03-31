// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/password_manager/core/browser/android_affiliation/affiliation_database.h"

#include <stdint.h>

#include <memory>

#include <set>
#include <vector>

#include "base/bind.h"
#include "base/containers/flat_set.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "build/build_config.h"
#include "sql/database.h"
#include "sql/error_delegate_util.h"
#include "sql/meta_table.h"
#include "sql/statement.h"
#include "sql/transaction.h"

namespace password_manager {

namespace {

// The current version number of the affiliation database schema.
const int kVersion = 3;

// The oldest version of the schema such that a legacy Chrome client using that
// version can still read/write the current database.
const int kCompatibleVersion = 1;

// Struct to hold table builder for "eq_classes", "eq_class_members",
// and "eq_class_groups" tables.
struct SQLTableBuilders {
  SQLTableBuilder* eq_classes;
  SQLTableBuilder* eq_class_members;
  SQLTableBuilder* eq_class_groups;
};

// Seals the version of the given builders. This is method should be always used
// to seal versions of all builder to make sure all builders are at the same
// version.
void SealVersion(SQLTableBuilders builders, unsigned expected_version) {
  unsigned eq_classes_version = builders.eq_classes->SealVersion();
  DCHECK_EQ(expected_version, eq_classes_version);

  unsigned eq_class_members_version = builders.eq_class_members->SealVersion();
  DCHECK_EQ(expected_version, eq_class_members_version);

  unsigned eq_class_groups_version = builders.eq_class_groups->SealVersion();
  DCHECK_EQ(expected_version, eq_class_groups_version);
}

// Initializes the passed in table builders and defines the structure of the
// tables.
void InitializeTableBuilders(SQLTableBuilders builders) {
  // Version 0 and 1 of the affiliation database.
  builders.eq_classes->AddPrimaryKeyColumn("id");
  builders.eq_classes->AddColumn("last_update_time", "INTEGER");
  builders.eq_class_members->AddPrimaryKeyColumn("id");
  builders.eq_class_members->AddColumnToUniqueKey("facet_uri",
                                                  "LONGVARCHAR NOT NULL");
  builders.eq_class_members->AddColumn(
      "set_id", "INTEGER NOT NULL REFERENCES eq_classes(id) ON DELETE CASCADE");
  // An index on eq_class_members.facet_uri is automatically created due to the
  // UNIQUE constraint, however, we must create one on eq_class_members.set_id
  // manually (to prevent linear scan when joining).
  builders.eq_class_members->AddIndex("index_on_eq_class_members_set_id",
                                      {"set_id"});
  SealVersion(builders, /*expected_version=*/0u);
  SealVersion(builders, /*expected_version=*/1u);

  // Version 2 of the affiliation database.
  builders.eq_class_members->AddColumn("facet_display_name", "VARCHAR");
  builders.eq_class_members->AddColumn("facet_icon_url", "VARCHAR");
  SealVersion(builders, /*expected_version=*/2u);

  // Version 3 of the affiliation database.
  builders.eq_class_groups->AddPrimaryKeyColumn("id");
  builders.eq_class_groups->AddColumn("facet_uri", "LONGVARCHAR NOT NULL");
  builders.eq_class_groups->AddColumn(
      "set_id", "INTEGER NOT NULL REFERENCES eq_classes(id) ON DELETE CASCADE");
  builders.eq_classes->AddColumn("group_display_name", "VARCHAR");
  builders.eq_classes->AddColumn("group_icon_url", "VARCHAR");
  SealVersion(builders, /*expected_version=*/3u);
}

// Creates the tables in the database using the provided table builders.
// Returns |false| on error, |true| on success.
bool CreateTables(SQLTableBuilders builders, sql::Database* db) {
  return builders.eq_classes->CreateTable(db) &&
         builders.eq_class_members->CreateTable(db) &&
         builders.eq_class_groups->CreateTable(db);
}

// Migrates an existing database from an earlier |version| using the provided
// table builders. Returns |false| on error, |true| on success.
bool MigrateTablesFrom(SQLTableBuilders builders,
                       unsigned version,
                       sql::Database* db) {
  return builders.eq_classes->MigrateFrom(version, db) &&
             builders.eq_class_members->MigrateFrom(version, db),
         builders.eq_class_groups->MigrateFrom(version, db);
}

}  // namespace

AffiliationDatabase::AffiliationDatabase() = default;

AffiliationDatabase::~AffiliationDatabase() = default;

bool AffiliationDatabase::Init(const base::FilePath& path) {
  sql_connection_ = std::make_unique<sql::Database>();
  sql_connection_->set_histogram_tag("Affiliation");
  sql_connection_->set_error_callback(base::BindRepeating(
      &AffiliationDatabase::SQLErrorCallback, base::Unretained(this)));

  if (!sql_connection_->Open(path))
    return false;

  if (!sql_connection_->Execute("PRAGMA foreign_keys=1")) {
    sql_connection_->Poison();
    return false;
  }

  sql::MetaTable metatable;
  if (!metatable.Init(sql_connection_.get(), kVersion, kCompatibleVersion)) {
    sql_connection_->Poison();
    return false;
  }

  if (metatable.GetCompatibleVersionNumber() > kVersion) {
    LOG(WARNING) << "AffiliationDatabase is too new.";
    sql_connection_->Poison();
    return false;
  }

  SQLTableBuilder eq_classes_builder("eq_classes");
  SQLTableBuilder eq_class_members_builder("eq_class_members");
  SQLTableBuilder eq_class_groups_builder("eq_class_groups");
  SQLTableBuilders builders = {&eq_classes_builder, &eq_class_members_builder,
                               &eq_class_groups_builder};
  InitializeTableBuilders(builders);

  if (!CreateTables(builders, sql_connection_.get())) {
    LOG(WARNING) << "Failed to create tables.";
    sql_connection_->Poison();
    return false;
  }

  int version = metatable.GetVersionNumber();
  if (version < kVersion) {
    if (!MigrateTablesFrom(builders, version, sql_connection_.get())) {
      LOG(WARNING) << "Failed to migrate tables from version " << version
                   << ".";
      sql_connection_->Poison();
      return false;
    }

    // Set the current version number is case of a successful migration.
    metatable.SetVersionNumber(kVersion);
  }

  return true;
}

bool AffiliationDatabase::GetAffiliationsAndBrandingForFacetURI(
    const FacetURI& facet_uri,
    AffiliatedFacetsWithUpdateTime* result) const {
  DCHECK(result);
  result->facets.clear();

  sql::Statement statement(sql_connection_->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT m2.facet_uri, m2.facet_display_name, m2.facet_icon_url,"
      "    c.last_update_time "
      "FROM eq_class_members m1, eq_class_members m2, eq_classes c "
      "WHERE m1.facet_uri = ? AND m1.set_id = m2.set_id AND m1.set_id = c.id"));
  statement.BindString(0, facet_uri.canonical_spec());

  while (statement.Step()) {
    result->facets.push_back(
        {FacetURI::FromCanonicalSpec(statement.ColumnString(0)),
         FacetBrandingInfo{
             statement.ColumnString(1), GURL(statement.ColumnString(2)),
         }});
    result->last_update_time =
        base::Time::FromInternalValue(statement.ColumnInt64(3));
  }

  return !result->facets.empty();
}

void AffiliationDatabase::GetAllAffiliationsAndBranding(
    std::vector<AffiliatedFacetsWithUpdateTime>* results) const {
  DCHECK(results);
  results->clear();

  sql::Statement statement(sql_connection_->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT m.facet_uri, m.facet_display_name, m.facet_icon_url,"
      "    c.last_update_time, c.id "
      "FROM eq_class_members m, eq_classes c "
      "WHERE m.set_id = c.id "
      "ORDER BY c.id"));

  int64_t last_eq_class_id = 0;
  while (statement.Step()) {
    int64_t eq_class_id = statement.ColumnInt64(4);
    if (results->empty() || eq_class_id != last_eq_class_id) {
      results->push_back(AffiliatedFacetsWithUpdateTime());
      last_eq_class_id = eq_class_id;
    }
    results->back().facets.push_back(
        {FacetURI::FromCanonicalSpec(statement.ColumnString(0)),
         FacetBrandingInfo{
             statement.ColumnString(1), GURL(statement.ColumnString(2)),
         }});
    results->back().last_update_time =
        base::Time::FromInternalValue(statement.ColumnInt64(3));
  }
}

std::vector<GroupedFacets> AffiliationDatabase::GetAllGroups() const {
  std::vector<GroupedFacets> results;

  sql::Statement statement(sql_connection_->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT g.facet_uri, c.id, c.group_display_name, c.group_icon_url "
      "FROM eq_class_groups g, eq_classes c "
      "WHERE g.set_id = c.id "
      "ORDER BY c.id"));

  int64_t last_eq_class_id = 0;
  while (statement.Step()) {
    int64_t eq_class_id = statement.ColumnInt64(1);
    if (results.empty() || eq_class_id != last_eq_class_id) {
      GroupedFacets group;
      group.branding_info = FacetBrandingInfo{
          statement.ColumnString(2),
          GURL(statement.ColumnString(3)),
      };
      results.push_back(std::move(group));
      last_eq_class_id = eq_class_id;
    }
    results.back().facets.push_back(
        {FacetURI::FromCanonicalSpec(statement.ColumnString(0))});
  }
  return results;
}

void AffiliationDatabase::DeleteAffiliationsAndBrandingForFacetURI(
    const FacetURI& facet_uri) {
  sql::Transaction transaction(sql_connection_.get());
  if (!transaction.Begin())
    return;

  sql::Statement statement_lookup(sql_connection_->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT m.set_id FROM eq_class_members m "
      "WHERE m.facet_uri = ?"));
  statement_lookup.BindString(0, facet_uri.canonical_spec());

  // No such |facet_uri|, nothing to do.
  if (!statement_lookup.Step())
    return;

  int64_t eq_class_id = statement_lookup.ColumnInt64(0);

  // Children will get deleted due to 'ON DELETE CASCADE'.
  sql::Statement statement_parent(sql_connection_->GetCachedStatement(
      SQL_FROM_HERE, "DELETE FROM eq_classes WHERE eq_classes.id = ?"));
  statement_parent.BindInt64(0, eq_class_id);
  if (!statement_parent.Run())
    return;

  transaction.Commit();
}

bool AffiliationDatabase::Store(
    const AffiliatedFacetsWithUpdateTime& affiliated_facets,
    const GroupedFacets& group) {
  DCHECK(!affiliated_facets.facets.empty());
  sql::Statement statement_parent(sql_connection_->GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT INTO eq_classes(last_update_time, group_display_name, "
      "group_icon_url) VALUES (?, ?, ?)"));

  sql::Statement statement_child(sql_connection_->GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT INTO "
      "eq_class_members(facet_uri, facet_display_name, facet_icon_url, set_id) "
      "VALUES (?, ?, ?, ?)"));

  sql::Statement statement_groups(sql_connection_->GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT INTO eq_class_groups(facet_uri, set_id) VALUES (?, ?)"));

  sql::Transaction transaction(sql_connection_.get());
  if (!transaction.Begin())
    return false;

  statement_parent.BindInt64(
      0, affiliated_facets.last_update_time.ToInternalValue());
  statement_parent.BindString(1, group.branding_info.name);
  statement_parent.BindString(
      2, group.branding_info.icon_url.possibly_invalid_spec());
  if (!statement_parent.Run())
    return false;

  int64_t eq_class_id = sql_connection_->GetLastInsertRowId();
  for (const Facet& facet : affiliated_facets.facets) {
    statement_child.Reset(true);
    statement_child.BindString(0, facet.uri.canonical_spec());
    statement_child.BindString(1, facet.branding_info.name);
    statement_child.BindString(
        2, facet.branding_info.icon_url.possibly_invalid_spec());
    statement_child.BindInt64(3, eq_class_id);
    if (!statement_child.Run())
      return false;
  }
  for (const Facet& facet : group.facets) {
    statement_groups.Reset(true);
    statement_groups.BindString(0, facet.uri.canonical_spec());
    statement_groups.BindInt64(1, eq_class_id);
    if (!statement_groups.Run())
      return false;
  }

  return transaction.Commit();
}

void AffiliationDatabase::StoreAndRemoveConflicting(
    const AffiliatedFacetsWithUpdateTime& affiliation,
    const GroupedFacets& group,
    std::vector<AffiliatedFacetsWithUpdateTime>* removed_affiliations) {
  DCHECK(!affiliation.facets.empty());
  DCHECK(removed_affiliations);
  removed_affiliations->clear();

  sql::Transaction transaction(sql_connection_.get());
  if (!transaction.Begin())
    return;

  for (const Facet& facet : affiliation.facets) {
    AffiliatedFacetsWithUpdateTime old_affiliation;
    if (GetAffiliationsAndBrandingForFacetURI(facet.uri, &old_affiliation)) {
      if (!AreEquivalenceClassesEqual(old_affiliation.facets,
                                      affiliation.facets)) {
        removed_affiliations->push_back(old_affiliation);
      }
      DeleteAffiliationsAndBrandingForFacetURI(facet.uri);
    }
  }

  if (!Store(affiliation, group))
    NOTREACHED();

  transaction.Commit();
}

void AffiliationDatabase::RemoveMissingFacetURI(
    std::vector<FacetURI> facet_uris) {
  sql::Transaction transaction(sql_connection_.get());
  if (!transaction.Begin())
    return;

  auto current_facets = base::MakeFlatSet<std::string>(
      facet_uris, {}, &FacetURI::potentially_invalid_spec);

  std::set<int64_t> all_ids, found_ids;
  sql::Statement statement(sql_connection_->GetUniqueStatement(
      "SELECT m.facet_uri, m.set_id FROM eq_class_members m"));

  // For every facet in the database check if it exists in |current_facets|.
  while (statement.Step()) {
    std::string facet_uri = statement.ColumnString(0);
    int64_t eq_class_id = statement.ColumnInt64(1);

    all_ids.insert(eq_class_id);
    if (current_facets.contains(facet_uri)) {
      found_ids.insert(eq_class_id);
    }
  }

  // Remove any equivalence class which aren't represented in |current_facets|.
  for (const auto id : all_ids) {
    if (found_ids.find(id) == found_ids.end()) {
      sql::Statement statement_parent(sql_connection_->GetCachedStatement(
          SQL_FROM_HERE, "DELETE FROM eq_classes WHERE eq_classes.id = ?"));
      statement_parent.BindInt64(0, id);
      statement_parent.Run();
    }
  }

  transaction.Commit();
}

// static
void AffiliationDatabase::Delete(const base::FilePath& path) {
  bool success = sql::Database::Delete(path);
  DCHECK(success);
}

int AffiliationDatabase::GetDatabaseVersionForTesting() {
  sql::MetaTable metatable;
  // The second and third parameters to |MetaTable::Init| are ignored, given
  // that a metatable already exists. Hence they are not influencing the version
  // of the underlying database.
  DCHECK(sql::MetaTable::DoesTableExist(sql_connection_.get()));
  metatable.Init(sql_connection_.get(), 1, 1);
  return metatable.GetVersionNumber();
}

void AffiliationDatabase::SQLErrorCallback(int error,
                                           sql::Statement* statement) {
  if (sql::IsErrorCatastrophic(error)) {
    // Normally this will poison the database, causing any subsequent operations
    // to silently fail without any side effects. However, if RazeAndClose() is
    // called from the error callback in response to an error raised from within
    // sql::Database::Open, opening the now-razed database will be retried.
    sql_connection_->RazeAndClose();
    return;
  }

  // The default handling is to assert on debug and to ignore on release.
  if (!sql::Database::IsExpectedSqliteError(error))
    DLOG(FATAL) << sql_connection_->GetErrorMessage();
}

}  // namespace password_manager
