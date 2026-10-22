# Records what a prefix was built from, so a restored cache is used only when it matches
# (src/ifc/IfcOpenShellPrefix.cmake). Run with -P after the prefix is installed.
file(WRITE "${STAMP_FILE}" "ifcopenshell ${PIN}\nprefix-format ${FORMAT}\n")
