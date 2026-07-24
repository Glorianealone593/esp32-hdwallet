# console — local serial/USB REPL (LOCAL origin).
idf_component_register(
    SRCS "console_ui.c"
    INCLUDE_DIRS "include"
    REQUIRES common secure_core chains connectivity
    PRIV_REQUIRES console
)
