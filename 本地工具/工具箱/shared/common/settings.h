#ifndef SETTINGS_H
#define SETTINGS_H

/* 统一的界面状态记忆。所有键都加 "toolbox/" 前缀，落地为工具箱目录下的 INI，
   不写系统注册表，换机器拷走目录即可连状态一起带走。

   用法：
     Settings s;                        // 默认按程序位置定位 INI
     s.setValue("pin.board", 1);
     int b = s.value("pin.board", 0).toInt(); */

#include <QSettings>
#include <QString>
#include <QVariant>

class QWidget;

class Settings {
public:
    Settings();

    void     setValue(const QString &key, const QVariant &v);
    QVariant value(const QString &key, const QVariant &def = QVariant()) const;

    /* 窗口几何：保存/恢复大小与位置。 */
    void saveGeometry(const QString &key, const QWidget *w);
    void restoreGeometry(const QString &key, QWidget *w);

    QString filePath() const;

private:
    QString m_path;
};

#endif // SETTINGS_H
