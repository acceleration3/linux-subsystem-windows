#include "lsw/host/manager/non_client_hit.hpp"

namespace lsw::host::manager {

const char* NonClientHit::name(int code) {
    switch (code) {
    case -2:            return "TRANSPARENT";  // HTTRANSPARENT
    case -1:            return "ERROR";        // HTERROR
    case 0:             return "NOWHERE";      // HTNOWHERE
    case kClient:       return "CLIENT";
    case kCaption:      return "CAPTION";
    case kSysMenu:      return "SYSMENU";
    case 4:             return "GROWBOX";
    case 5:             return "MENU";
    case 6:             return "HSCROLL";
    case 7:             return "VSCROLL";
    case kMinButton:    return "MINBUTTON";
    case kMaxButton:    return "MAXBUTTON";
    case kLeft:         return "LEFT";
    case kRight:        return "RIGHT";
    case kTop:          return "TOP";
    case kTopLeft:      return "TOPLEFT";
    case kTopRight:     return "TOPRIGHT";
    case kBottom:       return "BOTTOM";
    case kBottomLeft:   return "BOTTOMLEFT";
    case kBottomRight:  return "BOTTOMRIGHT";
    case 18:            return "BORDER";
    case kClose:        return "CLOSE";
    case 21:            return "HELP";
    default:            return "?";
    }
}

}  // namespace lsw::host::manager
