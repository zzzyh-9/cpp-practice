#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QWidget>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QWidget window;
    window.setWindowTitle(QStringLiteral("信号与槽：两个标签"));
    window.setFixedSize(320, 180);

    auto *label1 = new QLabel(QStringLiteral("label 1：等待点击"), &window);
    label1->setGeometry(20, 20, 280, 30);
    label1->setAlignment(Qt::AlignCenter);

    auto *label2 = new QLabel(QStringLiteral("label 2：等待点击"), &window);
    label2->setGeometry(20, 60, 280, 30);
    label2->setAlignment(Qt::AlignCenter);

    auto *button = new QPushButton(QStringLiteral("点击显示"), &window);
    button->setGeometry(100, 110, 120, 30);

    // 按钮发出 clicked 信号时，执行这个 lambda，同时更新两个标签。
    // 捕获 label1、label2 的指针，函数体内才能使用它们。
    // window 是连接的上下文对象，它销毁时 Qt 会自动断开连接。
    QObject::connect(button, &QPushButton::clicked, &window, [label1, label2]() {
        label1->setText(QStringLiteral("hello world"));
        label2->setText(QStringLiteral("hello world"));
    });

    window.show();
    return app.exec();
}
