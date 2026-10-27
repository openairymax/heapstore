airy_print_info("[HEAPSTORE] Configuring heapstore module (SQLite3: ${AIRY_HAS_SQLITE3})")

set(heapstore_SOURCES
    src/heapstore_core.c
    src/heapstore_core_batch.c
    # 域拆分（2026-08-27）：core 按职责切出熔断/错误/指标/写入四域
    src/heapstore_core_circuit.c
    src/heapstore_core_error.c
    src/heapstore_core_metrics.c
    src/heapstore_core_write.c
    src/heapstore_registry.c
    src/heapstore_registry_sqlite.c
    src/heapstore_registry_sqlite_agent.c
    src/heapstore_registry_sqlite_skill.c
    src/heapstore_registry_sqlite_session.c
    src/heapstore_registry_sqlite_iter.c
    # journal 六域（2026-10-27）：turns/checkpoints/llm_calls/tool_calls/
    # events/cost_tracking，双后端各一份实现
    src/heapstore_journal_sqlite.c
    src/heapstore_journal_mem.c
    src/heapstore_trace.c
    src/heapstore_log.c
    src/heapstore_memory.c
    src/heapstore_integration.c
    src/heapstore_migration.c
    # 域拆分（2026-08-27）：migration 切出前向域
    src/heapstore_migration_forward.c
    src/utils.c
)

set(heapstore_HEADERS
    include/heapstore.h
    include/heapstore_registry.h
    include/heapstore_journal.h
    include/heapstore_trace.h
    include/heapstore_log.h
    include/heapstore_memory.h
    include/heapstore_batch.h
    include/heapstore_migration.h
    include/heapstore_integration.h
    include/utils.h
    src/private.h
    src/heapstore_core_internal.h
    src/heapstore_registry_internal.h
    src/heapstore_migration_internal.h
)

# schema 单一真相源（2026-10-27）：migrations/V001_initial_schema.sql 是
# 唯一 DDL 文本，构建期转义为 C 字符串注入 heapstore_schema.h，运行时
# registry 初始化直接执行，避免 C 内嵌副本与迁移文件长期漂移。
set(heapstore_SCHEMA_SQL_FILE ${CMAKE_CURRENT_SOURCE_DIR}/migrations/V001_initial_schema.sql)
file(READ ${heapstore_SCHEMA_SQL_FILE} heapstore_SCHEMA_SQL)
string(REPLACE "\\" "\\\\" heapstore_SCHEMA_SQL "${heapstore_SCHEMA_SQL}")
string(REPLACE "\"" "\\\"" heapstore_SCHEMA_SQL "${heapstore_SCHEMA_SQL}")
string(REPLACE "\n" "\\n\"\n    \"" heapstore_SCHEMA_SQL "${heapstore_SCHEMA_SQL}")
# 语句级切分：`;` 是 schema 内唯一语句终止符，逐条生成数组元素，
# 避免整段 DDL 拼成单个超长字面量（C99 仅保证 4095 字节）。
string(REPLACE ";" ";@HS_SPLIT@" heapstore_SCHEMA_SQL "${heapstore_SCHEMA_SQL}")
string(REPLACE "@HS_SPLIT@" "\",\n    \"" heapstore_SCHEMA_SQL "${heapstore_SCHEMA_SQL}")
configure_file(src/heapstore_schema.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/heapstore_schema.h @ONLY)

add_library(airy_heapstore STATIC ${heapstore_SOURCES} ${heapstore_HEADERS})

target_include_directories(airy_heapstore PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../commons/platform/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../commons/utils/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../commons/utils/sync>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../commons/utils/compat>
    $<INSTALL_INTERFACE:include>
)

# 私有生成头（heapstore_schema.h）所在目录，仅本目标可见。
target_include_directories(airy_heapstore PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

# 0.1.16 B4（include 面收口，2026-09-14 裁决③）：heapstore 为 daemon 侧
# 存储基建（svc_common bootstrap 持有注入），与 daemons 服务面同权消费
# corekern 头（AIRY_API/airy_err_t，export.h 经 airy_rt.h）——PRIVATE 自取，
# 不向链接方传递内核头可见性。
if(AIRY_COREKERN_INCLUDE_DIR)
    target_include_directories(airy_heapstore PRIVATE ${AIRY_COREKERN_INCLUDE_DIR})
endif()

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
