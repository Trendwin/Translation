#ifndef WIDGET_H
#define WIDGET_H

#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <QWidget>
#include <memory>

#include "src/config/protocolconfig.h"
#include "src/config/draftstore.h"
#include "src/core/conversionengine.h"

class QComboBox;
class QCheckBox;
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
    bool saveConfiguration();
    bool saveUiToDraft();
    bool collectUiDraft(QJsonObject *draft);
    bool storeUiDraft(const QJsonObject &draft);
    bool debugConfig(ProtocolConfig *config);
    bool draftSaved();
    void updateTableControls();
    void markDraftDirty();
    void activateSavedConfiguration();
    void importDraft();
    void exportDraft();
    bool applyAdvancedJson();
    void refreshPorts();
    void updateConnectionUi(bool connected, const QString &reason);
    void updateLiveState();
    void preview(bool live);
    void parseFormatPreview();
    bool requireSequentialFormat();
    QByteArray inputBytes(const ProtocolConfig &config, TranslationError *error) const;
    QString previewText(const ConversionPreview &preview) const;
    void appendLog(const QString &text);
    void addFieldRow(const QJsonObject &field = QJsonObject());
    void addMappingRow(const QJsonObject &mapping = QJsonObject());
    void resetMappingForm();
    bool readNewMapping(QJsonObject *mapping);
    bool readGroupMapping(QJsonObject *mapping);
    bool commitMappingStep();
    bool requireSavedMappingStep();
    void updateMappingStepState();
    void loadMappingStep(int row);
    void refreshMappingSteps();
    void selectMappingStep(int row);
    void updateMappingSummary(int row, const QJsonObject &mapping);
    void updateMappingSources(bool rebuildCounts = true);
    void updateMappingCounts();
    void updateListGuidance();
    void addNumericSources(QComboBox *box, const QJsonArray &omitted);
    QJsonArray mappingOmittedFields() const;
    bool legacyMappedInput() const;
    void fieldFormatChanged();
    bool synchronizeFieldNames();
    QString mappingFieldIssue(const QJsonObject &mapping) const;
    void refreshMappingSummaries();
    void updateMappingControls();
    void filterMappingCommands();
    void loadCatalogParameters(const QJsonObject &parameters);
    void updateValveEditor();
    void setValveConstant(const QString &key, quint32 value);
    void setValveStateSource();
    bool valveValue(const QString &key, quint32 *value, QString *origin) const;
    bool catalogParameters(QJsonObject *parameters, QString *reason) const;
    QString parameterDefaultText(const QJsonObject &definition, const QString &operation) const;
    static QString stateText(RequestState state);

    QushengProtocol *m_protocol = nullptr;
    SerialTransport *m_transport = nullptr;
    RequestManager *m_requests = nullptr;
    TranslationService *m_service = nullptr;

    DraftStore m_draftStore;

    QJsonObject m_draftRoot;
    ProtocolConfig m_activeConfig;
    bool m_formDirty = false;
    bool m_jsonDirty = false;
    bool m_loadingUi = false;
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
    QTableWidget *m_mappingSteps = nullptr;
    QPushButton *m_saveMappingStep = nullptr;
    QLabel *m_mappingStepState = nullptr;
    QLabel *m_mappingInputHint = nullptr;
    QComboBox *m_groupInputMode = nullptr;
    QJsonArray m_stepDrafts;
    QJsonObject m_groupInput;
    QJsonObject m_stepOriginal;
    int m_selectedStep = -1;
    bool m_stepDirty = false, m_loadingStep = false, m_switchingStep = false;
    QLineEdit *m_mappingCode = nullptr;
    QComboBox *m_mappingOperation = nullptr;
    QLineEdit *m_mappingSearch = nullptr;
    QTableWidget *m_mappingParameters = nullptr;
    QGroupBox *m_valveEditor = nullptr;
    QGroupBox *m_valveChecksGroup = nullptr;
    QComboBox *m_valveMode = nullptr;
    QComboBox *m_valveStateSource = nullptr;
    bool m_valveStatePending = false;
    bool m_updatingValveState = false;
    QComboBox *m_valveNumber = nullptr;
    QList<QCheckBox *> m_valveChecks;
    QLabel *m_valveSummary = nullptr;
    QLabel *m_valveActionHint = nullptr;
    QLabel *m_mappingHint = nullptr;
    QGroupBox *m_mappingCountsGroup = nullptr;
    QLabel *m_mappingListGuidance = nullptr;
    QTableWidget *m_mappingCounts = nullptr;
    QMap<QString, int> m_listCounts;
    QComboBox *m_mappingSource = nullptr;
    QComboBox *m_mappingMotor = nullptr;
    QLineEdit *m_mappingValue = nullptr;
    QLineEdit *m_mappingMultiplier = nullptr;
    bool m_loadingMapping = false;
    bool m_syncingFields = false;
    QString m_editorOperation;
    QTextEdit *m_advancedJson = nullptr;

    QSpinBox *m_destination = nullptr;
    QSpinBox *m_source = nullptr;
    QSpinBox *m_devId = nullptr;
    bool m_unresolvedMotorId = false;
    QSpinBox *m_action = nullptr;
    QSpinBox *m_speed = nullptr;
    QSpinBox *m_minPosition = nullptr;
    QSpinBox *m_maxPosition = nullptr;

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
