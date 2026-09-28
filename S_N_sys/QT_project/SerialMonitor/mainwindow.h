#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSerialPort>        // 引入串口类
#include <QSerialPortInfo>    // 引入串口信息类
#include "qcustomplot.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    // 声明“打开串口”按钮的槽函数
    void on_btnOpenSerial_clicked();
    // 声明发送按钮的槽函数
    void on_btnSend_clicked();
    //声明清空按钮的槽函数
    void on_btnClearRecv_clicked();
    // 声明串口接收数据的槽函数
    void onSerialReadyRead();

private:
    Ui::MainWindow *ui;
    QSerialPort *serial;      // 定义串口对象
    bool isOpen;              // 记录串口状态
    QByteArray m_serialBuffer;

    //图表
    QVector<double> m_xData;
    QVector<double> m_tempData;
    QVector<double> m_humiData;
    int m_timeCounter;
};
#endif // MAINWINDOW_H