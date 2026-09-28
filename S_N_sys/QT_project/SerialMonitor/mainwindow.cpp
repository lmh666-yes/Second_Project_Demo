#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QDateTime>

// 构造函数
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // 【格外功能】：设置左侧面板宽度，让波形图占据主视野
    ui->splitter->setSizes(QList<int>() << 150 << 850);

    // 串口初始化
    // 基础部分：初始化串口对象，绑定对象树自动释放内存
    serial = new QSerialPort(this);
    isOpen = false; // 初始状态标记为未连接

    // 【基础部分】：自动扫描电脑上的可用串口，添加到下拉框
    foreach (const QSerialPortInfo &info, QSerialPortInfo::availablePorts()) {
        QString displayText = info.portName() + " (" + info.description() + ")";
        // currentData()存纯COM号，显示文字存 displayText，分开管理
        ui->comboPort->addItem(displayText, info.portName());
    }

    // 【基础部分】：预置波特率选项，默认115200
    ui->comboBaud->addItem("115200");
    ui->comboBaud->addItem("9600");
    ui->comboBaud->addItem("38400");
    ui->comboBaud->setCurrentIndex(0);

    ui->labelStatus->setText("状态：未连接");

    // 【基础部分】：核心信号与槽绑定，收到数据自动触发回调
    connect(serial, &QSerialPort::readyRead, this, &MainWindow::onSerialReadyRead);

    //图表初始化（QCustomPlot）
    // 【格外功能】：初始化波形图，添加温度（红线）和湿度（蓝线）
    ui->widgetPlot->addGraph();
    ui->widgetPlot->graph(0)->setName("温度 (℃)");
    ui->widgetPlot->graph(0)->setPen(QPen(Qt::red));

    ui->widgetPlot->addGraph();
    ui->widgetPlot->graph(1)->setName("湿度(%)");
    ui->widgetPlot->graph(1)->setPen(QPen(Qt::blue));

    ui->widgetPlot->xAxis->setLabel("时间");
    ui->widgetPlot->yAxis->setLabel("数据");
    ui->widgetPlot->legend->setVisible(true);

    // 【格外功能】：允许鼠标拖拽和滚轮缩放
    ui->widgetPlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);

    m_timeCounter = 0;  // 时间轴计数器初始化
}

// 析构函数：释放UI内存
MainWindow::~MainWindow()
{
    delete ui;
}

// 模块：打开/关闭串口
void MainWindow::on_btnOpenSerial_clicked() {
    if(!isOpen) {
        // 打开串口
        // 【基础部分】：防呆设计，未选串口时直接返回
        if(ui->comboPort->currentText().isEmpty()) {
            ui->labelStatus->setText("状态：未找到串口");
            return;
        }

        // 【基础部分】：配置串口参数
        serial->setPortName(ui->comboPort->currentData().toString()); // 读取隐藏的纯COM号
        serial->setBaudRate(ui->comboBaud->currentText().toInt());
        serial->setDataBits(QSerialPort::Data8);
        serial->setStopBits(QSerialPort::OneStop);
        serial->setParity(QSerialPort::NoParity);
        serial->setFlowControl(QSerialPort::NoFlowControl);

        if(serial->open(QIODevice::ReadWrite)) {
            isOpen = true;
            ui->btnOpenSerial->setText("关闭串口");
            ui->labelStatus->setText("状态：已连接 " + ui->comboPort->currentText());

            // 【基础部分】：连接状态下禁用下拉框，防止误触
            ui->comboPort->setEnabled(false);
            ui->comboBaud->setEnabled(false);
        } else {
            ui->labelStatus->setText("状态：打开失败");
        }
    } else {
        //关闭串口
        serial->close();
        isOpen = false;
        ui->btnOpenSerial->setText("打开串口");
        ui->labelStatus->setText("状态：未连接");
        ui->comboPort->setEnabled(true);
        ui->comboBaud->setEnabled(true);
    }
}

