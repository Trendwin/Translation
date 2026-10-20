#ifndef TEXTFORMATEDITOR_H
#define TEXTFORMATEDITOR_H
#include <QJsonArray>
#include <QJsonObject>
#include <QWidget>
class QLineEdit;
class QLabel;
class QTableWidget;
class TextFormatEditor : public QWidget
{
    Q_OBJECT
  public:
    explicit TextFormatEditor(QWidget *parent = nullptr);
    void setFormat(const QJsonObject &format);
    QJsonObject format() const;
  signals:
    void changed();

  private:
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
