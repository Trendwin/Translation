#include "widget.h"

#include <QApplication>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    // 标准编辑菜单（复制、粘贴等）也加载内置中文翻译，不依赖发布机器安装 Qt。
    QTranslator chinese;
    if (chinese.load(QStringLiteral(":/translations/qt_zh_CN.qm")))
        a.installTranslator(&chinese);
    Widget w;
    w.show();
    return a.exec();
}
