#include "uploaderwindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>

/* ------------------------------------------------------------------ */
/*  构造与骨架                                                          */
/* ------------------------------------------------------------------ */

UploaderWindow::UploaderWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QString::fromUtf8("Git 上传"));

    QTabWidget *tabs = new QTabWidget(this);
    tabs->addTab(buildUploadTab(),  QString::fromUtf8("上传"));
    tabs->addTab(buildStateTab(),   QString::fromUtf8("仓库状态"));
    tabs->addTab(buildAccountTab(), QString::fromUtf8("账户设置"));
    tabs->addTab(buildIgnoreTab(),  QString::fromUtf8(".gitignore 助手"));
    tabs->addTab(buildLogTab(),     QString::fromUtf8("运行日志"));
    setCentralWidget(tabs);

    connect(&m_up, &Uploader::logLine, this, &UploaderWindow::logLine);

    /* 启动时：先报告 git 是否可用，再恢复上次用的目录 */
    QString ver;
    if (!Uploader::gitAvailable(&ver))
        logLine(QString::fromUtf8("[警告] 未检测到 git，请先安装：https://git-scm.com/download/win"));
    else
        logLine(QString::fromUtf8("[环境] %1").arg(ver));

    const QString last = m_store.raw(QString::fromUtf8("lastFolder")).toString();
    if (!last.isEmpty() && QDir(last).exists()) {
        m_folder = last;
        if (m_folderEdit)
            m_folderEdit->setText(last);
        refreshState();
    }
}

/* ------------------------------------------------------------------ */
/*  日志                                                                */
/* ------------------------------------------------------------------ */

void UploaderWindow::appendLog(const QString &line)
{
    logLine(line);
}

void UploaderWindow::logLine(const QString &s)
{
    if (!m_log)
        return;
    m_log->appendPlainText(QString::fromUtf8("[%1] %2")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), s));
}

/* ------------------------------------------------------------------ */
/*  上传页                                                              */
/* ------------------------------------------------------------------ */

QWidget *UploaderWindow::buildUploadTab()
{
    QWidget *w = new QWidget;
    QVBoxLayout *lay = new QVBoxLayout(w);

    /* --- 目录 + 配置档 --- */
    QGroupBox *g1 = new QGroupBox(QString::fromUtf8("目标项目"));
    QFormLayout *f1 = new QFormLayout(g1);

    QHBoxLayout *rowDir = new QHBoxLayout;
    m_folderEdit = new QLineEdit;
    m_folderEdit->setPlaceholderText(QString::fromUtf8("选择要上传到 GitHub 的文件夹"));
    QPushButton *pick = new QPushButton(QString::fromUtf8("浏览…"));
    rowDir->addWidget(m_folderEdit, 1);
    rowDir->addWidget(pick);
    f1->addRow(QString::fromUtf8("文件夹"), rowDir);

    QHBoxLayout *rowPf = new QHBoxLayout;
    m_profileBox = new QComboBox;
    m_profileBox->setMinimumWidth(200);
    QPushButton *goAcc = new QPushButton(QString::fromUtf8("管理账户…"));
    rowPf->addWidget(m_profileBox, 1);
    rowPf->addWidget(goAcc);
    f1->addRow(QString::fromUtf8("账户配置档"), rowPf);

    m_msgEdit = new QLineEdit;
    m_msgEdit->setPlaceholderText(QString::fromUtf8("留空 = 自动用「update 日期 时间」"));
    f1->addRow(QString::fromUtf8("提交说明"), m_msgEdit);

    lay->addWidget(g1);

    /* --- 动作条 --- */
    QHBoxLayout *bar = new QHBoxLayout;
    m_uploadBtn = new QPushButton(QString::fromUtf8("检查并上传"));
    m_uploadBtn->setMinimumHeight(34);
    QFont bf = m_uploadBtn->font();
    bf.setBold(true);
    m_uploadBtn->setFont(bf);

    QPushButton *preview = new QPushButton(QString::fromUtf8("预览变更"));
    QPushButton *refresh = new QPushButton(QString::fromUtf8("刷新"));
    QPushButton *pull    = new QPushButton(QString::fromUtf8("拉取合并"));
    bar->addWidget(m_uploadBtn);
    bar->addWidget(preview);
    bar->addWidget(refresh);
    bar->addWidget(pull);
    bar->addStretch(1);
    lay->addLayout(bar);

    m_summary = new QLabel(QString::fromUtf8("选择文件夹后点「刷新」查看状态。"));
    m_summary->setWordWrap(true);
    m_summary->setStyleSheet(QStringLiteral(
        "QLabel { background:#1e2a3a; border:1px solid #2f4a66; border-radius:6px;"
        " padding:8px 10px; color:#cfe3ff; }"));
    lay->addWidget(m_summary);

    lay->addWidget(new QLabel(QString::fromUtf8("变更预览")));
    m_diffOut = new QPlainTextEdit;
    m_diffOut->setReadOnly(true);
    m_diffOut->setPlaceholderText(QString::fromUtf8("点「预览变更」查看本次会提交哪些文件"));
    m_diffOut->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    lay->addWidget(m_diffOut, 1);

    /* --- 信号 --- */
    connect(pick, &QPushButton::clicked, this, &UploaderWindow::onPickFolder);
    connect(goAcc, &QPushButton::clicked, this, [this]() {
        if (QTabWidget *t = qobject_cast<QTabWidget *>(centralWidget()))
            t->setCurrentIndex(2);
    });
    connect(m_profileBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &UploaderWindow::onProfileChanged);
    connect(m_uploadBtn, &QPushButton::clicked, this, &UploaderWindow::onUpload);
    connect(preview, &QPushButton::clicked, this, &UploaderWindow::onPreviewDiff);
    connect(refresh, &QPushButton::clicked, this, &UploaderWindow::onRefresh);
    connect(pull,    &QPushButton::clicked, this, &UploaderWindow::onPull);

    reloadProfiles();
    return w;
}

