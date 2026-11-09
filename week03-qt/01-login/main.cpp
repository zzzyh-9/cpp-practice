#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QWidget>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QWidget window;
    window.setWindowTitle(QStringLiteral("登录校验"));
    // 按课件要求设置窗口大小和各控件左上角的坐标。
    window.setFixedSize(300, 100);

    // 指定父窗口后，控件会显示在该窗口中，并随窗口一起销毁。
    auto *accountLabel = new QLabel(QStringLiteral("账户："), &window);
    accountLabel->setGeometry(20, 30, 40, 20);

    auto *accountEdit = new QLineEdit(&window);
    accountEdit->setGeometry(60, 20, 210, 22);
    accountEdit->setPlaceholderText(QStringLiteral("请输入账户"));

    auto *passwordLabel = new QLabel(QStringLiteral("密码："), &window);
    passwordLabel->setGeometry(20, 53, 40, 20);

    auto *passwordEdit = new QLineEdit(&window);
    passwordEdit->setGeometry(60, 50, 210, 22);
    passwordEdit->setPlaceholderText(QStringLiteral("请输入密码"));
    // Password 只隐藏输入内容的显示，不会加密保存的字符串。
    passwordEdit->setEchoMode(QLineEdit::Password);

    auto *loginButton = new QPushButton(QStringLiteral("登录"), &window);
    loginButton->setGeometry(60, 76, 80, 22);
    QObject::connect(loginButton, &QPushButton::clicked, &window,
                     [&window, accountEdit, passwordEdit]() {
        // 点击时读取输入框当前的内容，账号和密码必须同时匹配。
        const QString account = accountEdit->text();
        const QString password = passwordEdit->text();

        if (account == QStringLiteral("demo") && password == QStringLiteral("demo123")) {
            QMessageBox::information(&window, QStringLiteral("登录结果"),
                                     QStringLiteral("登录成功"));
        } else {
            QMessageBox::warning(&window, QStringLiteral("登录结果"),
                                 QStringLiteral("登录失败"));
        }
    });

    auto *exitButton = new QPushButton(QStringLiteral("退出"), &window);
    exitButton->setGeometry(190, 76, 80, 22);
    QObject::connect(exitButton, &QPushButton::clicked, &window, &QWidget::close);

    accountEdit->setFocus();
    window.show();
    return app.exec();
}
