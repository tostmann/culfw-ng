set(include_dirs "include")

file(GLOB_RECURSE app_sources *.c *.cpp)

idf_component_register(SRCS ${app_sources}
                       INCLUDE_DIRS ${include_dirs}
                       PRIV_INCLUDE_DIRS ".")
