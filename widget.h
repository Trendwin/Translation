#ifndef WIDGET_H
#define WIDGET_H
#include <QWidget>
#include <QMap>
#include <QQueue>
#include <memory>
#include "src/config/protocolconfig.h"
#include "src/core/conversionengine.h"

QT_BEGIN_NAMESPACE
namespace Ui { class Widget; }
QT_END_NAMESPACE
class QComboBox;
class QCheckBox;
class QLineEdit;
class QTextEdit;
class QTableWidget;
class QPushButton;
class QushengProtocol;
class SerialTransport;
class RequestManager;
class TranslationService;

class Widget : public QWidget
{
    Q_OBJECT
public:
    explicit Widget(QWidget *parent=nullptr);
    ~Widget() override;
private:
    struct Origin { ProtocolConfig config; bool upstream=false; quint64 upstreamGeneration=0; QString policy; };
    void appendLog(const QString &text);
    void refreshPorts();
    void updateConnectionUi(bool connected,const QString &reason);
    void updateSendEnabled();
    void preview();
    void simulateReply();
    void enableConfig();
    void refreshEditors();
    void applyFieldTable();
    void applyMappingTable();
    void submitInput(const QByteArray &bytes,bool upstream);
    QByteArray inputBytes() const;
    QByteArray buildResponse(const UnifiedResult &result,const Origin &origin) const;
    void onCompleted(const UnifiedResult &result);
    void queueUpstreamReply(const QByteArray &bytes);
    static QString stateText(RequestState state);
    Ui::Widget *ui;
    QushengProtocol *m_protocol;
    SerialTransport *m_transport,*m_upstreamTransport;
    RequestManager *m_requests;
    TranslationService *m_service;
    QComboBox *m_upstreamPort,*m_upstreamBaud,*m_inputEncoding;
    QCheckBox *m_bridge;
    QLineEdit *m_input,*m_simulatedReply;
    QTextEdit *m_configEdit,*m_previewEdit;
    QTableWidget *m_fieldTable,*m_mappingTable;
    QPushButton *m_upstreamConnect,*m_previewButton,*m_enableButton;
    std::unique_ptr<ConversionEngine> m_upstreamEngine;
    ProtocolConfig m_activeConfig;
    QMap<quint64,Origin> m_origins;
    QQueue<QByteArray> m_upstreamReplies;
    quint64 m_upstreamGeneration=0,m_lastRequestId=0,m_nextUpstreamWriteId=1;
    bool m_configEnabled=false,m_requestActive=false,m_upstreamWriting=false;
};
#endif