/* ------------------------------------------------------------------ */
/*  仓库状态页                                                          */
/* ------------------------------------------------------------------ */

QWidget *UploaderWindow::buildStateTab()
{
    QWidget *w = new QWidget;
    QVBoxLayout *lay = new QVBoxLayout(w);

    QHBoxLayout *bar = new QHBoxLayout;
    QPushButton *refresh = new QPushButton(QString::fromUtf8("刷新状态"));
    QPushButton *initB   = new QPushButton(QString::fromUtf8("初始化仓库"));
    QPushButton *bindB   = new QPushButton(QString::fromUtf8("绑定/修改远程"));
    bar->addWidget(refresh);
    bar->addWidget(initB);
    bar->addWidget(bindB);
    bar->addStretch(1);
    lay->addLayout(bar);

    m_stateOut = new QLabel;
    m_stateOut->setTextFormat(Qt::RichText);
    m_stateOut->setAlignment(Qt::AlignTop);
    m_stateOut->setWordWrap(true);
    m_stateOut->setStyleSheet(QStringLiteral("QLabel { padding:10px; }"));
    lay->addWidget(m_stateOut, 1);

    connect(refresh, &QPushButton::clicked, this, &UploaderWindow::onRefresh);
    connect(initB,   &QPushButton::clicked, this, &UploaderWindow::onInitRepo);
    connect(bindB,   &QPushButton::clicked, this, &UploaderWindow::onBindRemote);
    return w;
}

void UploaderWindow::refreshState()
{
    if (!m_stateOut)
        return;
    if (m_folder.isEmpty() || !QDir(m_folder).exists()) {
        m_stateOut->setText(QString::fromUtf8(
            "<p style='color:#8aa0b8'>尚未选择文件夹。回到「上传」页点「浏览…」选一个项目目录。</p>"));
        if (m_summary)
            m_summary->setText(QString::fromUtf8("选择文件夹后点「刷新」查看状态。"));
        return;
    }

    const RepoState st = m_up.inspect(m_folder);
    QString h;
    auto row = [](const QString &k, const QString &v, const QString &color = QString()) {
        return QString::fromUtf8("<tr><td style='padding:3px 14px 3px 0;color:#8aa0b8'>%1</td>"
                                 "<td style='padding:3px 0;color:%2'>%3</td></tr>")
            .arg(k, color.isEmpty() ? QStringLiteral("#dbe7f3") : color, v);
    };

    h += QStringLiteral("<h3 style='margin:0 0 8px 0'>%1</h3>")
             .arg(m_folder.toHtmlEscaped());
    h += QStringLiteral("<table cellspacing='0'>");

    if (!st.isRepo) {
        h += row(QString::fromUtf8("git 仓库"), QString::fromUtf8("尚无（点上方「初始化仓库」）"),
                 QStringLiteral("#ffb454"));
        h += QStringLiteral("</table>");
        m_stateOut->setText(h);
        if (m_summary)
            m_summary->setText(QString::fromUtf8("这个文件夹还不是 git 仓库。点「初始化仓库」再上传。"));
        return;
    }

    h += row(QString::fromUtf8("仓库根"), st.toplevel.toHtmlEscaped());
    h += row(QString::fromUtf8("当前分支"), st.branch);
    h += row(QString::fromUtf8("远程 origin"),
             st.hasRemote ? st.remote.toHtmlEscaped() : QString::fromUtf8("未绑定"),
             st.hasRemote ? QString() : QStringLiteral("#ffb454"));

    const QString syncColor = (st.behind > 0) ? QStringLiteral("#ff7b72") : QString();
    h += row(QString::fromUtf8("与远程差异"),
             QString::fromUtf8("领先 %1 / 落后 %2").arg(st.ahead).arg(st.behind), syncColor);

    h += row(QString::fromUtf8("已暂存待提交"), QString::number(st.staged),
             st.staged ? QStringLiteral("#ffd166") : QString());
    h += row(QString::fromUtf8("已改动未暂存"), QString::number(st.unstaged),
             st.unstaged ? QStringLiteral("#ffd166") : QString());
    h += row(QString::fromUtf8("未跟踪文件"), QString::number(st.untracked),
             st.untracked ? QStringLiteral("#ffd166") : QString());

    h += row(QString::fromUtf8("提交身份"),
             st.userName.isEmpty() ? QString::fromUtf8("未配置（点「账户设置」）")
                                   : QString::fromUtf8("%1 <%2>").arg(st.userName, st.userEmail),
             st.userName.isEmpty() ? QStringLiteral("#ffb454") : QString());
    h += QStringLiteral("</table>");

    if (!st.recent.isEmpty()) {
        h += QStringLiteral("<p style='margin:12px 0 4px 0;color:#8aa0b8'>最近提交</p>");
        h += QStringLiteral("<pre style='color:#dbe7f3;margin:0'>%1</pre>")
                 .arg(st.recent.join('\n').toHtmlEscaped());
    }

    m_stateOut->setText(h);

    /* 上传页的摘要标签同步 */
    if (m_summary) {
        const int dirty = st.staged + st.unstaged + st.untracked;
        m_summary->setText(QString::fromUtf8(
            "分支 %1 · 远程 %2 · 待提交 %3 个 · 领先 %4 / 落后 %5")
            .arg(st.branch,
                 st.hasRemote ? QString::fromUtf8("已绑定") : QString::fromUtf8("未绑定"))
            .arg(dirty).arg(st.ahead).arg(st.behind));
    }
}