// 模块：发送数据
void MainWindow::on_btnSend_clicked() {
    if(!isOpen) {
        ui->labelStatus->setText("状态：请先打开串口");
        return;
    }

    QString sendText = ui->editSend->text();
    if(sendText.isEmpty()) return;

    // 【基础部分】：十六进制与普通文本发送分支
    if(ui->checkHexSend->isChecked()) {
        QByteArray sendData = QByteArray::fromHex(sendText.toUtf8());
        serial->write(sendData);
    } else {
        // 【出现问题的解决部分】：文本模式发送必须补上 \r\n，否则下位机命令行解析器会一直等
        QString tempStr = sendText + "\r\n";
        serial->write(tempStr.toUtf8());
    }

    // 【格外功能】：发送回显 + 时间戳
    QString timeStr;
    if(ui->checkShowTimestamp->isCheckable()) {
        timeStr = "[" + QDateTime::currentDateTime().toString("hh:mm:ss.zzz") + "]";
    }
    QString displayStr = timeStr + "[发送]" + sendText;
    ui->textRecv->appendPlainText(displayStr);
    ui->textRecv->moveCursor(QTextCursor::End);
}

// 模块：清空接收区
void MainWindow::on_btnClearRecv_clicked() {
    ui->textRecv->clear();
}

// 模块：串口接收数据回调
void MainWindow::onSerialReadyRead() {
    // 【出现问题的解决部分】：串口粘包/分包问题的根本解法——引入缓冲区
    // 串口数据到达时间不定，不能依赖单次 readAll 读到完整一行
    m_serialBuffer.append(serial->readAll());

    // 【出现问题的解决部分】：循环查找完整的换行符 "\r\n"，按行切割
    while (m_serialBuffer.contains("\r\n")) {
        int endIndex = m_serialBuffer.indexOf("\r\n");
        QByteArray completeLine = m_serialBuffer.left(endIndex + 2);
        m_serialBuffer.remove(0, endIndex + 2); // 从缓冲区移除已读取部分

        QString str = QString::fromUtf8(completeLine).trimmed(); // 去掉首尾换行符
        if (str.isEmpty()) continue;

        // 【基础部分】：显示接收到的数据（支持十六进制和文本双模式）
        if(ui->checkHexRecv->isChecked()) {
            ui->textRecv->appendPlainText(completeLine.toHex(' ').toUpper());
        } else {
            ui->textRecv->appendPlainText(str);
        }

        // 【格外功能】：接收时间戳
        QString timeStr;
        if(ui->checkShowTimestamp->isCheckable()) {
            timeStr = "[" + QDateTime::currentDateTime().toString("hh:mm:ss.zzz") + "]";
        }
        QString displayStr = timeStr + "[接收]" + str;
        ui->textRecv->appendPlainText(displayStr);

        // 核心协议解析
        // 【基础部分】：解析温度数据
        int tempIndex = str.indexOf("TEMP:");
        if (tempIndex != -1) {
            int commaIndex = str.indexOf(",", tempIndex);
            if(commaIndex != -1) {
                QString tempValue = str.mid(tempIndex + 5, commaIndex - tempIndex - 5);
                ui->labelTemp->setText("温度：" + tempValue + " ℃");

                // 【格外功能】：追加温度数据到图表
                m_timeCounter++;
                m_xData.append(m_timeCounter);
                m_tempData.append(tempValue.toDouble());

                // 【出现问题的解决部分】：滑动窗口机制，防止内存无限增长
                if(m_xData.size() > 100) {
                    m_xData.removeFirst();
                    m_tempData.removeFirst();
                    m_humiData.removeFirst(); // 同步移除对应的湿度数据
                }
            }
        }

        // 【基础部分】：解析湿度数据
        int humiIndex = str.indexOf("HUMI:");
        if (humiIndex != -1) {
            int endIndex = str.indexOf("\r\n", humiIndex);
            if (endIndex == -1) endIndex = str.length();
            QString humiValue = str.mid(humiIndex + 5, endIndex - humiIndex - 5);
            ui->labelHumi->setText("湿度：" + humiValue + " %");

            // 【格外功能】：追加湿度数据到图表
            m_humiData.append(humiValue.toDouble());

            // 【格外功能】：更新图表并重新绘制
            ui->widgetPlot->graph(0)->setData(m_xData, m_tempData);
            ui->widgetPlot->graph(1)->setData(m_xData, m_humiData);
            ui->widgetPlot->xAxis->setRange(m_xData.first(), m_xData.last());
            ui->widgetPlot->yAxis->setRange(0, 100);
            ui->widgetPlot->replot();
        }
    }

    // 【出现问题的解决部分】：缓冲区溢出保护，防止下位机发乱码且无 \r\n 时内存爆满
    if (m_serialBuffer.length() > 1024) {
        m_serialBuffer.clear();
    }
}









