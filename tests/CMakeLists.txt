
# 测试代码豁免 BAN 严格合规：测试允许使用裸 malloc/free/memset 等函数
add_compile_definitions(AIRY_COMPLIANCE_IMPL)
set(TEST_FILES
    test_heapstore_core
    test_heapstore_registry
    test_heapstore_trace
    test_heapstore_log
    test_heapstore_ipc
    test_heapstore_memory
    test_heapstore_migration
    test_heapstore_integration
)

foreach(test ${TEST_FILES})
    # POSIX pthread 压测（test_heapstore_trace.c include <pthread.h> 并直调
    # pthread_create/join）：MSVC 工具链无 pthread（FindThreads 在 Win32 解析
    # 为空接口，链接必报 LNK2001 pthread_create/join），范式同
    # atoms/corekern/tests：多线程覆盖由 POSIX 腿（Linux/macOS）保障，
    # MSVC 下跳过构建。
    if(MSVC AND test STREQUAL "test_heapstore_trace")
        continue()
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${test}.c")
        add_executable(${test} ${test}.c)
        target_include_directories(${test} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../..
            ${CMAKE_CURRENT_SOURCE_DIR}/../../commons/utils/memory
        )
        target_link_libraries(${test}
            PRIVATE airy_heapstore
            Threads::Threads
        )
        if(AIRY_HAS_SQLITE3)
            if(SQLite3_FOUND)
                target_link_libraries(${test} PRIVATE ${SQLite3_LIBRARIES})
            elseif(SQLITE3_FOUND)
                target_link_libraries(${test} PRIVATE ${SQLITE3_LIBRARIES})
            endif()
        endif()
        add_test(NAME ${test} COMMAND ${test})
    endif()
endforeach()

option(BUILD_BENCHMARK "Build performance benchmark" ON)

if(BUILD_BENCHMARK)
    add_executable(heapstore_benchmark benchmark_heapstore.c)
    target_include_directories(heapstore_benchmark PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
        ${CMAKE_CURRENT_SOURCE_DIR}/../../commons/utils/memory
    )
    target_link_libraries(heapstore_benchmark
        PRIVATE airy_heapstore
        Threads::Threads
    )
    if(AIRY_HAS_SQLITE3)
        if(SQLite3_FOUND)
            target_link_libraries(heapstore_benchmark PRIVATE ${SQLite3_LIBRARIES})
        elseif(SQLITE3_FOUND)
            target_link_libraries(heapstore_benchmark PRIVATE ${SQLITE3_LIBRARIES})
        endif()
    endif()
endif()

file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/test_output)
