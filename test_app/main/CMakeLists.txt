# Empty component that links the idf_test runner (provides app_main)
idf_component_register(SRCS "app_main.c" REQUIRES unity)
