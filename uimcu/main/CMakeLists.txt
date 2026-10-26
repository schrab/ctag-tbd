# Panel firmware main component. PRIV_REQUIRES stays empty on purpose
# (main auto-depends on every component in the build; see the A1S
# main/CMakeLists.txt lesson — non-empty PRIV_REQUIRES breaks includes).
set(UI "${CMAKE_CURRENT_LIST_DIR}/../ui")

idf_component_register(
        SRCS
            "app_main.cpp"
            "DebugLink.cpp"
            "input/UserInput.cpp"
            "${UI}/DspProxy.cpp"
            "${UI}/Display.cpp"
            "${UI}/UIMenu.cpp"
            "${UI}/menupages/UIMenuPageHome.cpp"
            "${UI}/menupages/UIMenuPageMix.cpp"
            "${UI}/menupages/UIMenuPageMod.cpp"
            "${UI}/menupages/UIMenuPageParams.cpp"
            "${UI}/menupages/UIMenuPageSystem.cpp"
            "${UI}/menupages/UIMenuPageTape.cpp"
        INCLUDE_DIRS "." "${UI}")
