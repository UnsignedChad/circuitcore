// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "CalculatorsDialog.h"

#include <cmath>
#include <numbers>

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

#include "emi/CableCommonMode.h"
#include "pi/Dielectric.h"
#include "pi/Roughness.h"
#include "pi/TargetZ.h"
#include "pi/ViaInductance.h"
#include "pi/Vrm.h"

namespace circuitcore::studio {

namespace {

// Small helper: build a tab body laid out as (form rows on top,
// Compute button, result label). Caller wires the button click.
struct CalcTab {
    QWidget*     page;
    QFormLayout* form;
    QPushButton* btn;
    QLabel*      result;
};

CalcTab make_tab(const QString& button_text) {
    CalcTab t;
    t.page = new QWidget();
    auto* outer = new QVBoxLayout(t.page);
    t.form = new QFormLayout();
    outer->addLayout(t.form);
    t.btn = new QPushButton(button_text, t.page);
    outer->addWidget(t.btn);
    t.result = new QLabel(t.page);
    t.result->setTextInteractionFlags(Qt::TextSelectableByMouse);
    t.result->setWordWrap(true);
    t.result->setMinimumHeight(80);
    outer->addWidget(t.result, 1);
    return t;
}

QDoubleSpinBox* make_spin(double lo, double hi, double init, int decimals,
                           const QString& suffix = {}) {
    auto* s = new QDoubleSpinBox();
    s->setRange(lo, hi);
    s->setDecimals(decimals);
    s->setValue(init);
    if (!suffix.isEmpty()) s->setSuffix(suffix);
    return s;
}

}  // namespace

CalculatorsDialog::CalculatorsDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Calculators"));
    setWindowFlag(Qt::WindowMinMaxButtonsHint);
    setModal(false);
    resize(520, 360);

    tabs_ = new QTabWidget(this);
    auto* outer = new QVBoxLayout(this);
    outer->addWidget(tabs_);

    // ---- Target impedance (Larry Smith flat-target form) ----
    {
        auto t = make_tab(tr("Compute"));
        auto* v   = make_spin(0.1, 100.0, 3.3,   3, " V");
        auto* tol = make_spin(0.001, 100.0, 5.0,  3, " %");
        auto* i   = make_spin(0.001, 1000.0, 1.0, 3, " A");
        t.form->addRow(tr("V_nom"), v);
        t.form->addRow(tr("V_tolerance"), tol);
        t.form->addRow(tr("I_step"), i);
        QObject::connect(t.btn, &QPushButton::clicked, this,
            [t, v, tol, i]() {
                pdnkit::pi::TargetZSpec spec{
                    v->value(), tol->value() / 100.0, i->value()};
                const double z = pdnkit::pi::target_impedance_flat(spec);
                t.result->setText(tr("<b>Z_target = %1 mOhm</b><br>"
                                     "Hold this from DC up to the load's "
                                     "self-bypass corner (typ. 10-100 MHz).")
                                      .arg(z * 1000.0, 0, 'f', 3));
            });
        tabs_->addTab(t.page, tr("Target Z"));
    }

    // ---- Via barrel inductance (Goldfarb formula) ----
    {
        auto t = make_tab(tr("Compute"));
        auto* d = make_spin(0.05,  5.0, 0.3, 3, " mm");
        auto* l = make_spin(0.05, 10.0, 1.6, 3, " mm");
        auto* s = make_spin(0.0,  20.0, 0.0, 3, " mm");
        t.form->addRow(tr("Barrel diameter"), d);
        t.form->addRow(tr("Barrel length"),   l);
        t.form->addRow(tr("Return spacing (0 = self only)"), s);
        QObject::connect(t.btn, &QPushButton::clicked, this,
            [t, d, l, s]() {
                const double r = 0.5 * d->value() * 1.0e-3;
                const double h =       l->value() * 1.0e-3;
                const double L = pdnkit::pi::via_self_inductance(r, h);
                QString txt =
                    tr("<b>L_self = %1 pH</b>").arg(L * 1.0e12, 0, 'f', 2);
                if (s->value() > 0.0) {
                    const double M = pdnkit::pi::via_mutual_inductance(
                        r, h, s->value() * 1.0e-3);
                    txt += tr("<br>M_mutual = %1 pH<br>"
                              "<b>L_loop = %2 pH</b> (self - mutual)")
                               .arg(M * 1.0e12, 0, 'f', 2)
                               .arg((L - M) * 1.0e12, 0, 'f', 2);
                }
                t.result->setText(txt);
            });
        tabs_->addTab(t.page, tr("Via inductance"));
    }

