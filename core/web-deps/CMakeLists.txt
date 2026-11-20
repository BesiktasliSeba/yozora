cmake_minimum_required(VERSION 3.20)
project(YozoraSQLite C)
find_package(Threads REQUIRED)
add_library(sqlite3 STATIC "${YOZORA_SQLITE_SRC}/sqlite3.c")
# browser databases never load executable SQLite extensions
target_compile_definitions(sqlite3 PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION=1)
target_link_libraries(sqlite3 PUBLIC Threads::Threads)
install(TARGETS sqlite3 ARCHIVE DESTINATION lib)
install(FILES "${YOZORA_SQLITE_SRC}/sqlite3.h" "${YOZORA_SQLITE_SRC}/sqlite3ext.h" DESTINATION include)
