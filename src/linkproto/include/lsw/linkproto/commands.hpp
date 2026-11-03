#pragma once

namespace lsw::linkproto::cmd {
inline constexpr const char* kSystemInfo = "system_info";
inline constexpr const char* kGpuStatus = "gpu_status";
inline constexpr const char* kListSessions = "list_sessions";

inline constexpr const char* kListApps = "list_apps";
inline constexpr const char* kLaunch   = "launch";
inline constexpr const char* kOpenPackage = "open_package";
inline constexpr const char* kGetIcon  = "get_icon";
inline constexpr const char* kListStoreApps  = "list_store_apps";
inline constexpr const char* kLaunchStoreApp = "launch_store_app";
inline constexpr const char* kFileHandler    = "file_handler";

inline constexpr const char* kSetDisplay      = "set_display";
inline constexpr const char* kFileHashes      = "file_hashes";
inline constexpr const char* kSetClock        = "set_clock";
inline constexpr const char* kSetLogLevel     = "set_log_level";
inline constexpr const char* kReadFile        = "read_file";
inline constexpr const char* kPackLogs        = "pack_logs";
inline constexpr const char* kClearLogs       = "clear_logs";
inline constexpr const char* kListDir         = "list_dir";
inline constexpr const char* kResolveShortcut = "resolve_shortcut";
inline constexpr const char* kUploadFile      = "upload_file";

inline constexpr const char* kStartWindowMirror = "start_window_mirror";

inline constexpr const char* kListWindows  = "list_windows";
inline constexpr const char* kCloseWindow  = "close_window";
inline constexpr const char* kWindowCommand = "window_command";

inline constexpr const char* kDownloadInstall = "download_install";
inline constexpr const char* kDownloadExtract = "download_extract";
inline constexpr const char* kRunScript       = "run_script";

inline constexpr const char* kConsoleOpen  = "console_open";
inline constexpr const char* kConsoleInput = "console_input";
inline constexpr const char* kConsoleClose = "console_close";

inline constexpr const char* kClipboardGet = "clipboard_get";
inline constexpr const char* kClipboardSet = "clipboard_set";

inline constexpr const char* kUiaState                 = "uia_state";
inline constexpr const char* kUiaInvoke                = "uia_invoke";
inline constexpr const char* kUiaSetValue              = "uia_set_value";
inline constexpr const char* kUiaGetText               = "uia_get_text";
inline constexpr const char* kUiaTreeDump              = "uia_tree_dump";
inline constexpr const char* kUiaSubscribeLifecycle    = "uia_subscribe_lifecycle";
inline constexpr const char* kUiaSubscribeFocus        = "uia_subscribe_focus";
inline constexpr const char* kUiaSubscribeProperties   = "uia_subscribe_properties";
inline constexpr const char* kUiaUnsubscribe           = "uia_unsubscribe";

}

namespace lsw::linkproto::topic {
inline constexpr const char* kLog           = "log";
inline constexpr const char* kSetupProgress = "setup_progress";
inline constexpr const char* kSetupDone     = "setup_done";
inline constexpr const char* kConsoleOutput = "console_output";
inline constexpr const char* kConsoleClosed = "console_closed";
inline constexpr const char* kAppsChanged   = "apps_changed";
inline constexpr const char* kAppError      = "app_error";
inline constexpr const char* kLaunchEnded   = "launch_ended";

inline constexpr const char* kClipboardChanged = "clipboard_changed";

inline constexpr const char* kUiaWindowOpened  = "uia_window_opened";
inline constexpr const char* kUiaWindowClosed  = "uia_window_closed";
inline constexpr const char* kUiaFocusChanged  = "uia_focus_changed";
inline constexpr const char* kUiaStateChanged  = "uia_state_changed";
inline constexpr const char* kUiaTitleChanged  = "uia_title_changed";
inline constexpr const char* kUiaModalChanged  = "uia_modal_changed";

}