/* ------------------------------------------------------------------ */
/*  账户设置页                                                          */
/* ------------------------------------------------------------------ */

QWidget *UploaderWindow::buildAccountTab()
{
    QWidget *w = new QWidget;
    QVBoxLayout *lay = new QVBoxLayout(w);

    QLabel *hint = new QLabel(QString::fromUtf8(
        "这里存的是「要传到哪」和「以谁的名义提交」，可以存多套来回切。\n"
        "注意：GitHub 的登录凭据由系统凭据管理器保管，本工具不保存密码或 Token。"));
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("QLabel { color:#8aa0b8; padding:4px 0 10px 0; }"));
    lay->addWidget(hint);

    QGroupBox *g = new QGroupBox(QString::fromUtf8("配置档"));
    QFormLayout *f = new QFormLayout(g);

    QHBoxLayout *rowSel = new QHBoxLayout;
    m_accProfile = new QComboBox;
    QPushButton *bNew = new QPushButton(QString::fromUtf8("新建"));
    QPushButton *bDup = new QPushButton(QString::fromUtf8("重命名"));
    QPushButton *bDel = new QPushButton(QString::fromUtf8("删除"));
    rowSel->addWidget(m_accProfile, 1);
    rowSel->addWidget(bNew);
    rowSel->addWidget(bDup);
    rowSel->addWidget(bDel);
    f->addRow(QString::fromUtf8("选择"), rowSel);

    m_accName     = new QLineEdit; m_accName->setPlaceholderText(QString::fromUtf8("如：我的主仓库"));
    m_accRemote   = new QLineEdit; m_accRemote->setPlaceholderText(
        QString::fromUtf8("https://github.com/用户名/仓库名.git"));
    m_accBranch   = new QLineEdit; m_accBranch->setPlaceholderText(QStringLiteral("main"));
    m_accUserName = new QLineEdit; m_accUserName->setPlaceholderText(QString::fromUtf8("提交署名，如 Zhang San"));
    m_accUserMail = new QLineEdit; m_accUserMail->setPlaceholderText(QStringLiteral("you@example.com"));
    m_accFolder   = new QLineEdit; m_accFolder->setPlaceholderText(QString::fromUtf8("该档默认上传的文件夹，可留空"));
    m_accNote     = new QLineEdit; m_accNote->setPlaceholderText(QString::fromUtf8("备注，如：EnvMonitor 主仓"));

    f->addRow(QString::fromUtf8("配置档名"), m_accName);
    f->addRow(QString::fromUtf8("GitHub 账户"), m_accUserName);
    f->addRow(QString::fromUtf8("仓库地址"), m_accRemote);
    f->addRow(QString::fromUtf8("分支"), m_accBranch);
    f->addRow(QString::fromUtf8("提交邮箱"), m_accUserMail);
    f->addRow(QString::fromUtf8("默认文件夹"), m_accFolder);
    f->addRow(QString::fromUtf8("备注"), m_accNote);

    m_accGlobal = new QCheckBox(QString::fromUtf8(
        "把提交身份写进全局 git 配置（勾掉则只对当前仓库生效，推荐勾掉）"));
    f->addRow(QString(), m_accGlobal);

    lay->addWidget(g);

    QHBoxLayout *bar = new QHBoxLayout;
    QPushButton *save  = new QPushButton(QString::fromUtf8("保存配置档"));
    QPushButton *apply = new QPushButton(QString::fromUtf8("应用提交身份到当前仓库"));
    QPushButton *bind  = new QPushButton(QString::fromUtf8("绑定远程地址到当前仓库"));
    bar->addWidget(save);
    bar->addWidget(apply);
    bar->addWidget(bind);
    bar->addStretch(1);
    lay->addLayout(bar);
    lay->addStretch(1);

    connect(m_accProfile, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                const QString n = m_accProfile->currentText();
                if (n.isEmpty())
                    return;
                applyProfileToUi(m_store.get(n));
            });
    connect(bNew, &QPushButton::clicked, this, &UploaderWindow::onNewProfile);
    connect(bDel, &QPushButton::clicked, this, &UploaderWindow::onDeleteProfile);
    connect(bDup, &QPushButton::clicked, this, [this]() {
        const QString old = m_accProfile->currentText();
        if (old.isEmpty())
            return;
        bool ok = false;
        const QString nn = QInputDialog::getText(this, QString::fromUtf8("重命名配置档"),
            QString::fromUtf8("新名称："), QLineEdit::Normal, old + QString::fromUtf8(" 副本"), &ok);
        if (ok && !nn.trimmed().isEmpty()) {
            if (m_store.rename(old, nn.trimmed())) {
                reloadProfiles(nn.trimmed());
                logLine(QString::fromUtf8("配置档「%1」已重命名为「%2」").arg(old, nn.trimmed()));
            } else {
                QMessageBox::warning(this, QString::fromUtf8("重命名配置档"),
                    QString::fromUtf8("配置档「%1」已存在，或原配置档已不存在，未做修改。")
                        .arg(nn.trimmed()));
            }
        }
    });
    connect(save, &QPushButton::clicked, this, &UploaderWindow::onSaveProfile);
    connect(apply, &QPushButton::clicked, this, &UploaderWindow::onSaveIdentity);
    connect(bind, &QPushButton::clicked, this, &UploaderWindow::onBindRemote);

    return w;
}

