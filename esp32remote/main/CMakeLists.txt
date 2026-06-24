idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS ".")

# Inject the firmware version (set in the top-level CMakeLists.txt) as FW_VERSION.
target_compile_definitions(${COMPONENT_LIB} PRIVATE FW_VERSION="${PROJECT_VER}")
