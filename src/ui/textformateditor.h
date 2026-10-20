#ifndef TEXTFORMATEDITOR_H
#define TEXTFORMATEDITOR_H
#include "config/sampleformat.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QWidget>
class QLineEdit;
class QLabel;
class QTableWidget;
class QComboBox;
class QPushButton;
class QTextEdit;
class QVBoxLayout;
class TextFormatEditor : public QWidget
{
    Q_OBJECT
  public:
    explicit TextFormatEditor(QWidget *parent = nullptr);
    void setFormat(const QJsonObject &format);
    QJsonObject format() const;
    bool hasPendingSampleEdits() const
    {
        return m_pendingSampleEdits;
    }
    void discardSample();
    void setFrameBoundaries(const QString &header, const QString &footer);
  signals:
    void changed();

  private:
    void buildSampleUi(QVBoxLayout *layout);
    QJsonObject sampleSeparators() const;
    QJsonObject sampleExternal() const;
    void splitSample();
    void applySample();
    void parseSample();
    void refreshSampleRows(bool loadInspector = true);
    int selectedSamplePiece() const;
    void selectSamplePiece();
    void editSamplePiece();
    void invalidateSample();
    void refreshSampleSummary(bool restoreInputs = true);
    QLineEdit *m_sample;
    QLineEdit *m_regionSeparator;
    QLineEdit *m_fieldSeparator;
    QLineEdit *m_assignmentSeparator;
    QLineEdit *m_parameterSeparator;
    QComboBox *m_knownCommands;
    QTableWidget *m_sampleTable;
    QLabel *m_sampleStatus;
    QLabel *m_splitPreview;
    QPushButton *m_applySample;
    QTextEdit *m_sampleResult;
    QWidget *m_pieceEditor;
    QLineEdit *m_pieceName;
    QComboBox *m_pieceType;
    QComboBox *m_pieceRole;
    QLineEdit *m_pieceFixed;
    QLineEdit *m_pieceMin;
    QLineEdit *m_pieceMax;
    QString m_frameHeader, m_frameFooter;
    SampleFormatDraft m_sampleDraft;
    bool m_hasSampleDraft = false;
    bool m_pendingSampleEdits = false;
    bool m_loadingPiece = false;
    void addField(QTableWidget *table, const QJsonObject &field);
    QJsonArray readFields(QTableWidget *table) const;
    void storeParameters() const;
    void selectCommand();
    void notifyChanged();
    void updateParameterContext();
    QLabel *m_parameterContext;
    QLineEdit *m_template;
    QTableWidget *m_fields;
    QTableWidget *m_commands;
    QTableWidget *m_parameters;
    QJsonObject m_original;
    mutable int m_selected = -1;
    bool m_loading = false;
};
#endif
