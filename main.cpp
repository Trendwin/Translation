#include "widget.h"

#include <QApplication>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    // 固定草稿的用户数据目录，不随编译套件、exe输出目录或启动路径变化。
    QCoreApplication::setOrganizationName(QStringLiteral("Trendwin"));
    QCoreApplication::setApplicationName(QStringLiteral("Translation"));
    // 标准编辑菜单（复制、粘贴等）也加载内置中文翻译，不依赖发布机器安装 Qt。
    QTranslator chinese;
    if (chinese.load(QStringLiteral(":/translations/qt_zh_CN.qm")))
        a.installTranslator(&chinese);
    Widget w;
    w.show();
    return a.exec();
}
