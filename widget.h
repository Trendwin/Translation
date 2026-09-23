#ifndef WIDGET_H
#define WIDGET_H
#include <QWidget>
#include "src/model/translationtypes.h"

QT_BEGIN_NAMESPACE
namespace Ui { class Widget; }
QT_END_NAMESPACE
class DtClientProtocol;
class QushengProtocol;
class SerialTransport;
class RequestManager;
class TranslationService;

class Widget : public QWidget
{
    Q_OBJECT
public:
    explicit Widget(QWidget *parent = nullptr);
    ~Widget() override;
private:
    void appendLog(const QString &text);
    static QString stateText(RequestState state);
    Ui::Widget *ui;
    DtClientProtocol *m_client;
    QushengProtocol *m_protocol;
    SerialTransport *m_transport;
    RequestManager *m_requests;
    TranslationService *m_service;
    quint64 m_lastRequestId = 0;
};
#endif
