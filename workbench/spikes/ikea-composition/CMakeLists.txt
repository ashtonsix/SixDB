# Historical executable probes owned by the composition investigation.
# Configure together; build only the targets needed for a particular check.
if(NOT TARGET ikea_bitsets)
  add_subdirectory(probes/ikea-blocks)
endif()
if(NOT TARGET ikea_integer_kernels)
  add_subdirectory(probes/ikea-integers)
endif()
if(NOT TARGET ikea_heterogeneous)
  add_subdirectory(probes/ikea-heterogeneous)
endif()

# Measurements and verification of the production module.
add_subdirectory(validation)
