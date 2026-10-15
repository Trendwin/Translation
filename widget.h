#ifndef WIDGET_H
#define WIDGET_H

#include <QJsonObject>
#include <QWidget>
#include <memory>

#include "src/config/protocolconfig.h"
#include "src/config/draftstore.h"
#include "src/core/conversionengine.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QGroupBox;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTextEdit;
class QushengProtocol;
class SerialTransport;
class RequestManager;
class TranslationService;

class Widget : public QWidget
{
    Q_OBJECT
public:
    explicit Widget(QWidget *parent = nullptr, const QString &draftFilePath = QString());
    ~Widget() override = default;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    void connectSignals();
    void loadInitialDraft();
    bool persistDraft(const QJsonObject &draft);
    void loadUiFromDraft();
    bool saveUiToDraft();
    bool draftSaved();
    void updateTableControls();
    void markDraftDirty();
    void validateDraft();
    void enableDraft();
    void importDraft();
    void exportDraft();
    bool applyAdvancedJson();
    void refreshPorts();
    void updateConnectionUi(bool connected, const QString &reason);
    void updateLiveState();
    void preview(bool live);
    QByteArray inputBytes(const ProtocolConfig &config, TranslationError *error) const;
    QString previewText(const ConversionPreview &preview) const;
    void appendLog(const QString &text);
    void addFieldRow(const QJsonObject &field = QJsonObject());
    void addMappingRow(const QJsonObject &mapping = QJsonObject());
    void loadMappingEditor(int row);
    bool saveMappingEditor();
    void updateMappingSummary(int row, const QJsonObject &mapping);
    void updateMappingSources();
    void rebuildMappingOmissions(const QJsonObject &input);
    void fieldFormatChanged();
    bool synchronizeFieldNames();
    QString mappingFieldIssue(const QJsonObject &mapping) const;
    void refreshMappingSummaries();
    void updateMappingControls();
    void mappingEdited();
    void filterMappingCommands();
    void loadCatalogParameters(const QJsonObject &parameters);
    bool catalogParameters(QJsonObject *parameters, QString *reason) const;
    QString parameterDefaultText(const QJsonObject &definition, const QString &operation) const;
    static QString stateText(RequestState state);

    QushengProtocol *m_protocol = nullptr;
    SerialTransport *m_transport = nullptr;
    RequestManager *m_requests = nullptr;
    TranslationService *m_service = nullptr;

    DraftStore m_draftStore;

    QJsonObject m_draftRoot;
    ProtocolConfig m_validatedDraft;
    ProtocolConfig m_activeConfig;
    bool m_draftValid = false;
    bool m_formDirty = false;
    bool m_jsonDirty = false;
    bool m_loadingUi = false;
    bool m_configEnabled = false;
    bool m_requestActive = false;
    quint64 m_lastRequestId = 0;

    QLabel *m_draftVersion = nullptr;
    QLabel *m_activeVersion = nullptr;
    QLabel *m_validationState = nullptr;
    QComboBox *m_encoding = nullptr;
    QLineEdit *m_header = nullptr;
    QLineEdit *m_footer = nullptr;
    QLabel *m_byteOrderLabel = nullptr;
    QComboBox *m_byteOrder = nullptr;
    QTableWidget *m_fieldTable = nullptr;
    QTableWidget *m_mappingTable = nullptr;
    QGroupBox *m_mappingEditor = nullptr;
    QLineEdit *m_mappingCode = nullptr;
    QComboBox *m_mappingOperation = nullptr;
    QLineEdit *m_mappingSearch = nullptr;
    QTableWidget *m_mappingParameters = nullptr;
    QComboBox *m_mappingSource = nullptr;
    QLineEdit *m_mappingValue = nullptr;
    QLineEdit *m_mappingMultiplier = nullptr;
    QGroupBox *m_mappingOmissions = nullptr;
    int m_mappingEditorRow = -1;
    bool m_loadingMapping = false;
    bool m_syncingFields = false;
    bool m_mappingEditorDirty = false;
    bool m_multiplierChanged = false;
    QString m_editorOperation;
    QTextEdit *m_advancedJson = nullptr;

    QSpinBox *m_destination = nullptr;
    QSpinBox *m_source = nullptr;
    QSpinBox *m_devId = nullptr;
    QSpinBox *m_action = nullptr;
    QSpinBox *m_speed = nullptr;
    QSpinBox *m_minPosition = nullptr;
    QSpinBox *m_maxPosition = nullptr;
    QCheckBox *m_deviceConfirmed = nullptr;

    QComboBox *m_port = nullptr;
    QComboBox *m_baud = nullptr;
    QPushButton *m_connectButton = nullptr;
    QLabel *m_connectionState = nullptr;
    QLabel *m_liveReason = nullptr;

    QLabel *m_inputEncoding = nullptr;
    QLineEdit *m_input = nullptr;
    QPushButton *m_previewButton = nullptr;
    QPushButton *m_sendButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QPushButton *m_queryButton = nullptr;
    QTextEdit *m_previewOutput = nullptr;
    QTextEdit *m_targetOutput = nullptr;
    QTextEdit *m_rawReply = nullptr;
    QTextEdit *m_replyResult = nullptr;
    QLabel *m_requestState = nullptr;
    QTextEdit *m_log = nullptr;
};

#endif
