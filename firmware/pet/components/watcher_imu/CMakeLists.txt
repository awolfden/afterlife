include(${CMAKE_CURRENT_LIST_DIR}/pins.cmake)
idf_component_register(SRCS "upstream/bmi2.c" "upstream/bmi270.c" INCLUDE_DIRS "upstream")
