set(SOURCES
    # Isolated Scope #1
    core/utils/QtUtils.cpp
    core/utils/StdUtils.cpp
    core/utils/Logger.cpp
    core/HexUtils.cpp
    core/utils/BsonUtils.cpp
    core/settings/CredentialSettings.cpp
    core/settings/ConnectionSettings.cpp
    core/Event.cpp
    core/Enums.cpp
    core/EventError.cpp
    core/EventBusSubscriber.cpp
    core/EventBusDispatcher.cpp
    core/EventWrapper.cpp
    core/EventBus.cpp
    core/KeyboardManager.cpp
    core/domain/MongoNamespace.cpp
    core/domain/MongoFunction.cpp
    core/domain/MongoUtils.cpp
    core/domain/MongoCollection.cpp
    core/domain/MongoCollectionInfo.cpp
    core/domain/MongoQueryInfo.cpp
    core/domain/CursorPosition.cpp
    core/domain/ScriptInfo.cpp
    core/events/MongoEventsInfo.cpp
    core/bson/Bson.cpp
    core/mongodb/MongoConnection.cpp
    core/mongodb/JsonDocumentReader.cpp
    core/mongodb/DataTransfer.cpp
    core/mongodb/ImportBatchWriter.cpp

    # Isolated Scope #2
    core/engine/ScriptEngine.cpp
    core/events/MongoEvents.cpp
    core/domain/MongoDocument.cpp
    gui/AppStyle.cpp
    core/domain/MongoServer.cpp
    core/domain/MongoShell.cpp
    core/domain/MongoDatabase.cpp
    core/domain/App.cpp
    core/mongodb/MongoClient.cpp
    core/mongodb/MongoWorker.cpp
    core/mongodb/ReplicaSet.cpp
    core/settings/SettingsManager.cpp
    core/AppRegistry.cpp
    utils/StringOperations.cpp
    utils/common.cpp
    utils/SimpleCrypt.cpp
    utils/RoboCrypt.cpp

    # Isolated Scope #3
    gui/GuiRegistry.cpp
    gui/dialogs/AboutDialog.cpp
    gui/dialogs/EulaDialog.cpp
    gui/dialogs/ConnectionAdvancedTab.cpp
    gui/dialogs/ConnectionAuthTab.cpp
    gui/dialogs/ConnectionBasicTab.cpp
    gui/dialogs/ConnectionDiagnosticDialog.cpp
    gui/dialogs/ConnectionDialog.cpp
    gui/dialogs/CopyCollectionDialog.cpp
    gui/widgets/workarea/IndicatorLabel.cpp
    gui/dialogs/CreateCollectionDialog.cpp
    gui/dialogs/CreateDatabaseDialog.cpp
    gui/dialogs/CreateUserDialog.cpp
    gui/utils/ComboBoxUtils.cpp
    gui/utils/DialogUtils.cpp

    # Isolated scope #4
    gui/dialogs/PreferencesDialog.cpp
    gui/dialogs/ConnectionsDialog.cpp
    gui/dialogs/DataTransferDialog.cpp
    gui/dialogs/ChangeShellTimeoutDialog.cpp

    # Isolated scope #5
    gui/editors/PlainJavaScriptEditor.cpp
    gui/editors/JSLexer.cpp
    gui/editors/FindFrame.cpp
    gui/widgets/explorer/AddEditIndexDialog.cpp
    gui/widgets/workarea/ScriptWidget.cpp

    # Isolated scope #6
    gui/widgets/explorer/ExplorerCollectionTreeItem.cpp
    gui/widgets/explorer/ExplorerCollectionIndexesDir.cpp
    gui/widgets/explorer/ExplorerCollectionIndexItem.cpp
    gui/widgets/explorer/ExplorerTreeItem.cpp
    gui/widgets/explorer/ExplorerReplicaSetFolderItem.cpp
    gui/widgets/explorer/ExplorerReplicaSetTreeItem.cpp
    gui/widgets/explorer/ExplorerDatabaseTreeItem.cpp
    gui/widgets/explorer/ExplorerDatabaseCategoryTreeItem.cpp
    gui/widgets/explorer/ExplorerUserTreeItem.cpp
    gui/widgets/explorer/ExplorerFunctionTreeItem.cpp
    gui/dialogs/DocumentTextEditor.cpp
    gui/dialogs/FieldValueEditor.cpp
    gui/dialogs/FunctionTextEditor.cpp

    # Isolated scope #7
    gui/widgets/explorer/ExplorerServerTreeItem.cpp
    gui/widgets/explorer/ExplorerTreeWidget.cpp
    gui/widgets/explorer/ExplorerWidget.cpp
    gui/widgets/workarea/BsonTableModel.cpp
    gui/widgets/workarea/BsonTableView.cpp
    gui/widgets/workarea/BsonTreeItem.cpp
    gui/widgets/workarea/BsonTreeModel.cpp
    gui/widgets/workarea/BsonTreeView.cpp
    core/domain/Notifier.cpp

    # Isolated scope #8
    gui/widgets/workarea/CollectionStatsTreeItem.cpp
    gui/widgets/workarea/CollectionStatsTreeWidget.cpp
    gui/widgets/workarea/JsonPrepareThread.cpp
    gui/widgets/workarea/OutputItemContentWidget.cpp
    gui/widgets/workarea/OutputItemHeaderWidget.cpp
    gui/widgets/workarea/OutputWidget.cpp
    gui/widgets/workarea/PagingWidget.cpp
    gui/widgets/workarea/ProgressBarPopup.cpp
    gui/widgets/workarea/QueryWidget.cpp
    gui/widgets/workarea/WorkAreaTabBar.cpp
    gui/widgets/workarea/WorkAreaTabWidget.cpp
    gui/widgets/workarea/WelcomeTab.cpp

    # Final scope
    gui/widgets/LogWidget.cpp
    gui/MainWindow.cpp

    gui/dialogs/SSHTunnelTab.cpp
    gui/dialogs/SSLTab.cpp
    core/settings/SshSettings.cpp
    core/settings/SslSettings.cpp
    core/settings/ReplicaSetSettings.cpp
    core/mongodb/SshTunnelWorker.cpp

    resources/robo.qrc
    gui/resources/gui.qrc
)

