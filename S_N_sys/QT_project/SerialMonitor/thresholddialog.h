#ifndef THRESHOLDDIALOG_H
#define THRESHOLDDIALOG_H

/* ============================================================================
 *  thresholddialog.h —— 告警阈值设置对话框（6 项 × 下限/上限/启用/回差）
 *  单开一个对话框：主界面左栏固定 330 px，阈值表再加 12 个启用勾选框会把
 *  thresholdBox 的 minimumSizeHint 撑到 600+ px，而 QSplitter 按子控件的
 *  minimumSizeHint 分配宽度，中间那栏曲线图会被挤没。
 *  主界面的 4 列（项目/下限/上限/回差）仍可直接改，约定改哪个框即启用哪一侧，
 *  否则用户改了值却不生效；只想判一侧（如光照只关心太暗）就在本对话框关掉另一侧。
 * ==========================================================================*/

#include "alarmmanager.h"

#include <QDialog>

class QCheckBox;
class QDoubleSpinBox;

class ThresholdDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ThresholdDialog(AlarmManager *mgr, QWidget *parent = nullptr);

    /* 模态打开；点了"确定"才把值写回 mgr，返回 true */
    static bool edit(AlarmManager *mgr, QWidget *parent);

private slots:
    void onDefaults();
    void onAccept();

private:
    void build();
    void loadFrom(const AlarmManager &mgr);

    AlarmManager *m_mgr = nullptr;

    QDoubleSpinBox *m_low[int(Series::Count)]  = { nullptr };
    QDoubleSpinBox *m_high[int(Series::Count)] = { nullptr };
    QDoubleSpinBox *m_hyst[int(Series::Count)] = { nullptr };
    QCheckBox      *m_lowOn[int(Series::Count)]  = { nullptr };
    QCheckBox      *m_highOn[int(Series::Count)] = { nullptr };
};

#endif  /* THRESHOLDDIALOG_H */