    // ---- VRM impedance (R_droop + jwL) ----
    {
        auto t = make_tab(tr("Compute"));
        auto* r = make_spin(0.0, 1000.0, 5.0, 3, " mOhm");
        auto* L = make_spin(0.0, 1000.0, 1.0, 3, " uH");
        auto* f = make_spin(1.0, 1.0e12, 1.0e6, 1, " Hz");
        t.form->addRow(tr("R_droop"), r);
        t.form->addRow(tr("L_out"),   L);
        t.form->addRow(tr("Frequency"), f);
        QObject::connect(t.btn, &QPushButton::clicked, this,
            [t, r, L, f]() {
                pdnkit::pi::VrmModel m{r->value() * 1.0e-3,
                                        L->value() * 1.0e-6};
                const double omega = 2.0 * std::numbers::pi * f->value();
                const auto z = pdnkit::pi::vrm_impedance(m, omega);
                const double mag = std::abs(z);
                t.result->setText(tr("<b>|Z_VRM| = %1 mOhm</b><br>"
                                     "Re = %2 mOhm, Im = %3 mOhm")
                                      .arg(mag * 1000.0, 0, 'f', 3)
                                      .arg(z.real() * 1000.0, 0, 'f', 3)
                                      .arg(z.imag() * 1000.0, 0, 'f', 3));
            });
        tabs_->addTab(t.page, tr("VRM impedance"));
    }

    // ---- Djordjevic-Sarkar dielectric ----
    {
        auto t = make_tab(tr("Compute"));
        auto* eps_inf   = make_spin(1.0, 30.0, 1.0,    3);
        auto* delta_eps = make_spin(0.0, 30.0, 3.3,    3);
        auto* f1        = make_spin(1.0, 1.0e12, 1.0e3,  1, " Hz");
        auto* f2        = make_spin(1.0, 1.0e15, 1.0e12, 1, " Hz");
        auto* f         = make_spin(1.0, 1.0e12, 1.0e9,  1, " Hz");
        t.form->addRow(tr("eps_inf"),   eps_inf);
        t.form->addRow(tr("delta_eps"), delta_eps);
        t.form->addRow(tr("f1"), f1);
        t.form->addRow(tr("f2"), f2);
        t.form->addRow(tr("Evaluate at f"), f);
        QObject::connect(t.btn, &QPushButton::clicked, this,
            [t, eps_inf, delta_eps, f1, f2, f]() {
                pdnkit::pi::DjordjevicSarkar m{
                    eps_inf->value(), delta_eps->value(),
                    f1->value(), f2->value()};
                auto s = pdnkit::pi::dj_sarkar_at(m, f->value());
                t.result->setText(tr("<b>eps_r' = %1</b><br>"
                                     "eps_r\" = %2<br>"
                                     "tan(d) = %3")
                                      .arg(s.eps_r_real, 0, 'f', 4)
                                      .arg(s.eps_r_imag, 0, 'e', 3)
                                      .arg(s.tan_delta, 0, 'f', 5));
            });
        tabs_->addTab(t.page, tr("eps(f)"));
    }

    // ---- Hammerstad-Jensen surface roughness multiplier ----
    {
        auto t = make_tab(tr("Compute"));
        auto* rq = make_spin(0.01, 50.0, 1.0,   3, " um RMS");
        auto* f  = make_spin(1.0,  1.0e12, 1.0e9, 1, " Hz");
        t.form->addRow(tr("R_q (foil RMS)"), rq);
        t.form->addRow(tr("Frequency"),       f);
        QObject::connect(t.btn, &QPushButton::clicked, this,
            [t, rq, f]() {
                const double r_q_m = rq->value() * 1.0e-6;
                const double k = pdnkit::pi::hj_roughness_multiplier(
                    r_q_m, f->value());
                const double omega = 2.0 * std::numbers::pi * f->value();
                const double delta = pdnkit::pi::skin_depth_copper(omega);
                t.result->setText(tr("<b>K_HJ = %1</b> (Rs_rough = K * Rs_smooth)<br>"
                                     "skin depth = %2 um<br>"
                                     "R_q / delta = %3")
                                      .arg(k, 0, 'f', 4)
                                      .arg(delta * 1.0e6, 0, 'f', 3)
                                      .arg(r_q_m / delta, 0, 'f', 3));
            });
        tabs_->addTab(t.page, tr("Roughness K"));
    }

    // ---- Cable common-mode E-field ----
    {
        auto t = make_tab(tr("Compute"));
        auto* L  = make_spin(0.01, 10.0, 1.0,    3, " m");
        auto* i  = make_spin(1.0e-6, 1.0, 1.0e-3, 6, " A");
        auto* f  = make_spin(1.0e3, 1.0e12, 1.0e8, 1, " Hz");
        auto* d  = make_spin(0.1,   100.0,  3.0,   2, " m");
        t.form->addRow(tr("Cable length"),     L);
        t.form->addRow(tr("CM current I_cm"),  i);
        t.form->addRow(tr("Frequency"),        f);
        t.form->addRow(tr("Measurement distance"), d);
        QObject::connect(t.btn, &QPushButton::clicked, this,
            [t, L, i, f, d]() {
                emikit::emi::CableSpec c{L->value(), i->value()};
                const double e = emikit::emi::cable_cm_e_field(
                    c, f->value(), d->value());
                const double edb = emikit::emi::cable_cm_e_field_dbuv(
                    c, f->value(), d->value());
                t.result->setText(tr("<b>|E| = %1 uV/m</b><br>"
                                     "= %2 dBuV/m<br>"
                                     "Compare with CISPR 22/32 class B limits "
                                     "(40 dBuV/m at 30-230 MHz, 47 above).")
                                      .arg(e * 1.0e6, 0, 'f', 2)
                                      .arg(edb, 0, 'f', 2));
            });
        tabs_->addTab(t.page, tr("Cable CM"));
    }
}

}  // namespace circuitcore::studio
