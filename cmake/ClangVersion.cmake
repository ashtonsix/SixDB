# Initial pin: already installed in the Linux development VM. Change this file
# deliberately when moving the project to another compiler release.
set(SIXDB_CLANG_VERSION "21.1.8")
string(REGEX MATCH "^[0-9]+" SIXDB_CLANG_MAJOR "${SIXDB_CLANG_VERSION}")
