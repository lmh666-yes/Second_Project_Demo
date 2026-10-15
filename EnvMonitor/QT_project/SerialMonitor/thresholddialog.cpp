#include "thresholddialog.h"
#include "physrange.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

ThresholdDialog::ThresholdDialog(AlarmManager *mgr, QWidget *parent)
    : QDialog(parent)
    , m_mgr(mgr)
{
    setWindowTitle(QStringLiteral("告警阈值设置"));
    build();
    loadFrom(*mgr);
}

bool ThresholdDialog::edit(AlarmManager *mgr, QWidget *parent)
{
    ThresholdDialog d(mgr, parent);
    return d.exec() == QDialog::Accepted;
}

void ThresholdDialog::build()
{
    const char *kColors[int(Series::Count)] = {
        "#c0392b", "#2874a6", "#1e8449", "#b9770e", "#7d3c98", "#707b7c"
    };

    QGridLayout *g = new QGridLayout();
    const QStringList head = QStringList()
            << QStringLiteral("项目") << QStringLiteral("下限") << QStringLiteral("启用")
            << QStringLiteral("上限") << QStringLiteral("启用") << QStringLiteral("回差")
            << QStringLiteral("合理范围");
    for (int c = 0; c < head.size(); ++c)
        g->addWidget(new QLabel(head.at(c)), 0, c);

    for (int i = 0; i < int(Series::Count); ++i) {
        const Series s = Series(i);
        const int dec = DataModel::decimals(s);

        auto mkSpin = [this, dec]() {
            QDoubleSpinBox *sb = new QDoubleSpinBox(this);
            sb->setDecimals(dec);
            /* 输入范围给得宽（-10000~10000）：阈值是报警线，不要求落在物理合理
             * 范围内，合理范围只在最后一列作提示。 */
            sb->setRange(-10000.0, 10000.0);
            sb->setSingleStep(dec == 0 ? 10.0 : 1.0);
            return sb;
        };

        m_low[i]  = mkSpin();
        m_high[i] = mkSpin();
        m_hyst[i] = mkSpin();
        m_lowOn[i]  = new QCheckBox(QStringLiteral("低"), this);
        m_highOn[i] = new QCheckBox(QStringLiteral("高"), this);
        m_lowOn[i]->setToolTip(QStringLiteral("勾选后低于下限即报警"));
        m_highOn[i]->setToolTip(QStringLiteral("勾选后高于上限即报警"));

        QLabel *name = new QLabel(QStringLiteral("%1 (%2)")
                                      .arg(QString::fromUtf8(DataModel::name(s)),
                                           QString::fromUtf8(DataModel::unit(s))), this);
        name->setStyleSheet(QStringLiteral("color:%1;font-weight:600;").arg(QLatin1String(kColors[i])));

        QLabel *rng = new QLabel(PhysRange::text(s), this);
        rng->setStyleSheet(QStringLiteral("color:#777;"));
        rng->setToolTip(QStringLiteral("这一项的物理合理区间；超出会标黄且不参与告警判定"));

        g->addWidget(name,         i + 1, 0);
        g->addWidget(m_low[i],     i + 1, 1);
        g->addWidget(m_lowOn[i],   i + 1, 2);
        g->addWidget(m_high[i],    i + 1, 3);
        g->addWidget(m_highOn[i],  i + 1, 4);
        g->addWidget(m_hyst[i],    i + 1, 5);
        g->addWidget(rng,          i + 1, 6);
    }

    QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QPushButton *btnDef = bb->addButton(QStringLiteral("恢复默认值"), QDialogButtonBox::ResetRole);
    connect(btnDef, &QPushButton::clicked, this, &ThresholdDialog::onDefaults);
    connect(bb, &QDialogButtonBox::accepted, this, &ThresholdDialog::onAccept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QLabel *note = new QLabel(QStringLiteral(
        "说明：\n"
        "· 勾掉某一侧，即该侧不参与告警判定（例：光照只关心太暗，只勾低）。\n"
        "· 回差（迟滞）：已经报警后，要走出 阈值 ± 回差 才解除，避免在报警线上下抖动反复报。\n"
        "· 改动只在内存中，点确定后生效，并写入 上位机配置.ini，下次启动自动读回。"), this);
    note->setStyleSheet(QStringLiteral("color:#555;"));

    QVBoxLayout *v = new QVBoxLayout(this);
    v->addLayout(g);
    v->addWidget(note);
    v->addWidget(bb);
    setMinimumWidth(620);
}

void ThresholdDialog::loadFrom(const AlarmManager &mgr)
{
    for (int i = 0; i < int(Series::Count); ++i) {
        const AlarmManager::Threshold &t = mgr.threshold(Series(i));
        m_low[i]->setValue(t.low);
        m_high[i]->setValue(t.high);
        m_hyst[i]->setValue(t.hyst);
        m_lowOn[i]->setChecked(t.lowEnabled);
        m_highOn[i]->setChecked(t.highEnabled);
    }
}

void ThresholdDialog::onDefaults()
{
    loadFrom(AlarmManager());            /* 默认构造 = 出厂阈值 */
}

void ThresholdDialog::onAccept()
{
    for (int i = 0; i < int(Series::Count); ++i) {
        const bool lo = m_lowOn[i]->isChecked();
        const bool hiOn = m_highOn[i]->isChecked();

        if (lo && hiOn && m_low[i]->value() >= m_high[i]->value()) {
            QMessageBox::warning(this, QStringLiteral("阈值不合法"),
                                 QStringLiteral("%1：下限 %2 ≥ 上限 %3。\n\n"
                                                "下限必须小于上限，否则判定逻辑先判上限，"
                                                "下限那一条永远不会命中。")
                                     .arg(QString::fromUtf8(DataModel::name(Series(i))))
                                     .arg(m_low[i]->value(), 0, 'f', DataModel::decimals(Series(i)))
                                     .arg(m_high[i]->value(), 0, 'f', DataModel::decimals(Series(i))));
            return;                      /* 不关闭对话框，留待修改 */
        }
    }

    for (int i = 0; i < int(Series::Count); ++i) {
        AlarmManager::Threshold t;       /* 从默认值起，避免漏字段 */
        t = m_mgr->threshold(Series(i));
        t.low         = m_low[i]->value();
        t.lowEnabled  = m_lowOn[i]->isChecked();
        t.high        = m_high[i]->value();
        t.highEnabled = m_highOn[i]->isChecked();
        t.hyst        = m_hyst[i]->value() > 0.0 ? m_hyst[i]->value() : 0.0;
        m_mgr->setThreshold(Series(i), t);
    }
    accept();
}
