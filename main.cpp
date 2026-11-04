#include "widget.h"
#include "src/transport/virtualcomprovider.h"

#include <QApplication>
#include <QTranslator>

int main(int argc, char *argv[])
{
    if (argc >= 2 && QByteArray(argv[1]) == "--setup-virtual-com") {
        QCoreApplication helper(argc, argv);
        if (helper.arguments().size() != 3) return 2;
        return VirtualComProvider::runSetup(helper.arguments().at(2));
    }
    QApplication a(argc, argv);
    // 配置由 DraftStore 保存在程序旁的 config/draft.json。
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
