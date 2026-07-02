include(FindGettext)
find_program(GETTEXT_XGETTEXT_EXECUTABLE xgettext)

set(DOMAIN ${FULL_PROJECT_NAME})
set(POT_FILE ${DOMAIN}.pot)
file(GLOB PO_FILES *.po)

if(NOT GETTEXT_XGETTEXT_EXECUTABLE)
    message(FATAL_ERROR "Could not find xgettext. Install gettext to update translations.")
endif()

add_custom_target(${POT_FILE} ALL
    COMMENT "Generating translation template"
    COMMAND ${GETTEXT_XGETTEXT_EXECUTABLE} -o ${CMAKE_CURRENT_SOURCE_DIR}/${POT_FILE}
        -D ${CMAKE_SOURCE_DIR}
        --from-code=UTF-8
        --c++ --qt --language=javascript
        --keyword=tr --keyword=tr:1,2 --keyword=ctr:1c,2 --keyword=dctr:2c,3
        --keyword=N_ --keyword=_ --keyword=dtr:2 --keyword=dtr:2,3
        --package-name='${DOMAIN}'
        --sort-by-file
        ${I18N_SRC_FILES}
)

foreach(PO_FILE ${PO_FILES})
    get_filename_component(LANG ${PO_FILE} NAME_WE)
    gettext_process_po_files(${LANG} ALL PO_FILES ${PO_FILE})
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${LANG}.gmo
        DESTINATION share/locale/${LANG}/LC_MESSAGES
        RENAME ${DOMAIN}.mo)
endforeach()
