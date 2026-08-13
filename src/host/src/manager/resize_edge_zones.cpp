#include "lsw/host/manager/resize_edge_zones.hpp"

namespace lsw::host::manager {

Qt::Edges ResizeEdgeZones::at(QPointF position, int width, int height, int border) {
    if (border <= 0) {
        return {};
    }
    Qt::Edges edges = {};
    if (position.x() < border) {
        edges |= Qt::LeftEdge;
    } else if (position.x() >= width - border) {
        edges |= Qt::RightEdge;
    }
    if (position.y() < border) {
        edges |= Qt::TopEdge;
    } else if (position.y() >= height - border) {
        edges |= Qt::BottomEdge;
    }
    return edges;
}

Qt::CursorShape ResizeEdgeZones::cursor_for(Qt::Edges edges) {
    const bool left = (edges & Qt::LeftEdge) != 0;
    const bool right = (edges & Qt::RightEdge) != 0;
    const bool top = (edges & Qt::TopEdge) != 0;
    const bool bottom = (edges & Qt::BottomEdge) != 0;
    if ((top && left) || (bottom && right)) return Qt::SizeFDiagCursor;
    if ((top && right) || (bottom && left)) return Qt::SizeBDiagCursor;
    if (left || right) return Qt::SizeHorCursor;
    if (top || bottom) return Qt::SizeVerCursor;
    return Qt::ArrowCursor;
}

}  // namespace lsw::host::manager
