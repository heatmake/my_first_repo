file(GLOB CORE_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/activition_phase/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/flash_phase/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/prefabrication_phase/*.cpp
)
set(CORE_SRC ${CORE_SRC} PARENT_SCOPE)
