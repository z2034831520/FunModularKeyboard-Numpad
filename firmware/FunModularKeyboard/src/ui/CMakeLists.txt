SET(SOURCES ui.c
    ui_MainScreen.c
    ui_StatusBar.c
    ui_font_BebasNeueFont48.c
    ui_font_BebasNeueFont24.c
    ui_font_BebasNeueFont86.c
    ui_font_BebasNeueFont36.c)

add_library(ui ${SOURCES})
