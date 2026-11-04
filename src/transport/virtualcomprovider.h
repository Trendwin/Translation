#ifndef VIRTUALCOMPROVIDER_H
#define VIRTUALCOMPROVIDER_H

#include <QByteArray>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QString>
#include <memory>

class QLockFile;
class QTimer;

class VirtualComProvider : public QObject
{
    Q_OBJECT
public:
    explicit VirtualComProvider(QObject *parent = nullptr, const QString &statePath = QString());
    ~VirtualComProvider() override;
    void refresh();
    void initialize();
    bool isBusy() const { return m_busy; }
    QString publicPort() const { return m_publicPort; }
    QString internalPort() const { return m_internalPort; }
    QString statePath() const { return m_statePath; }
    static int runSetup(const QString &statePath);

signals:
    void ready(const QString &publicPort, const QString &internalPort);
    void statusChanged(const QString &message);
    void needsInitialization(bool needed);
    void busyChanged(bool busy);

private:
    friend class FrameworkTest;
    struct Pair {
        QString publicPort;
        QString internalPort;
        bool healthy = false;
    };
    struct Snapshot {
        QString machineId;
        QMap<int, Pair> pairs;
        QSet<QString> occupiedPorts;
        QByteArray comDatabase;
    };
    static Snapshot inspectSystem();
    static bool validState(const QJsonObject &state, QString *reason);
    static QString selectPublicPort(const Snapshot &snapshot);
    static bool publicPortAvailable(const Snapshot &snapshot, const QString &name);
    static int matchingPair(const QJsonObject &state, const Snapshot &snapshot);
    static QJsonObject newState(const Snapshot &snapshot);
    static bool readState(const QString &path, QJsonObject *state, QString *reason);
    static bool saveState(const QString &path, const QJsonObject &state, QString *reason);
    static bool installPair(const QString &setup, const QString &statePath, QJsonObject *state, QString *reason);
    static QStringList pairArguments(const QJsonObject &state);
    void pollSetup();
    void finishSetup(const QString &failure = QString());

    QString m_statePath;
    QString m_publicPort;
    QString m_internalPort;
    bool m_busy = false;
    void *m_process = nullptr;
    QTimer *m_pollTimer = nullptr;
    std::unique_ptr<QLockFile> m_lock;
};

#endif
