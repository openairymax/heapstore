airy_print_info("[HEAPSTORE] Configuring heapstore module (SQLite3: ${AIRY_HAS_SQLITE3})")

set(heapstore_SOURCES
    src/heapstore_core.c
    src/heapstore_core_batch.c
    src/heapstore_registry.c
    src/heapstore_registry_sqlite.c
    src/heapstore_registry_sqlite_agent.c
    src/heapstore_registry_sqlite_skill.c
    src/heapstore_registry_sqlite_session.c
    src/heapstore_registry_sqlite_iter.c
    src/heapstore_trace.c
    src/heapstore_log.c
    src/heapstore_ipc.c
    src/heapstore_memory.c
    src/heapstore_token.c
    src/heapstore_integration.c
    src/heapstore_migration.c
    src/utils.c
    kernel/services/trace_store_service.c
    kernel/services/log_store_service.c
)

set(heapstore_HEADERS
    include/heapstore.h
    include/heapstore_registry.h
    include/heapstore_trace.h
    include/heapstore_log.h
    include/heapstore_ipc.h
    include/heapstore_memory.h
    include/heapstore_batch.h
    include/heapstore_migration.h
    include/heapstore_token.h
    include/heapstore_integration.h
    include/utils.h
    src/private.h
    src/heapstore_registry_internal.h
)

add_library(airy_heapstore STATIC ${heapstore_SOURCES} ${heapstore_HEADERS})

target_include_directories(airy_heapstore PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../commons/platform/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../commons/utils/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../commons/utils/sync/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../commons/utils/compat/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_definitions(airy_heapstore PRIVATE
    airy_heapstore_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    airy_heapstore_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    airy_heapstore_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

target_link_libraries(airy_heapstore
    PRIVATE Threads::Threads
    PUBLIC airy_compile_defs
)

if(TARGET airy_common)
    target_link_libraries(airy_heapstore PUBLIC airy_common)
endif()

if(AIRY_HAS_SQLITE3)
    if(SQLite3_FOUND)
        target_include_directories(airy_heapstore PRIVATE ${SQLite3_INCLUDE_DIRS})
        target_link_libraries(airy_heapstore PRIVATE ${SQLite3_LIBRARIES})
    elseif(SQLITE3_FOUND)
        target_include_directories(airy_heapstore PRIVATE ${SQLITE3_INCLUDE_DIRS})
        target_link_libraries(airy_heapstore PRIVATE ${SQLITE3_LIBRARIES})
    endif()
    airy_print_ok("heapstore: SQLite3 registry backend enabled")
else()
    airy_print_info("heapstore: using in-memory registry backend (no SQLite3)")
endif()

install(TARGETS airy_heapstore
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES ${heapstore_HEADERS}
    DESTINATION include/agentrt/heapstore
)

if(BUILD_TESTS)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests")
        add_subdirectory(tests)
    endif()
endif()

airy_print_info("AgentRT heapstore v${PROJECT_VERSION}")
