#include "kickstartwindow.h"

#include "settings.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

KickStartWindow::KickStartWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_specs = defaultSpecs();
    setWindowTitle(QStringLiteral("项目起手助手"));

    QWidget *central = new QWidget(this);
    QHBoxLayout *root = new QHBoxLayout(central);

    m_steps = new QListWidget(central);
    m_steps->setFixedWidth(190);
    for (const PageSpec &p : m_specs)
        m_steps->addItem(p.title);

    m_pages = new QStackedWidget(central);
    for (const PageSpec &p : m_specs)
        m_pages->addWidget(buildPage(p));

    m_preview = new QTextBrowser(central);

    QSplitter *split = new QSplitter(Qt::Vertical, central);
    split->addWidget(m_pages);
    split->addWidget(m_preview);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);

    QVBoxLayout *right = new QVBoxLayout;
    right->addWidget(split, 1);

    /* F2「图形界面大小」：原来 7 个按钮挤在一行，最小宽度 737px，把本页顶到 951px、
       进而把主窗口顶到 1195px 宽（笔记本上缩不小）。拆成两行：第一行"走流程"，
       第二行"草稿与导出"，两行各自靠左留白，窄窗口也不会挤成一团。 */
    QHBoxLayout *bar = new QHBoxLayout;
    m_btnPrev  = new QPushButton(QStringLiteral("上一步"), central);
    m_btnNext  = new QPushButton(QStringLiteral("下一步"), central);
    QPushButton *btnCopy   = new QPushButton(QStringLiteral("复制 Markdown"), central);
    QPushButton *btnExport = new QPushButton(QStringLiteral("导出 Markdown"), central);
    QPushButton *btnLoad   = new QPushButton(QStringLiteral("载入草稿"), central);
    QPushButton *btnSave   = new QPushButton(QStringLiteral("保存草稿"), central);
    /* C2：把答案里的外设清单与任务表喂给「工程模板」生成器 */
    QPushButton *btnScaffold = new QPushButton(QStringLiteral("一键生成工程骨架…"), central);
    btnScaffold->setToolTip(QStringLiteral(
        "把上面填的「外设清单」与「任务表」转成工程模板生成器的参数，"
        "跳到「工程模板」页并生成一份能编的骨架。"));
    bar->addWidget(m_btnPrev);
    bar->addWidget(m_btnNext);
    bar->addStretch(1);
    bar->addWidget(btnScaffold);
    QHBoxLayout *bar2 = new QHBoxLayout;
    bar2->addWidget(btnLoad);
    bar2->addWidget(btnSave);
    bar2->addWidget(btnCopy);
    bar2->addWidget(btnExport);
    bar2->addStretch(1);
    right->addLayout(bar);
    right->addLayout(bar2);

    root->addWidget(m_steps);
    root->addLayout(right, 1);
    setCentralWidget(central);

    connect(m_steps, &QListWidget::currentRowChanged, this, &KickStartWindow::onStepChanged);
    connect(m_btnPrev, &QPushButton::clicked, this, &KickStartWindow::onPrev);
    connect(m_btnNext, &QPushButton::clicked, this, &KickStartWindow::onNext);
    connect(btnCopy, &QPushButton::clicked, this, &KickStartWindow::onCopy);
    connect(btnExport, &QPushButton::clicked, this, &KickStartWindow::onExport);
    connect(btnSave, &QPushButton::clicked, this, &KickStartWindow::onSave);
    connect(btnLoad, &QPushButton::clicked, this, &KickStartWindow::onLoad);
    /* C2：外设清单 + 任务表 → 生成器参数，交给主窗口去跳页并生成 */
    connect(btnScaffold, &QPushButton::clicked, this, [this]() {
        const QString per = scaffoldPeriphArg(answer(QStringLiteral("peripherals")),
                                             answer(QStringLiteral("rtos")));
        QStringList skipped;
        const QString tsk = scaffoldTasksArg(answer(QStringLiteral("tasks")), &skipped);
        emit scaffoldRequested(per, tsk, skipped);
    });

    m_steps->setCurrentRow(0);
}

