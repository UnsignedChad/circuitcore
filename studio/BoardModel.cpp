#include "BoardModel.h"

#include <utility>
#include <memory>

#include "circuitcore/formats/kicad/PcbParser.h"

namespace circuitcore::studio {

BoardModel::BoardModel(QObject* parent) : QObject(parent) {}

bool BoardModel::loadKicadPcb(const QString& path) {
    auto result = formats::kicad::PcbParser::parse_file(path.toStdString());
    if (!result) {
        emit parseFailed(QString::fromStdString(result.error().format()));
        return false;
    }
    board_ = std::make_unique<board::Board>(std::move(*result));

    // SI stackup is opportunistic -- a missing one isn't a hard error,
    // just means SI-tab analyses that need per-layer eps_r will fall
    // back to defaults.
    auto sis = sikit::si::load_si_stackup(path.toStdString());
    si_stackup_ = sis ? std::move(*sis) : sikit::si::SiStackup{};

    current_path_  = path;
    active_net_id_ = -1;
    active_layer_  = -1;
    emit boardLoaded();
    return true;
}

void BoardModel::setActiveNetId(int id) {
    if (id == active_net_id_) return;
    active_net_id_ = id;
    emit activeNetChanged(id);
}

void BoardModel::setActiveLayerOrdinal(int ord) {
    if (ord == active_layer_) return;
    active_layer_ = ord;
    emit activeLayerChanged(ord);
}

void BoardModel::setHover(double x_m, double y_m) {
    emit hover(x_m, y_m);
}

}  // namespace circuitcore::studio