# Object library shares exactly the production implementation with tests.
add_library(robo_core OBJECT ${SOURCES})
target_link_libraries(robo_core PUBLIC robo_dependencies ssh)
add_executable(robomongo MACOSX_BUNDLE WIN32 app/main.cpp)
target_link_libraries(robomongo PRIVATE robo_core)
if(APPLE)
    set_target_properties(robomongo PROPERTIES
        OUTPUT_NAME "Robo 3T"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/install/macosx/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "Robo 3T"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.3tsoftwarelabs.robo3t"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_LONG_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "robomongo"
        INSTALL_RPATH "@executable_path/../Frameworks")
    install(TARGETS robomongo BUNDLE DESTINATION .)
    set(ROBO_RESOURCES "Robo 3T.app/Contents/Resources")
    install(FILES "${CMAKE_SOURCE_DIR}/install/macosx/robomongo.icns" DESTINATION "${ROBO_RESOURCES}")
else()
    set_target_properties(robomongo PROPERTIES OUTPUT_NAME robo3t)
    install(TARGETS robomongo RUNTIME DESTINATION bin)
    set(ROBO_RESOURCES share/robo3t)
endif()
qt_generate_deploy_app_script(TARGET robomongo OUTPUT_SCRIPT deploy_script NO_UNSUPPORTED_PLATFORM_ERROR)
install(SCRIPT ${deploy_script})
install(FILES "${CMAKE_SOURCE_DIR}/LICENSE" "${CMAKE_SOURCE_DIR}/COPYRIGHT" DESTINATION "${ROBO_RESOURCES}")
install(PROGRAMS "${ROBOMONGO_NODE_EXECUTABLE}" DESTINATION "${ROBO_RESOURCES}/runtime")
install(FILES "${ROBOMONGO_RUNTIME_DIR}/mongosh-bridge.js" "${ROBOMONGO_RUNTIME_DIR}/mongosh-autocomplete.js" "${ROBOMONGO_RUNTIME_DIR}/package.json" "${ROBOMONGO_RUNTIME_DIR}/package-lock.json" DESTINATION "${ROBO_RESOURCES}/runtime")
install(DIRECTORY "${ROBOMONGO_NODE_MODULES}" DESTINATION "${ROBO_RESOURCES}/runtime")
install(FILES "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md" DESTINATION "${ROBO_RESOURCES}")
install(DIRECTORY "${CMAKE_SOURCE_DIR}/licenses" DESTINATION "${ROBO_RESOURCES}")
if(APPLE)
    configure_file("${CMAKE_SOURCE_DIR}/install/macosx/SignBundle.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/SignBundle.cmake" @ONLY)
    install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/SignBundle.cmake")
endif()