void UploaderWindow::reloadProfiles(const QString &select)
{
    const QStringList ns = m_store.names();
    const QString act = m_store.activeName();

    if (m_profileBox) {
        const QString cur = select.isEmpty() ? m_profileBox->currentText() : select;
        m_profileBox->blockSignals(true);
        m_profileBox->clear();
        m_profileBox->addItems(ns);
        m_profileBox->blockSignals(false);
        const QString want = !cur.isEmpty() ? cur : act;
        const int i = ns.indexOf(want);
        if (i >= 0) {
            m_profileBox->blockSignals(true);
            m_profileBox->setCurrentIndex(i);
            m_profileBox->blockSignals(false);
        }
    }
    if (m_accProfile) {
        const QString cur = select.isEmpty() ? m_accProfile->currentText() : select;
        m_accProfile->blockSignals(true);
        m_accProfile->clear();
        m_accProfile->addItems(ns);
        m_accProfile->blockSignals(false);
        const QString want = !cur.isEmpty() ? cur : act;
        const int i = ns.indexOf(want);
        if (i >= 0)
            m_accProfile->setCurrentIndex(i);
    }
    if (!ns.isEmpty()) {
        Profile p = m_store.get(select.isEmpty() ? ns.first() : select);
        if (p.isValid())
            applyProfileToUi(p);
    }
}

void UploaderWindow::applyProfileToUi(const Profile &p)
{
    if (m_accName)     m_accName->setText(p.name);
    if (m_accRemote)   m_accRemote->setText(p.remote);
    if (m_accBranch)   m_accBranch->setText(p.branch);
    if (m_accUserName) m_accUserName->setText(p.userName);
    if (m_accUserMail) m_accUserMail->setText(p.userEmail);
    if (m_accFolder)   m_accFolder->setText(p.folder);
    if (m_accNote)     m_accNote->setText(p.note);
    if (m_accGlobal)   m_accGlobal->setChecked(false);
}

Profile UploaderWindow::collectProfileFromUi() const
{
    Profile p;
    if (m_accName)     p.name      = m_accName->text().trimmed();
    if (m_accUserName) p.userName  = m_accUserName->text().trimmed();
    if (m_accRemote)   p.remote    = m_accRemote->text().trimmed();
    if (m_accBranch)   p.branch    = m_accBranch->text().trimmed();
    if (m_accUserMail) p.userEmail = m_accUserMail->text().trimmed();
    if (m_accFolder)   p.folder    = m_accFolder->text().trimmed();
    if (m_accNote)     p.note      = m_accNote->text().trimmed();
    p.account = p.userName;
    if (p.branch.isEmpty())
        p.branch = QStringLiteral("main");
    return p;
}

void UploaderWindow::onNewProfile()
{
    bool ok = false;
    const QString n = QInputDialog::getText(this, QString::fromUtf8("新建配置档"),
        QString::fromUtf8("给这套配置起个名字："), QLineEdit::Normal,
        QString::fromUtf8("新配置"), &ok);
    if (!ok || n.trimmed().isEmpty())
        return;
    Profile p;
    p.name = n.trimmed();
    p.branch = QStringLiteral("main");
    m_store.save(p);
    reloadProfiles(p.name);
    logLine(QString::fromUtf8("已新建配置档「%1」").arg(p.name));
}

void UploaderWindow::onDeleteProfile()
{
    const QString n = m_accProfile ? m_accProfile->currentText() : QString();
    if (n.isEmpty())
        return;
    if (QMessageBox::question(this, QString::fromUtf8("删除配置档"),
            QString::fromUtf8("确定删除配置档「%1」吗？\n（只删本地记录，不影响 GitHub 上的仓库）").arg(n))
        != QMessageBox::Yes)
        return;
    m_store.remove(n);
    reloadProfiles();
    logLine(QString::fromUtf8("已删除配置档「%1」").arg(n));
}

void UploaderWindow::onSaveProfile()
{
    const Profile p = collectProfileFromUi();
    if (!p.isValid()) {
        QMessageBox::warning(this, QString::fromUtf8("保存配置档"),
                             QString::fromUtf8("配置档名不能为空。"));
        return;
    }
    m_store.save(p);
    m_store.setActive(p.name);
    reloadProfiles(p.name);
    logLine(QString::fromUtf8("已保存配置档「%1」").arg(p.name));
    if (m_profileBox) {
        m_profileBox->blockSignals(true);
        const int i = m_profileBox->findText(p.name);
        if (i >= 0)
            m_profileBox->setCurrentIndex(i);
        m_profileBox->blockSignals(false);
    }
}

void UploaderWindow::onSaveIdentity()
{
    const Profile p = collectProfileFromUi();
    if (p.userName.isEmpty() || p.userEmail.isEmpty()) {
        QMessageBox::warning(this, QString::fromUtf8("应用提交身份"),
            QString::fromUtf8("请先填写「GitHub 账户」与「提交邮箱」。"));
        return;
    }
    const bool global = m_accGlobal && m_accGlobal->isChecked();
    /* 仓库作用域要求先有仓库；没有就先在选定目录 init，避免 config 报错 */
    QString dir = m_folder;
    if (dir.isEmpty() || !QDir(dir).exists())
        dir = p.folder;
    if (dir.isEmpty() || !QDir(dir).exists()) {
        QMessageBox::warning(this, QString::fromUtf8("应用提交身份"),
            QString::fromUtf8("请先在上传页选择文件夹，或在配置档里填「默认文件夹」。"));
        return;
    }
    if (!global && !m_up.isRepo(dir))
        m_up.initRepo(dir, p.branch);

    const GitResult r = m_up.setIdentity(dir, p.userName, p.userEmail, global);
    if (r.ok)
        logLine(QString::fromUtf8("[完成] 提交身份已写入%1：%2 <%3>")
                    .arg(global ? QString::fromUtf8("全局配置") : QString::fromUtf8("当前仓库"),
                         p.userName, p.userEmail));
    else
        logLine(QString::fromUtf8("[失败] 写入提交身份失败：%1").arg(r.tail()));
    refreshState();
}

