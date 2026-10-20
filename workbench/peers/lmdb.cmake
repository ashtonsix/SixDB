# Actual LMDB only; no SixDB schema, workload, transaction or flush policy.
include_guard(GLOBAL)
find_path(SIXDB_LMDB_INCLUDE_DIR NAMES lmdb.h REQUIRED)
find_library(SIXDB_LMDB_LIBRARY NAMES lmdb REQUIRED)
add_library(workbench_lmdb UNKNOWN IMPORTED GLOBAL)
set_target_properties(workbench_lmdb PROPERTIES
  IMPORTED_LOCATION "${SIXDB_LMDB_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${SIXDB_LMDB_INCLUDE_DIR}")
add_library(workbench::lmdb ALIAS workbench_lmdb)