QWidget *KickStartWindow::buildPage(const PageSpec &page)
{
    QWidget *content = new QWidget;
    QVBoxLayout *lay = new QVBoxLayout(content);

    QLabel *title = new QLabel(page.title, content);
    QFont tf = title->font();
    tf.setPointSize(tf.pointSize() + 4);
    tf.setBold(true);
    title->setFont(tf);
    lay->addWidget(title);

    QLabel *teach = new QLabel(page.teach, content);
    teach->setWordWrap(true);
    teach->setStyleSheet(QStringLiteral("color:%1;").arg(QStringLiteral("#8a8a8a")));
    lay->addWidget(teach);

    for (const FieldSpec &f : page.fields) {
        QLabel *fl = new QLabel(f.label, content);
        QFont lf = fl->font();
        lf.setBold(true);
        fl->setFont(lf);
        lay->addWidget(fl);

        /* 为什么这么写：常驻显示，是这一格的判断标准 */
        if (!f.why.isEmpty()) {
            QLabel *why = new QLabel(QStringLiteral("为什么这么写：%1").arg(f.why), content);
            why->setWordWrap(true);
            why->setStyleSheet(QStringLiteral("color:%1;").arg(QStringLiteral("#7a9ab8")));
            lay->addWidget(why);
        }

        QWidget *editor = nullptr;
        if (f.multi) {
            QPlainTextEdit *e = new QPlainTextEdit(content);
            e->setPlaceholderText(f.hint);
            e->setMinimumHeight(88);
            m_editors.insert(f.id, e);
            connect(e, &QPlainTextEdit::textChanged, this, [this, f, e]() {
                m_answers[f.id] = e->toPlainText();
                refreshPreview();
            });
            editor = e;
            lay->addWidget(e);
        } else {
            QLineEdit *e = new QLineEdit(content);
            e->setPlaceholderText(f.hint);
            m_editors.insert(f.id, e);
            connect(e, &QLineEdit::textChanged, this, [this, f](const QString &t) {
                m_answers[f.id] = t;
                refreshPreview();
            });
            editor = e;
            lay->addWidget(e);
        }

        /* 示例答案：默认折叠，卡住时展开照着改；可一键填入输入框 */
        if (!f.demo.isEmpty() && editor) {
            const QString demo = f.demo;
            const QString id = f.id;

            QPushButton *toggle = new QPushButton(QStringLiteral("看示例 / 填入示例"), content);
            toggle->setFlat(true);
            toggle->setCursor(Qt::PointingHandCursor);
            /* F2：界面上所有按钮总高必须一种。全局 QSS 给按钮定的是
               min/max-height 22px（内容高），总高 = 内容 22 + padding 12 + 边框 2 = 36；
               这里原来写 border:none 会少掉 2px、变成 34px，是量测里唯一"另类"的按钮。
               改成 1px 透明边框即可：看着仍是"无框文字按钮"，尺寸却与别的按钮一致。 */
            toggle->setStyleSheet(QStringLiteral("text-align:left;color:%1;border:1px solid transparent;")
                                      .arg(QStringLiteral("#6fa8dc")));

            QLabel *demoBox = new QLabel(demo, content);
            demoBox->setWordWrap(true);
            demoBox->setTextInteractionFlags(Qt::TextSelectableByMouse);
            demoBox->setVisible(false);
            demoBox->setStyleSheet(QStringLiteral(
                "color:#cfcfcf;background:#242424;border:1px solid #3a3a3a;"
                "border-radius:6px;padding:8px;"));

            connect(toggle, &QPushButton::clicked, this, [this, toggle, demoBox, id, demo]() {
                if (!demoBox->isVisible()) {
                    demoBox->setVisible(true);
                    toggle->setText(QStringLiteral("收起示例"));
                } else {
                    /* 第二次点击时顺手填入，省去手工复制 */
                    setEditorText(id, demo);
                    m_answers[id] = demo;
                    refreshPreview();
                    demoBox->setVisible(false);
                    toggle->setText(QStringLiteral("已填入示例（再点可展开）"));
                }
            });

            lay->addWidget(toggle);
            lay->addWidget(demoBox);
        }
    }
    lay->addStretch(1);

    QScrollArea *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setWidget(content);
    return scroll;
}

void KickStartWindow::onStepChanged(int row)
{
    if (row < 0 || row >= m_pages->count())
        return;
    m_pages->setCurrentIndex(row);
    m_btnPrev->setEnabled(row > 0);
    m_btnNext->setEnabled(row < m_pages->count() - 1);
}

void KickStartWindow::onPrev()
{
    m_steps->setCurrentRow(m_steps->currentRow() - 1);
}

void KickStartWindow::onNext()
{
    m_steps->setCurrentRow(m_steps->currentRow() + 1);
}