/* ------------------------------------------------------------------ */
/*  .gitignore 助手                                                     */
/* ------------------------------------------------------------------ */

QWidget *UploaderWindow::buildIgnoreTab()
{
    QWidget *w = new QWidget;
    QVBoxLayout *lay = new QVBoxLayout(w);

    QLabel *hint = new QLabel(QString::fromUtf8(
        "扫描当前文件夹里的编译产物（.o / .hex / Objects/ 等），勾选要忽略的规则后一键写进 .gitignore，"
        "避免把中间产物传上 GitHub。已存在的规则不会重复写。"));
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("QLabel { color:#8aa0b8; padding:4px 0 10px 0; }"));
    lay->addWidget(hint);

    QSplitter *sp = new QSplitter(Qt::Horizontal);

    QWidget *left = new QWidget;
    QVBoxLayout *ll = new QVBoxLayout(left);
    ll->setContentsMargins(0, 0, 0, 0);
    QHBoxLayout *lbar = new QHBoxLayout;
    QPushButton *scan = new QPushButton(QString::fromUtf8("扫描"));
    QPushButton *all  = new QPushButton(QString::fromUtf8("全选"));
    QPushButton *none = new QPushButton(QString::fromUtf8("全不选"));
    QPushButton *hit  = new QPushButton(QString::fromUtf8("只选命中的"));
    lbar->addWidget(scan);
    lbar->addWidget(all);
    lbar->addWidget(none);
    lbar->addWidget(hit);
    lbar->addStretch(1);
    ll->addLayout(lbar);

    m_ignoreList = new QListWidget;
    ll->addWidget(m_ignoreList, 1);
    sp->addWidget(left);

    QWidget *right = new QWidget;
    QVBoxLayout *rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->addWidget(new QLabel(QString::fromUtf8("现有 .gitignore")));
    m_ignorePreview = new QPlainTextEdit;
    m_ignorePreview->setReadOnly(true);
    m_ignorePreview->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    rl->addWidget(m_ignorePreview, 1);
    sp->addWidget(right);
    sp->setSizes(QList<int>() << 420 << 420);
    lay->addWidget(sp, 1);

    QHBoxLayout *bar = new QHBoxLayout;
    QPushButton *write = new QPushButton(QString::fromUtf8("把勾选的规则写进 .gitignore"));
    write->setMinimumHeight(30);
    bar->addWidget(write);
    bar->addStretch(1);
    lay->addLayout(bar);

    connect(scan, &QPushButton::clicked, this, &UploaderWindow::onScanIgnore);
    connect(write, &QPushButton::clicked, this, &UploaderWindow::onWriteIgnore);
    connect(all, &QPushButton::clicked, this, [this]() {
        for (int i = 0; i < m_ignoreList->count(); ++i)
            m_ignoreList->item(i)->setCheckState(Qt::Checked);
    });
    connect(none, &QPushButton::clicked, this, [this]() {
        for (int i = 0; i < m_ignoreList->count(); ++i)
            m_ignoreList->item(i)->setCheckState(Qt::Unchecked);
    });
    connect(hit, &QPushButton::clicked, this, [this]() {
        for (int i = 0; i < m_ignoreList->count(); ++i) {
            QListWidgetItem *it = m_ignoreList->item(i);
            const bool has = it->data(Qt::UserRole + 1).toBool();
            it->setCheckState(has ? Qt::Checked : Qt::Unchecked);
        }
    });
    return w;
}

void UploaderWindow::onScanIgnore()
{
    if (!ensureFolderReady())
        return;
    m_ignoreRules = m_up.scanIgnoreCandidates(m_folder);
    m_ignoreList->clear();
    int hitCount = 0;
    for (const IgnoreRule &r : m_ignoreRules) {
        if (r.hits)
            ++hitCount;
        QListWidgetItem *it = new QListWidgetItem(
            QString::fromUtf8("%1    %2%3")
                .arg(r.pattern, r.why,
                     r.hits ? QString::fromUtf8("  ← 本目录有") : QString()));
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(r.hits ? Qt::Checked : Qt::Unchecked);
        it->setData(Qt::UserRole, r.pattern);
        it->setData(Qt::UserRole + 1, r.hits > 0);
        m_ignoreList->addItem(it);
    }
    m_ignorePreview->setPlainText(
        m_up.readGitignore(m_folder).isEmpty()
            ? QString::fromUtf8("（当前没有 .gitignore，写入时会新建）")
            : m_up.readGitignore(m_folder));
    logLine(QString::fromUtf8("已扫描忽略候选：%1 条，其中本目录命中 %2 条")
                .arg(m_ignoreRules.size()).arg(hitCount));
}

void UploaderWindow::onWriteIgnore()
{
    if (!ensureFolderReady())
        return;
    QStringList rules;
    for (int i = 0; i < m_ignoreList->count(); ++i) {
        QListWidgetItem *it = m_ignoreList->item(i);
        if (it->checkState() == Qt::Checked)
            rules << it->data(Qt::UserRole).toString();
    }
    if (rules.isEmpty()) {
        QMessageBox::information(this, QString::fromUtf8(".gitignore 助手"),
                                 QString::fromUtf8("没有勾选任何规则。"));
        return;
    }
    const GitResult r = m_up.writeGitignore(m_folder, rules);
    logLine(r.out);
    m_ignorePreview->setPlainText(m_up.readGitignore(m_folder));
    QMessageBox::information(this, QString::fromUtf8(".gitignore 助手"), r.out);
}

