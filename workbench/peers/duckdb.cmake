# Official release C API. The engine is prebuilt; caller flags do not describe it.
include_guard(GLOBAL)
include(FetchContent)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "This DuckDB control is pinned for Linux")
endif()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
  set(duckdb_platform arm64)
  set(duckdb_sha c60eecf91143ca6bbac55c4f4d40dc22520488b0ac257c86e0fe9a2b451cf2b7)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
  set(duckdb_platform amd64)
  set(duckdb_sha 993ad89e7dfca66cd6da994c2cad090c2fc160ddf9f550498a098ab4bb13cfb1)
else()
  message(FATAL_ERROR "No pinned DuckDB release for this architecture")
endif()
set(WORKBENCH_DUCKDB_VERSION "2.0.0")
set(duckdb_url "https://github.com/duckdb/duckdb/releases/download/v${WORKBENCH_DUCKDB_VERSION}/duckdb-shared-libs-linux-${duckdb_platform}.tar.gz")
# A local archive avoids downloading existing verified bytes again. The same
# published digest is required; this is not a source-directory bypass.
set(WORKBENCH_DUCKDB_ARCHIVE "" CACHE FILEPATH "Existing hash-verified DuckDB release archive")
if(WORKBENCH_DUCKDB_ARCHIVE)
  set(duckdb_url "${WORKBENCH_DUCKDB_ARCHIVE}")
endif()
if(FETCHCONTENT_SOURCE_DIR_WORKBENCH_DUCKDB)
  message(FATAL_ERROR "DuckDB requires the hash-verified release archive")
endif()
FetchContent_Declare(workbench_duckdb URL "${duckdb_url}"
  URL_HASH "SHA256=${duckdb_sha}" SOURCE_SUBDIR include DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(workbench_duckdb)
add_library(workbench_duckdb_c SHARED IMPORTED GLOBAL)
set_target_properties(workbench_duckdb_c PROPERTIES
  IMPORTED_LOCATION "${workbench_duckdb_SOURCE_DIR}/libduckdb.so"
  INTERFACE_INCLUDE_DIRECTORIES "${workbench_duckdb_SOURCE_DIR}")
add_library(workbench::duckdb ALIAS workbench_duckdb_c)
