#pragma once

#include "config/schema/diagnostics.h"
#include "config/schema/field.h"
#include "core/toml.h"

#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace noctalia::config::schema {

  // Populate `out` from `tbl` by running every field's reader. Absent keys leave
  // the struct default. Replaces a hand-written parseTableInto section.
  template <typename Struct>
  void readInto(
      const toml::table& tbl, Struct& out, const Schema<Struct>& schema, std::string_view parentPath, Diagnostics& diag
  ) {
    for (const auto& f : schema) {
      f.read(tbl, out, parentPath, diag);
    }
  }

  // Serialize `in` into a fresh table by running every field's writer, in schema
  // order. Replaces a hand-written configToToml section.
  template <typename Struct> toml::table writeTable(const Struct& in, const Schema<Struct>& schema) {
    toml::table tbl;
    for (const auto& f : schema) {
      f.write(tbl, in);
    }
    return tbl;
  }

  // Append dotted paths of keys in `tbl` that no field in `schema` recognizes,
  // recursing through fields that carry a child-validator (sub-tables).
  template <typename Struct>
  void collectUnknownKeys(
      const toml::table& tbl, const Schema<Struct>& schema, std::string_view parentPath,
      std::vector<std::string>& unknown
  ) {
    std::unordered_set<std::string_view> known;
    known.reserve(schema.size());
    for (const auto& f : schema) {
      known.insert(f.key);
    }
    for (const auto& [key, node] : tbl) {
      (void)node;
      if (!known.contains(key.str())) {
        unknown.push_back(joinPath(parentPath, key.str()));
      }
    }
    for (const auto& f : schema) {
      if (f.findUnknown) {
        f.findUnknown(tbl, parentPath, unknown);
      }
    }
  }

  // Nested struct under a fixed key (e.g. shell.shadow). Reads/writes via the
  // sub-schema and recurses for unknown-key detection.
  template <typename Struct, typename Sub>
  Field<Struct> subTable(Sub Struct::* member, std::string_view key, const Schema<Sub>& subSchema) {
    return Field<Struct>{
        key,
        [member, key, &subSchema](const toml::table& tbl, Struct& out, std::string_view parentPath, Diagnostics& diag) {
          if (auto* sub = tbl[key].as_table()) {
            readInto(*sub, out.*member, subSchema, joinPath(parentPath, key), diag);
          }
        },
        [member, key, &subSchema](toml::table& tbl, const Struct& in) {
          tbl.insert_or_assign(key, writeTable(in.*member, subSchema));
        },
        [key, &subSchema](const toml::table& tbl, std::string_view parentPath, std::vector<std::string>& unknown) {
          if (auto* sub = tbl[key].as_table()) {
            collectUnknownKeys(*sub, subSchema, joinPath(parentPath, key), unknown);
          }
        },
    };
  }

} // namespace noctalia::config::schema