/* ------------------------------------------------------------------ */
/*  日志页                                                              */
/* ------------------------------------------------------------------ */

QWidget *UploaderWindow::buildLogTab()
{
    QWidget *w = new QWidget;
    QVBoxLayout *lay = new QVBoxLayout(w);

    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    lay->addWidget(m_log, 1);

    QHBoxLayout *bar = new QHBoxLayout;
    QPushButton *save = new QPushButton(QString::fromUtf8("另存为…"));
    QPushButton *clr  = new QPushButton(QString::fromUtf8("清空"));
    bar->addWidget(save);
    bar->addWidget(clr);
    bar->addStretch(1);
    lay->addLayout(bar);

    connect(save, &QPushButton::clicked, this, [this]() {
        const QString f = QFileDialog::getSaveFileName(this, QString::fromUtf8("保存日志"),
            QStringLiteral("git-upload-log.txt"), QStringLiteral("文本 (*.txt)"));
        if (f.isEmpty())
            return;
        QFile out(f);
        if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            out.write(m_log->toPlainText().toUtf8());
            out.close();
        }
    });
    connect(clr, &QPushButton::clicked, this, [this]() { m_log->clear(); });
    return w;
}

/* ------------------------------------------------------------------ */
/*  交互动作                                                            */
/* ------------------------------------------------------------------ */

void UploaderWindow::onPickFolder()
{
    const QString start = m_folderEdit && !m_folderEdit->text().isEmpty()
                              ? m_folderEdit->text() : QDir::homePath();
    const QString d = QFileDialog::getExistingDirectory(this,
        QString::fromUtf8("选择要上传的项目文件夹"), start);
    if (d.isEmpty())
        return;
    setFolder(d);
}

void UploaderWindow::setFolder(const QString &folder)
{
    m_folder = folder;
    if (m_folderEdit)
        m_folderEdit->setText(folder);
    m_store.setRaw(QString::fromUtf8("lastFolder"), folder);
    logLine(QString::fromUtf8("目标文件夹：%1").arg(folder));
    refreshState();
}

void UploaderWindow::onProfileChanged(int index)
{
    if (!m_profileBox || index < 0)
        return;
    const QString n = m_profileBox->currentText();
    if (n.isEmpty())
        return;
    m_store.setActive(n);
    const Profile p = m_store.get(n);
    /* 配置档带了默认文件夹，且当前没选目录时，顺手填上 */
    if (!p.folder.isEmpty() && QDir(p.folder).exists()
        && (m_folder.isEmpty() || !QDir(m_folder).exists()))
        setFolder(p.folder);
    logLine(QString::fromUtf8("已切到配置档「%1」%2")
                .arg(n, p.remote.isEmpty() ? QString() : QString::fromUtf8("（%1）").arg(p.remote)));
}

void UploaderWindow::onRefresh()
{
    if (m_folder.isEmpty() || !QDir(m_folder).exists()) {
        onPickFolder();
        return;
    }
    refreshState();
}

void UploaderWindow::onInitRepo()
{
    if (m_folder.isEmpty() || !QDir(m_folder).exists()) {
        QMessageBox::warning(this, QString::fromUtf8("初始化仓库"),
                             QString::fromUtf8("请先选择文件夹。"));
        return;
    }
    if (m_up.isRepo(m_folder)) {
        QMessageBox::information(this, QString::fromUtf8("初始化仓库"),
                                 QString::fromUtf8("这个文件夹已经是 git 仓库了。"));
        return;
    }
    const Profile p = m_store.activeProfile();
    const QString br = p.branch.isEmpty() ? QStringLiteral("main") : p.branch;
    if (QMessageBox::question(this, QString::fromUtf8("初始化仓库"),
            QString::fromUtf8("在此文件夹执行 git init 并切到分支 %1？").arg(br))
        != QMessageBox::Yes)
        return;
    startBusy(QString::fromUtf8("初始化仓库"));
    const GitResult r = m_up.initRepo(m_folder, br);
    endBusy();
    logLine(r.ok ? QString::fromUtf8("[完成] 已初始化仓库，分支 %1").arg(br)
                   : QString::fromUtf8("[失败] 初始化失败：%1").arg(r.tail()));
    refreshState();
}

void UploaderWindow::onBindRemote()
{
    QString dir = m_folder;
    if (dir.isEmpty() || !QDir(dir).exists()) {
        Profile p = m_store.activeProfile();
        dir = p.folder;
    }
    Profile p = collectProfileFromUi();
    if (p.remote.isEmpty())
        p = m_store.activeProfile();
    if (p.remote.isEmpty()) {
        QMessageBox::warning(this, QString::fromUtf8("绑定远程"),
            QString::fromUtf8("请先在「账户设置」里填仓库地址。"));
        return;
    }
    if (dir.isEmpty() || !QDir(dir).exists()) {
        QMessageBox::warning(this, QString::fromUtf8("绑定远程"),
            QString::fromUtf8("请先选择文件夹。"));
        return;
    }
    if (!m_up.isRepo(dir))
        m_up.initRepo(dir, p.branch);
    const GitResult r = m_up.setRemote(dir, p.remote);
    logLine(r.ok ? QString::fromUtf8("[完成] 远程已绑定：%1").arg(p.remote)
                   : QString::fromUtf8("[失败] 绑定远程失败：%1").arg(r.tail()));
    refreshState();
}

