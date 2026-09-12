include("${PROJECT_SOURCE_DIR}/cmake/GoogleBenchmark.cmake")
add_executable(ikea_tuplepack_bench main.cpp composition.cpp packets.cpp
  packets_wide.cpp packets_medium.cpp packets_narrow.cpp)
sixdb_target(ikea_tuplepack_bench)
target_link_libraries(ikea_tuplepack_bench PRIVATE ikea::tuplepack benchmark::benchmark)

# Shape shards bound compiler memory; the small ordinary suite need not compile them.
add_executable(ikea_tuplepack_word_bench main_words.cpp words_1.cpp words_2.cpp words_4.cpp words_8.cpp)
sixdb_target(ikea_tuplepack_word_bench)
target_link_libraries(ikea_tuplepack_word_bench PRIVATE ikea::tuplepack benchmark::benchmark)
