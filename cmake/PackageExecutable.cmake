if(NOT CONFIG STREQUAL "Release")
  message(FATAL_ERROR "Build distribution artifacts with the release configuration")
endif()
get_filename_component(binary_dir "${BINARY}" DIRECTORY)
get_filename_component(symbols_dir "${SYMBOLS}" DIRECTORY)
file(MAKE_DIRECTORY "${binary_dir}" "${symbols_dir}")
# Publish final outputs only after every operation succeeds. A failed packaging
# step must remain dirty to Ninja on the next invocation.
set(binary_tmp "${BINARY}.tmp")
get_filename_component(symbols_name "${SYMBOLS}" NAME)
set(symbols_tmp_dir "${SYMBOLS}.tmpdir")
file(MAKE_DIRECTORY "${symbols_tmp_dir}")
set(symbols_tmp "${symbols_tmp_dir}/${symbols_name}")
execute_process(COMMAND "${OBJCOPY}" --only-keep-debug "${INPUT}" "${symbols_tmp}"
  COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy "${INPUT}" "${binary_tmp}"
  COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${STRIP}" --strip-unneeded "${binary_tmp}"
  COMMAND_ERROR_IS_FATAL ANY)
# The temporary directory preserves the final debug filename in the link.
execute_process(COMMAND "${OBJCOPY}" "--add-gnu-debuglink=${symbols_tmp}" "${binary_tmp}"
  COMMAND_ERROR_IS_FATAL ANY)
file(RENAME "${symbols_tmp}" "${SYMBOLS}")
file(RENAME "${binary_tmp}" "${BINARY}")
file(REMOVE_RECURSE "${symbols_tmp_dir}")