void UploaderWindow::onPreviewDiff()
{
    if (!ensureFolderReady())
        return;
    /* 先暂存才能列出「本次将提交」的清单；暂存不改工作区内容，安全 */
    m_up.addAll(m_folder);
    const Uploader::StagedInfo si = m_up.staged(m_folder);
    const QStringList files = si.files;
    const Uploader::ChangeStat st = si.stat;

    QStringList out;
    out << QString::fromUtf8("本次将提交 %1 个文件（新增 %2 / 修改 %3 / 删除 %4）")
               .arg(files.size()).arg(st.added).arg(st.modified).arg(st.deleted);
    out << QString();
    const int showN = qMin(200, files.size());
    for (int i = 0; i < showN; ++i)
        out << files.at(i);
    if (files.size() > showN)
        out << QString::fromUtf8("…（其余 %1 个略）").arg(files.size() - showN);

    const QList<Uploader::BigFile> big = m_up.bigFiles(m_folder, files);
    if (!big.isEmpty()) {
        out << QString();
        out << QString::fromUtf8("大文件提醒：");
        for (const Uploader::BigFile &b : big)
            out << QString::fromUtf8("  %1 %2 MB %3").arg(b.path)
                       .arg(b.bytes / 1024.0 / 1024.0, 0, 'f', 1)
                       .arg(b.hard ? QString::fromUtf8("← 超过 100MB，GitHub 会拒绝")
                                   : QString::fromUtf8("← 超过 50MB，上传较慢"));
    }
    m_diffOut->setPlainText(out.join('\n'));
    logLine(QString::fromUtf8("变更预览：%1 个文件").arg(files.size()));
}

void UploaderWindow::onPull()
{
    if (!ensureFolderReady())
        return;
    const Profile p = m_store.activeProfile();
    const QString br = p.branch.isEmpty() ? m_up.currentBranch(m_folder) : p.branch;
    if (QMessageBox::question(this, QString::fromUtf8("拉取合并"),
            QString::fromUtf8("从远程拉取并合并到本地分支 %1？\n（会用到 git pull --no-rebase）").arg(br))
        != QMessageBox::Yes)
        return;
    startBusy(QString::fromUtf8("拉取合并"));
    const GitResult r = m_up.pull(m_folder, br);
    endBusy();
    if (r.ok) {
        logLine(QString::fromUtf8("[完成] 已拉取合并"));
    } else {
        logLine(QString::fromUtf8("[失败] 拉取失败：%1").arg(Uploader::explainFailure(r.out)));
        logLine(r.tail(20));
    }
    refreshState();
}

/* ------------------------------------------------------------------ */
/*  核心：检查并上传                                                     */
/* ------------------------------------------------------------------ */

