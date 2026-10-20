#include "lmdb.h"
#include <cerrno>

namespace index_spike::lmdb_peer {
Environment::~Environment() { close(); }
void Environment::close() noexcept {
  if (handle) mdb_env_close(handle);
  handle=nullptr;
  db=0;
}
int open(Environment& destination,const char* path,std::size_t map_bytes,
    unsigned flags) noexcept {
  if (destination.handle) return EINVAL;
  Environment candidate;
  int status=mdb_env_create(&candidate.handle);
  if (status) return status;
  status=mdb_env_set_mapsize(candidate.handle,map_bytes);
  if (status) return status;
  status=mdb_env_open(candidate.handle,path,flags,0600);
  if (status) return status;
  MDB_txn* setup{};
  status=mdb_txn_begin(candidate.handle,nullptr,flags&MDB_RDONLY,&setup);
  if (status) return status;
  status=mdb_dbi_open(setup,nullptr,0,&candidate.db);
  if (status) { mdb_txn_abort(setup); return status; }
  status=mdb_txn_commit(setup);
  if (status) return status;
  destination.handle=candidate.handle;
  destination.db=candidate.db;
  candidate.handle=nullptr;
  return 0;
}
int inspect(const Environment& env,Info& info) noexcept {
  if (!env.handle) return EINVAL;
  info.version=mdb_version(nullptr,nullptr,nullptr);
  int status=mdb_env_get_flags(env.handle,&info.flags);
  if (status) return status;
  return mdb_env_info(env.handle,&info.environment);
}
} // namespace index_spike::lmdb_peer
