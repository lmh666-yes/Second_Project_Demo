#ifndef UPLOADERWINDOW_H
#define UPLOADERWINDOW_H

#include <QMainWindow>
#include <QMap>

#include "uploader.h"
#include "profile.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QCheckBox;
class QListWidget;

class UploaderWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit UploaderWindow(QWidget *parent = nullptr);

    /* 供综合程序内嵌：重新扫描当前目录 / 切换项目 */
    void setFolder(const QString &folder);
    QString folder() const { return m_folder; }

    /* 供综合程序调用：把外部一段日志贴进日志页 */
    void appendLog(const QString &line);

private slots:
    void onPickFolder();
    void onProfileChanged(int index);
    void onSaveProfile();
    void onNewProfile();
    void onDeleteProfile();
    void onUpload();
    void onRefresh();
    void onPull();
    void onInitRepo();
    void onBindRemote();
    void onPreviewDiff();
    void onScanIgnore();
    void onWriteIgnore();
    void onSaveIdentity();

private:
    QWidget *buildUploadTab();
    QWidget *buildStateTab();
    QWidget *buildAccountTab();
    QWidget *buildIgnoreTab();
    QWidget *buildLogTab();

    void reloadProfiles(const QString &select = QString());
    void applyProfileToUi(const Profile &p);
    Profile collectProfileFromUi() const;
    void startBusy(const QString &what);
    void endBusy();
    bool ensureFolderReady();          /* 目录存在且是（或已问过是否 init）仓库 */

    Uploader     m_up;
    ProfileStore m_store;

    QString m_folder;

    /* 上传页 */
    QLineEdit      *m_folderEdit = nullptr;
    QComboBox      *m_profileBox = nullptr;
    QLineEdit      *m_msgEdit    = nullptr;
    QPushButton    *m_uploadBtn  = nullptr;
    QLabel         *m_summary    = nullptr;
    QPlainTextEdit *m_diffOut    = nullptr;

    /* 状态页 */
    QLabel         *m_stateOut   = nullptr;

    /* 账户页 */
    QComboBox      *m_accProfile = nullptr;
    QLineEdit      *m_accName    = nullptr;
    QLineEdit      *m_accRemote  = nullptr;
    QLineEdit      *m_accBranch  = nullptr;
    QLineEdit      *m_accUserName = nullptr;
    QLineEdit      *m_accUserMail = nullptr;
    QLineEdit      *m_accFolder  = nullptr;
    QLineEdit      *m_accNote    = nullptr;
    QCheckBox      *m_accGlobal  = nullptr;

    /* 忽略助手页 */
    QListWidget    *m_ignoreList = nullptr;
    QPlainTextEdit *m_ignorePreview = nullptr;
    QList<IgnoreRule> m_ignoreRules;

    /* 日志页 */
    QPlainTextEdit *m_log = nullptr;

    bool m_busy = false;

    void logLine(const QString &s);
    void refreshState();
};

#endif // UPLOADERWINDOW_H
