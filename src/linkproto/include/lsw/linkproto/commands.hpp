#pragma once

// The agent RPC vocabulary, once.
//
// The command set used to exist as bare string literals in three places:
// the agent's ``LSW_REGISTER_RPC`` calls, the manager's request builders,
// and the MCP tool wrappers. Nothing checked the three agreed, so a
// renamed command surfaced as a runtime "unknown command" from whichever
// copy was missed.
//
// Both sides now name these constants, so a rename is a compile error.
// Event topics live here too: they are the other half of the same
// vocabulary (the agent posts them, the manager subscribes by name).

namespace lsw::linkproto::cmd {

// -- system ---------------------------------------------------------------
inline constexpr const char* kSystemInfo = "system_info";

// -- installed apps -------------------------------------------------------
inline constexpr const char* kListApps = "list_apps";
inline constexpr const char* kLaunch   = "launch";
inline constexpr const char* kGetIcon  = "get_icon";

// -- windows --------------------------------------------------------------
inline constexpr const char* kListWindows   = "list_windows";
inline constexpr const char* kCloseWindow   = "close_window";
inline constexpr const char* kFocusWindow   = "focus_window";
inline constexpr const char* kUnfocusWindow = "unfocus_window";
inline constexpr const char* kResizeWindow  = "resize_window";
inline constexpr const char* kWindowProps   = "window_props";
inline constexpr const char* kNcHitTest     = "nchittest";
inline constexpr const char* kGetWindowIcon = "get_window_icon";

// -- input ----------------------------------------------------------------
inline constexpr const char* kInjectInput = "inject_input";

// -- capture --------------------------------------------------------------
inline constexpr const char* kSubscribeWindow   = "subscribe_window";
inline constexpr const char* kUnsubscribeWindow = "unsubscribe_window";
inline constexpr const char* kCaptureDebug      = "capture_debug";
inline constexpr const char* kSetCaptureMethod  = "set_capture_method";

// -- install / provisioning ----------------------------------------------
inline constexpr const char* kDownloadInstall = "download_install";
inline constexpr const char* kDownloadExtract = "download_extract";
inline constexpr const char* kRunScript       = "run_script";

// -- console --------------------------------------------------------------
inline constexpr const char* kConsoleOpen  = "console_open";
inline constexpr const char* kConsoleInput = "console_input";
inline constexpr const char* kConsoleClose = "console_close";

// -- UI Automation --------------------------------------------------------
inline constexpr const char* kUiaState                 = "uia_state";
inline constexpr const char* kUiaInvoke                = "uia_invoke";
inline constexpr const char* kUiaSetValue              = "uia_set_value";
inline constexpr const char* kUiaGetText               = "uia_get_text";
inline constexpr const char* kUiaTreeDump              = "uia_tree_dump";
inline constexpr const char* kUiaSubscribeLifecycle    = "uia_subscribe_lifecycle";
inline constexpr const char* kUiaUnsubscribeLifecycle  = "uia_unsubscribe_lifecycle";
inline constexpr const char* kUiaSubscribeFocus        = "uia_subscribe_focus";
inline constexpr const char* kUiaSubscribeProperties   = "uia_subscribe_properties";
inline constexpr const char* kUiaUnsubscribe           = "uia_unsubscribe";

}  // namespace lsw::linkproto::cmd

namespace lsw::linkproto::topic {

// Agent-pushed event topics. Carried in the ``event`` field of a control
// envelope; the rest of the object is the per-topic payload.
inline constexpr const char* kLog           = "log";
inline constexpr const char* kSetupProgress = "setup_progress";
inline constexpr const char* kSetupDone     = "setup_done";
inline constexpr const char* kConsoleOutput = "console_output";
inline constexpr const char* kConsoleClosed = "console_closed";

inline constexpr const char* kCaptionDragStarted = "caption_drag_started";

// Sent by the agent on the shared-memory RPC channel as soon as it maps
// the region: proof to the manager that the channel is live, and the
// ring size it may write. Absent means "socket only" — an agent too old
// for the channel, or a guest without the IVSHMEM driver.
inline constexpr const char* kChannelHello = "channel_hello";

inline constexpr const char* kUiaWindowOpened  = "uia_window_opened";
inline constexpr const char* kUiaWindowClosed  = "uia_window_closed";
inline constexpr const char* kUiaFocusChanged  = "uia_focus_changed";
inline constexpr const char* kUiaStateChanged  = "uia_state_changed";
inline constexpr const char* kUiaTitleChanged  = "uia_title_changed";
inline constexpr const char* kUiaModalChanged  = "uia_modal_changed";

}  // namespace lsw::linkproto::topic