QString KickStartWindow::docText() const
{
    return renderMarkdown(m_answers.value(QStringLiteral("name")), m_answers);
}

QString KickStartWindow::answer(const QString &id) const
{
    return m_answers.value(id);
}

void KickStartWindow::refreshPreview()
{
    m_preview->setMarkdown(docText());
}

void KickStartWindow::onCopy()
{
    QApplication::clipboard()->setText(docText());
}

void KickStartWindow::onExport()
{
    QString name = m_answers.value(QStringLiteral("name")).trimmed();
    if (name.isEmpty())
        name = QStringLiteral("设计文档骨架");

    /* 默认目录：优先上次导出过的地方；否则落在仓库工程目录（EnvMonitor）旁，
       省得每次都要从"我的文档"一层层翻。 */
    Settings st;
    QString dir = st.value(QStringLiteral("kickstart/exportDir")).toString();
    if (dir.isEmpty() || !QDir(dir).exists())
        dir = QDir::currentPath();
    const QString def = QDir(dir).filePath(name + QStringLiteral(".md"));

    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出设计文档"), def,
        QStringLiteral("Markdown (*.md)"));
    if (path.isEmpty())
        return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"),
                             QStringLiteral("无法写入：%1").arg(path));
        return;
    }
    f.write(docText().toUtf8());
    f.close();
    st.setValue(QStringLiteral("kickstart/exportDir"), QFileInfo(path).absolutePath());
    QMessageBox::information(this, QStringLiteral("已导出"), path);
}

void KickStartWindow::setEditorText(const QString &id, const QString &text)
{
    QWidget *w = m_editors.value(id);
    if (!w)
        return;
    QSignalBlocker block(w);
    if (auto *le = qobject_cast<QLineEdit *>(w))
        le->setText(text);
    else if (auto *pe = qobject_cast<QPlainTextEdit *>(w))
        pe->setPlainText(text);
}

void KickStartWindow::onSave()
{
    QString name = m_answers.value(QStringLiteral("name")).trimmed();
    if (name.isEmpty())
        name = QStringLiteral("起手草稿");
    Settings st;
    QString dir = st.value(QStringLiteral("kickstart/draftDir")).toString();
    if (dir.isEmpty() || !QDir(dir).exists())
        dir = QDir::currentPath();
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存草稿"),
        QDir(dir).filePath(name + QStringLiteral(".kick.json")),
        QStringLiteral("JSON (*.json)"));
    if (path.isEmpty())
        return;
    st.setValue(QStringLiteral("kickstart/draftDir"), QFileInfo(path).absolutePath());

    QJsonObject answers;
    for (auto it = m_answers.constBegin(); it != m_answers.constEnd(); ++it)
        answers.insert(it.key(), it.value());
    QJsonObject root;
    root.insert(QStringLiteral("name"), m_answers.value(QStringLiteral("name")));
    root.insert(QStringLiteral("answers"), answers);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"),
                             QStringLiteral("无法写入：%1").arg(path));
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    QMessageBox::information(this, QStringLiteral("已保存"), path);
}

void KickStartWindow::onLoad()
{
    Settings st;
    QString dir = st.value(QStringLiteral("kickstart/draftDir")).toString();
    if (dir.isEmpty() || !QDir(dir).exists())
        dir = QDir::currentPath();
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("载入草稿"), dir,
        QStringLiteral("JSON (*.json)"));
    if (path.isEmpty())
        return;
    st.setValue(QStringLiteral("kickstart/draftDir"), QFileInfo(path).absolutePath());

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("载入失败"),
                             QStringLiteral("无法读取：%1").arg(path));
        return;
    }
    const QByteArray raw = f.readAll();
    f.close();

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    if (err.error != QJsonParseError::NoError) {
        QMessageBox::warning(this, QStringLiteral("载入失败"), err.errorString());
        return;
    }

    const QJsonObject rootObj = doc.object();
    m_answers.clear();
    const QJsonObject answers = rootObj.value(QStringLiteral("answers")).toObject();
    for (auto it = answers.constBegin(); it != answers.constEnd(); ++it)
        m_answers.insert(it.key(), it.value().toString());
    m_answers.insert(QStringLiteral("name"), rootObj.value(QStringLiteral("name")).toString());

    for (const PageSpec &p : m_specs)
        for (const FieldSpec &fl : p.fields)
            setEditorText(fl.id, m_answers.value(fl.id));

    refreshPreview();
    m_steps->setCurrentRow(0);
}