void UploaderWindow::onUpload()
{
    if (m_busy)
        return;
    if (m_folder.isEmpty() || !QDir(m_folder).exists()) {
        QMessageBox::warning(this, QString::fromUtf8("上传"),
                             QString::fromUtf8("请先选择要上传的文件夹。"));
        return;
    }
    if (!Uploader::gitAvailable()) {
        QMessageBox::critical(this, QString::fromUtf8("上传"),
            QString::fromUtf8("未检测到 git。请先安装 Git for Windows：\n"
                              "https://git-scm.com/download/win"));
        return;
    }

    Profile p = m_store.activeProfile();

    /* 从这里起要跑一串后端命令（isRepo / remoteUrl / inspect …），先置忙，
       免得这些步骤耗时的空档里用户连点，触发两轮并发 init/add/commit/push。 */
    startBusy(QString::fromUtf8("上传中"));

    /* 配置档没绑仓库地址，但目录已经绑了 → 以目录里的为准 */
    if (!m_up.isRepo(m_folder)) {
        if (QMessageBox::question(this, QString::fromUtf8("上传"),
                QString::fromUtf8("这个文件夹还不是 git 仓库，现在初始化吗？"))
            != QMessageBox::Yes) {
            endBusy();
            return;
        }
        const QString br = p.branch.isEmpty() ? QStringLiteral("main") : p.branch;
        const GitResult ir = m_up.initRepo(m_folder, br);
        if (!ir.ok) {
            logLine(QString::fromUtf8("[失败] 初始化仓库失败：%1").arg(ir.tail()));
            endBusy();
            refreshState();
            return;
        }
    }

    const QString localRemote = m_up.remoteUrl(m_folder);
    if (localRemote.isEmpty()) {
        if (p.remote.isEmpty()) {
            QMessageBox::warning(this, QString::fromUtf8("上传"),
                QString::fromUtf8("这个仓库还没绑定远程地址。\n"
                                  "请到「账户设置」填好仓库地址并保存，再回来上传。"));
            endBusy();
            if (QTabWidget *t = qobject_cast<QTabWidget *>(centralWidget()))
                t->setCurrentIndex(2);
            return;
        }
        const GitResult rr = m_up.setRemote(m_folder, p.remote);
        if (!rr.ok) {
            logLine(QString::fromUtf8("[失败] 绑定远程失败：%1").arg(rr.tail()));
            endBusy();
            return;
        }
        logLine(QString::fromUtf8("[完成] 已绑定远程：%1").arg(p.remote));
    }

    /* 提交身份兜底：仓库没配就用配置档里的 */
    const RepoState st0 = m_up.inspect(m_folder);
    if (st0.userName.isEmpty() || st0.userEmail.isEmpty()) {
        if (p.userName.isEmpty() || p.userEmail.isEmpty()) {
            QMessageBox::warning(this, QString::fromUtf8("上传"),
                QString::fromUtf8("git 提交身份未配置。\n"
                                  "请到「账户设置」填写账户与邮箱并保存（或点「应用提交身份」）。"));
            endBusy();
            if (QTabWidget *t = qobject_cast<QTabWidget *>(centralWidget()))
                t->setCurrentIndex(2);
            return;
        }
        const GitResult idr = m_up.setIdentity(m_folder, p.userName, p.userEmail, false);
        if (!idr.ok)
            logLine(QString::fromUtf8("[警告] 应用提交身份失败：%1").arg(idr.tail()));
    }

    /* 提交说明 */
    QString msg = m_msgEdit ? m_msgEdit->text().trimmed() : QString();
    if (msg.isEmpty())
        msg = QString::fromUtf8("update %1")
                  .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")));

    logLine(QString::fromUtf8("===== 开始上传 ====="));

    /* 1) 暂存 */
    const GitResult ar = m_up.addAll(m_folder);
    if (!ar.ok) {
        logLine(QString::fromUtf8("[失败] 暂存失败：%1").arg(ar.tail()));
        endBusy();
        refreshState();
        return;
    }

    /* 2) 变更预览 + 大文件保护 */
    const Uploader::StagedInfo si = m_up.staged(m_folder);
    const QStringList files = si.files;
    const Uploader::ChangeStat stt = si.stat;
    logLine(QString::fromUtf8("本次将提交 %1 个文件（新增 %2 / 修改 %3 / 删除 %4）")
                .arg(files.size()).arg(stt.added).arg(stt.modified).arg(stt.deleted));

    const QList<Uploader::BigFile> big = m_up.bigFiles(m_folder, files);
    QStringList hard;
    for (const Uploader::BigFile &b : big) {
        const QString s = QString::fromUtf8("%1（%2 MB）").arg(b.path)
                              .arg(b.bytes / 1024.0 / 1024.0, 0, 'f', 1);
        if (b.hard)
            hard << s;
        else
            logLine(QString::fromUtf8("[警告] 大文件 %1，上传会较慢").arg(s));
    }
    if (!hard.isEmpty()) {
        endBusy();
        if (QMessageBox::question(this, QString::fromUtf8("大文件保护"),
                QString::fromUtf8("以下文件超过 100MB，GitHub 会拒绝：\n\n%1\n\n"
                                  "建议先到「.gitignore 助手」排除它们。\n"
                                  "仍要继续吗？").arg(hard.join('\n')))
            != QMessageBox::Yes) {
            logLine(QString::fromUtf8("[提示] 已取消上传；刚才暂存的文件仍留在暂存区，本次未提交。"));
            return;
        }
        startBusy(QString::fromUtf8("上传中"));
    }

    /* 3) 提交（没有变更就跳过，但仍可能只需推送） */
    if (!files.isEmpty()) {
        const GitResult cr = m_up.commit(m_folder, msg);
        if (!cr.ok) {
            const QString why = Uploader::explainFailure(cr.out);
            logLine(QString::fromUtf8("[失败] 提交失败：%1").arg(why));
            logLine(cr.tail(20));
            endBusy();
            QMessageBox::critical(this, QString::fromUtf8("提交失败"), why);
            refreshState();
            return;
        }
        logLine(QString::fromUtf8("[完成] 已提交：%1").arg(msg));
    } else {
        logLine(QString::fromUtf8("[提示] 没有新变更需要提交，直接尝试推送。"));
    }

    /* 4) 推送 */
    /* 提交落在当前分支上，推送也用当前分支，免得与配置档里写的分支不一致 */
    const QString br = m_up.currentBranch(m_folder);
    const GitResult pr = m_up.push(m_folder, br);
    endBusy();

    if (pr.ok) {
        if (pr.out.contains(QStringLiteral("Everything up-to-date")))
            logLine(QString::fromUtf8("[完成] 远程已是最新，没有新内容需要上传"));
        else
            logLine(QString::fromUtf8("[成功] 已上传到 GitHub"));
        refreshState();
        QMessageBox::information(this, QString::fromUtf8("上传完成"),
            files.isEmpty()
                ? QString::fromUtf8("远程已是最新，没有新内容需要上传。")
                : QString::fromUtf8("已上传 %1 个文件到 GitHub。").arg(files.size()));
    } else {
        const QString why = Uploader::explainFailure(pr.out);
        logLine(QString::fromUtf8("[失败] 推送失败：%1").arg(why));
        logLine(pr.tail(20));
        refreshState();
        QMessageBox::critical(this, QString::fromUtf8("推送失败"), why);
    }
}

/* ------------------------------------------------------------------ */
/*  工具方法                                                            */
/* ------------------------------------------------------------------ */

void UploaderWindow::startBusy(const QString &what)
{
    m_busy = true;
    if (m_uploadBtn) {
        m_uploadBtn->setEnabled(false);
        m_uploadBtn->setText(QString::fromUtf8("%1…").arg(what));
    }
    QApplication::setOverrideCursor(Qt::BusyCursor);
}

void UploaderWindow::endBusy()
{
    m_busy = false;
    if (m_uploadBtn) {
        m_uploadBtn->setEnabled(true);
        m_uploadBtn->setText(QString::fromUtf8("检查并上传"));
    }
    QApplication::restoreOverrideCursor();
}

bool UploaderWindow::ensureFolderReady()
{
    if (m_folder.isEmpty() || !QDir(m_folder).exists()) {
        QMessageBox::warning(this, QString::fromUtf8("提示"),
                             QString::fromUtf8("请先选择文件夹。"));
        return false;
    }
    return true;
}
