# The elevated virtual-COM helper starts outside Qt Creator's run environment.
# Keep each application's local Qt runtime complete even when no relink is needed.
win32:!contains(TEMPLATE, subdirs) {
    !debug_and_release|build_pass {
        translation_deploy_tool = $$[QT_HOST_BINS]/windeployqt.exe
        !exists($$translation_deploy_tool): error("The selected Qt kit does not contain windeployqt.exe; install its Windows deployment tools.")

        CONFIG(debug, debug|release): translation_deploy_mode = --debug
        else: translation_deploy_mode = --release

        translation_deploy_runtime.target = deploy-runtime
        # qmake's native target variable already contains the correctly quoted,
        # relative Windows path. Avoid re-encoding an absolute workspace path.
        translation_deploy_runtime.depends = $(DESTDIR_TARGET)
        # Qt 5 windeployqt queries qmake through PATH even when started by its
        # absolute filename. Scope the active kit's bin to this recipe so an
        # unrelated kit in Qt Creator's environment cannot supply its Qt paths.
        translation_deploy_runtime.commands = set \"PATH=$$shell_path($$[QT_HOST_BINS]);%PATH%\" && $$shell_quote($$shell_path($$translation_deploy_tool)) $$translation_deploy_mode --no-translations --no-compiler-runtime $(DESTDIR_TARGET)

        translation_deploy_all.target = all
        translation_deploy_all.depends = deploy-runtime
        QMAKE_EXTRA_TARGETS += translation_deploy_runtime translation_deploy_all
    }
}
