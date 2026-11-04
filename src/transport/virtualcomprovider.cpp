#include "virtualcomprovider.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSerialPortInfo>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QVector>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#include <wintrust.h>
#include <softpub.h>
#endif

namespace {
const QString enumRoot = QStringLiteral("HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Enum\\com0com\\port");
const QString parametersRoot = QStringLiteral("HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Services\\com0com\\Parameters");

QString defaultStatePath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath("config/upstream-port.json");
}

bool failure(QString *reason, const QString &text)
{
    if (reason) *reason = text;
    return false;
}

#ifdef Q_OS_WIN
QSet<QString> activeSerialNames()
{
    QSet<QString> names;
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DEVICEMAP\\SERIALCOMM", 0,
                      KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return names;
    DWORD valueCount = 0, maximumNameLength = 0, maximumDataBytes = 0;
    if (RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                         &valueCount, &maximumNameLength, &maximumDataBytes, nullptr, nullptr) == ERROR_SUCCESS) {
        QVector<wchar_t> valueName(int(maximumNameLength) + 1);
        QVector<wchar_t> data(int(maximumDataBytes / sizeof(wchar_t)) + 2);
        for (DWORD index = 0; index < valueCount; ++index) {
            DWORD nameLength = DWORD(valueName.size());
            DWORD dataBytes = DWORD(data.size() * sizeof(wchar_t));
            DWORD type = 0;
            if (RegEnumValueW(key, index, valueName.data(), &nameLength, nullptr, &type,
                              reinterpret_cast<LPBYTE>(data.data()), &dataBytes) != ERROR_SUCCESS
                    || type != REG_SZ || dataBytes % sizeof(wchar_t) != 0) continue;
            QString name = QString::fromWCharArray(data.constData(), int(dataBytes / sizeof(wchar_t)));
            const int terminator = name.indexOf(QChar(0));
            if (terminator >= 0) name.truncate(terminator);
            if (!name.isEmpty()) names.insert(name.toUpper());
        }
    }
    RegCloseKey(key);
    return names;
}

bool trustedExecutable(const QString &path)
{
    const QString native = QDir::toNativeSeparators(path);
    WINTRUST_FILE_INFO file = {};
    file.cbStruct = sizeof(file);
    file.pcwszFilePath = reinterpret_cast<LPCWSTR>(native.utf16());
    WINTRUST_DATA data = {};
    data.cbStruct = sizeof(data);
    data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_NONE;
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &file;
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG result = WinVerifyTrust(nullptr, &action, &data);
    data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &action, &data);
    return result == ERROR_SUCCESS;
}

QString installedSetup()
{
    const QString key = QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\com0com");
    for (auto format : {QSettings::Registry64Format, QSettings::Registry32Format}) {
        QSettings registry(key, format);
        const QString location = registry.value("InstallLocation").toString();
        if (location.isEmpty() || !QDir::isAbsolutePath(location)) continue;
        const QString path = QDir(location).filePath("setupc.exe");
        if (QFileInfo::exists(path) && trustedExecutable(path)) return path;
    }
    return QString();
}

bool runProcess(const QString &program, const QStringList &arguments, const QString &directory,
                QString *reason, const QProcessEnvironment &environment = QProcessEnvironment::systemEnvironment(),
                bool *started = nullptr, QByteArray *output = nullptr)
{
    if (started) *started = false;
    if (output) output->clear();
    QProcess process;
    process.setWorkingDirectory(directory);
    process.setProcessEnvironment(environment);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(program, arguments);
    if (!process.waitForStarted(10000)) return failure(reason, QString::fromUtf8(u8"无法启动驱动配置程序：%1").arg(process.errorString()));
    if (started) *started = true;
    process.closeWriteChannel();
    // This code runs in the elevated helper, never in the UI event loop. Do not
    // terminate a driver installer while Windows may still be binding devices.
    process.waitForFinished(-1);
    const QByteArray diagnostics = process.readAll();
    if (output) *output = diagnostics;
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return failure(reason, QString::fromUtf8(u8"驱动配置程序未成功完成，退出码 %1。%2")
                       .arg(process.exitCode()).arg(QString::fromLocal8Bit(diagnostics).right(1200)));
    return true;
}

bool ensureDriver(QString *setup, QString *reason)
{
    *setup = installedSetup();
    if (!setup->isEmpty()) return true;
    SYSTEM_INFO info = {};
    GetNativeSystemInfo(&info);
    QString resource, expected;
    if (info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) {
        resource = QStringLiteral(":/virtualcom/com0com-x64.exe");
        expected = QStringLiteral("26486b28604b49a9008c54feb11b9ece0008a8287ee5caf0bcf2a62f4317128f");
    } else if (info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL) {
        resource = QStringLiteral(":/virtualcom/com0com-x86.exe");
        expected = QStringLiteral("de917425e022839f6374b509e3d34f3f9e9773915a44996194442337eadf4a5a");
    } else return failure(reason, QString::fromUtf8(u8"此驱动安装包支持 x86 和 x64 Windows，当前系统架构不受支持。"));
    QFile embedded(resource);
    if (!embedded.open(QIODevice::ReadOnly)) return failure(reason, QString::fromUtf8(u8"程序未包含所需的虚拟串口驱动安装包。"));
    const QByteArray bytes = embedded.readAll();
    if (QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() != expected.toLatin1())
        return failure(reason, QString::fromUtf8(u8"驱动安装包校验失败。"));
    QTemporaryDir temporary;
    if (!temporary.isValid()) return failure(reason, QString::fromUtf8(u8"无法创建驱动安装临时目录。"));
    const QString installer = temporary.filePath("com0com-setup.exe");
    QFile file(installer);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        return failure(reason, QString::fromUtf8(u8"无法写入驱动安装临时文件。"));
    file.close();
    if (!trustedExecutable(installer)) return failure(reason, QString::fromUtf8(u8"驱动安装包的 Windows 签名验证失败。"));
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("CNC_INSTALL_CNCA0_CNCB0_PORTS", "NO");
    environment.insert("CNC_INSTALL_COMX_COMX_PORTS", "NO");
    environment.insert("CNC_INSTALL_START_MENU_SHORTCUTS", "NO");
    if (!runProcess(installer, {"/S"}, temporary.path(), reason, environment)) return false;
    *setup = installedSetup();
    return !setup->isEmpty() || failure(reason, QString::fromUtf8(u8"驱动安装后仍未找到可信的配置程序，请重启 Windows 后重试。"));
}
#endif
}

VirtualComProvider::VirtualComProvider(QObject *parent, const QString &statePath)
    : QObject(parent), m_statePath(statePath.isEmpty() ? defaultStatePath() : QFileInfo(statePath).absoluteFilePath()),
      m_pollTimer(new QTimer(this))
{
    m_pollTimer->setInterval(250);
    connect(m_pollTimer, &QTimer::timeout, this, &VirtualComProvider::pollSetup);
}

VirtualComProvider::~VirtualComProvider()
{
    m_pollTimer->stop();
#ifdef Q_OS_WIN
    if (m_process) CloseHandle(static_cast<HANDLE>(m_process));
#endif
}

VirtualComProvider::Snapshot VirtualComProvider::inspectSystem()
{
    Snapshot snapshot;
#ifdef Q_OS_WIN
    QSettings machine(QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Cryptography"), QSettings::Registry64Format);
    snapshot.machineId = machine.value("MachineGuid").toString();
    if (snapshot.machineId.isEmpty()) {
        QSettings fallback(QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Cryptography"), QSettings::NativeFormat);
        snapshot.machineId = fallback.value("MachineGuid").toString();
    }
    const auto activeNames = activeSerialNames();
    snapshot.occupiedPorts = activeNames;
    for (const auto &port : QSerialPortInfo::availablePorts()) snapshot.occupiedPorts.insert(port.portName().toUpper());
    QSettings database(QStringLiteral("HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\COM Name Arbiter"), QSettings::NativeFormat);
    snapshot.comDatabase = database.value("ComDB").toByteArray();
    QSettings reserved(QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Ports"), QSettings::NativeFormat);
    for (QString name : reserved.allKeys()) {
        if (name.endsWith(':')) name.chop(1);
        snapshot.occupiedPorts.insert(name.toUpper());
    }
    const QRegularExpression sidePattern(QStringLiteral("^CNC([AB])([0-9]+)$"));
    QSettings parameters(parametersRoot, QSettings::NativeFormat);
    for (const QString &side : parameters.childGroups()) {
        const auto match = sidePattern.match(side);
        if (!match.hasMatch()) continue;
        const int id = match.captured(2).toInt();
        const QString name = parameters.value(side + "/PortName").toString().toUpper();
        snapshot.occupiedPorts.insert(name);
        if (match.captured(1) == "A") snapshot.pairs[id].publicPort = name;
        else snapshot.pairs[id].internalPort = name;
    }
    QSettings bus(QStringLiteral("HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Enum\\ROOT\\COM0COM"), QSettings::NativeFormat);
    for (const QString &device : bus.childGroups()) {
        bool ok = false;
        const int id = bus.value(device + "/Device Parameters/PortNum").toInt(&ok);
        if (ok && id >= 0) snapshot.pairs[id];
    }
    QMap<int, int> healthySides;
    QSettings ports(enumRoot, QSettings::NativeFormat);
    for (const QString &side : ports.childGroups()) {
        const auto match = sidePattern.match(side);
        if (!match.hasMatch()) continue;
        const int id = match.captured(2).toInt();
        const QString name = ports.value(side + "/Device Parameters/PortName").toString().toUpper();
        snapshot.occupiedPorts.insert(name);
        if (match.captured(1) == "A") snapshot.pairs[id].publicPort = name;
        else snapshot.pairs[id].internalPort = name;
        const bool healthy = !name.isEmpty() && activeNames.contains(name)
                && ports.value(side + "/ConfigFlags", 0).toUInt() == 0
                && ports.value(side + "/Service").toString().compare("com0com", Qt::CaseInsensitive) == 0
                && !ports.value(side + "/Driver").toString().isEmpty();
        if (healthy) healthySides[id] |= match.captured(1) == "A" ? 1 : 2;
    }
    for (auto it = snapshot.pairs.begin(); it != snapshot.pairs.end(); ++it)
        it.value().healthy = healthySides.value(it.key()) == 3;
#endif
    return snapshot;
}

bool VirtualComProvider::validState(const QJsonObject &state, QString *reason)
{
    const auto pairId = state.value("pairId");
    const QRegularExpression publicName(QStringLiteral("^COM([1-9][0-9]*)$"));
    const auto publicMatch = publicName.match(state.value("publicPort").toString());
    const QString status = state.value("status").toString();
    if (state.value("version").toDouble() != 1 || !state.value("machineId").isString()
            || state.value("machineId").toString().isEmpty() || !pairId.isDouble()
            || pairId.toDouble() != pairId.toInt(-1) || pairId.toInt() < 0 || pairId.toInt() > 9999
            || !publicMatch.hasMatch() || publicMatch.captured(1).toInt() < 10 || publicMatch.captured(1).toInt() > 255
            || !QRegularExpression(QStringLiteral("^TWV[0-9A-F]{8}$")).match(state.value("internalPort").toString()).hasMatch()
            || (status != "pending" && status != "ready") || !state.value("creationAttempted").isBool())
        return failure(reason, QString::fromUtf8(u8"专用上游端口配置损坏，请保留 upstream-port.json 并联系维护人员。"));
    return true;
}

QString VirtualComProvider::selectPublicPort(const Snapshot &snapshot)
{
    for (int number = 10; number <= 255; ++number) {
        const QString name = QStringLiteral("COM%1").arg(number);
        if (publicPortAvailable(snapshot, name)) return name;
    }
    return QString();
}

bool VirtualComProvider::publicPortAvailable(const Snapshot &snapshot, const QString &name)
{
    const int bit = name.mid(3).toInt() - 1;
    if (bit < 0) return false;
    const bool reserved = bit / 8 < snapshot.comDatabase.size()
            && (quint8(snapshot.comDatabase.at(bit / 8)) & (1u << (bit % 8)));
    return !reserved && !snapshot.occupiedPorts.contains(name);
}

int VirtualComProvider::matchingPair(const QJsonObject &state, const Snapshot &snapshot)
{
    if (state.value("machineId").toString() != snapshot.machineId) return -1;
    for (auto it = snapshot.pairs.begin(); it != snapshot.pairs.end(); ++it)
        if (it.value().publicPort == state.value("publicPort").toString()
                && it.value().internalPort == state.value("internalPort").toString()) return it.key();
    return -1;
}

QJsonObject VirtualComProvider::newState(const Snapshot &snapshot)
{
    int pairId = 0;
    while (snapshot.pairs.contains(pairId) && pairId <= 9999) ++pairId;
    QString internalPort;
    do { internalPort = "TWV" + QUuid::createUuid().toString(QUuid::Id128).left(8).toUpper(); }
    while (snapshot.occupiedPorts.contains(internalPort));
    return {{"version", 1}, {"machineId", snapshot.machineId}, {"pairId", pairId},
        {"publicPort", selectPublicPort(snapshot)},
        {"internalPort", internalPort},
        {"status", "pending"}, {"creationAttempted", false}};
}

bool VirtualComProvider::readState(const QString &path, QJsonObject *state, QString *reason)
{
    QFile file(path);
    if (!file.exists()) { *state = QJsonObject(); return true; }
    if (!file.open(QIODevice::ReadOnly)) return failure(reason, QString::fromUtf8(u8"无法读取专用上游端口配置：%1").arg(file.errorString()));
    if (file.size() > 65536) return failure(reason, QString::fromUtf8(u8"专用上游端口配置文件过大。"));
    const auto document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) return failure(reason, QString::fromUtf8(u8"专用上游端口配置不是有效 JSON。"));
    *state = document.object();
    return validState(*state, reason);
}

bool VirtualComProvider::saveState(const QString &path, const QJsonObject &state, QString *reason)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return failure(reason, QString::fromUtf8(u8"无法创建程序的 config 目录。"));
    QSaveFile file(path);
    const QByteArray bytes = QJsonDocument(state).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return failure(reason, QString::fromUtf8(u8"无法保存专用上游端口配置：%1").arg(file.errorString()));
    return true;
}

bool VirtualComProvider::installPair(const QString &setup, const QString &statePath,
                                    QJsonObject *state, QString *reason)
{
#ifdef Q_OS_WIN
    state->insert("creationAttempted", true);
    if (!saveState(statePath, *state, reason)) {
        state->insert("creationAttempted", false);
        return false;
    }
    const QString log = QFileInfo(statePath).dir().filePath("upstream-setup.log");
    bool started = false;
    QByteArray diagnostics;
    const bool successful = runProcess(setup, pairArguments(*state), QFileInfo(setup).absolutePath(), reason,
                                      QProcessEnvironment::systemEnvironment(), &started, &diagnostics);
    QSaveFile diagnosticFile(log);
    if (diagnosticFile.open(QIODevice::WriteOnly)) {
        diagnosticFile.write(diagnostics.left(65536));
        diagnosticFile.commit();
    }
    if (successful) return true;
    if (!started) {
        // No driver command ran, so a later explicit retry may safely try again.
        state->insert("creationAttempted", false);
        QString saveReason;
        if (!saveState(statePath, *state, &saveReason) && reason) *reason += "\n" + saveReason;
    }
    return false;
#else
    Q_UNUSED(setup)
    Q_UNUSED(statePath)
    Q_UNUSED(state)
    return failure(reason, QString::fromUtf8(u8"专用虚拟串口初始化需要 Windows。"));
#endif
}

QStringList VirtualComProvider::pairArguments(const QJsonObject &state)
{
    // setupc has a small legacy command parser. Keep the command short and avoid
    // path-valued options; Qt captures output separately, including paths with spaces.
    return {"--silent", "install", QString::number(state.value("pairId").toInt()),
        "PortName=" + state.value("publicPort").toString(), "PortName=" + state.value("internalPort").toString()};
}

void VirtualComProvider::refresh()
{
    if (m_busy) return;
    m_publicPort.clear(); m_internalPort.clear();
    QJsonObject state; QString reason;
    if (!readState(m_statePath, &state, &reason)) {
        emit needsInitialization(true); emit statusChanged(reason); return;
    }
    const auto snapshot = inspectSystem();
    const int pair = state.isEmpty() ? -1 : matchingPair(state, snapshot);
    if (pair >= 0 && snapshot.pairs.value(pair).healthy) {
        auto recovered = state;
        recovered.insert("pairId", pair);
        recovered.insert("status", "ready");
        recovered.remove("lastError");
        if (recovered != state) {
            QString ignored;
            saveState(m_statePath, recovered, &ignored);
        }
        m_publicPort = state.value("publicPort").toString();
        m_internalPort = state.value("internalPort").toString();
        emit needsInitialization(false);
        emit statusChanged(QString::fromUtf8(u8"专用上游端口已就绪，请在上游软件选择 %1。").arg(m_publicPort));
        emit ready(m_publicPort, m_internalPort);
        return;
    }
    emit needsInitialization(true);
    const QString previousError = state.value("lastError").toString();
    emit statusChanged(previousError.isEmpty() ? QString::fromUtf8(u8"首次使用请初始化专用上游端口。") : previousError);
}

void VirtualComProvider::initialize()
{
    if (m_busy) return;
    refresh();
    if (!m_publicPort.isEmpty()) return;
#ifdef Q_OS_WIN
    if (!QDir().mkpath(QFileInfo(m_statePath).absolutePath())) {
        emit statusChanged(QString::fromUtf8(u8"程序目录不可写，无法创建上游端口配置。")); return;
    }
    m_lock.reset(new QLockFile(m_statePath + ".initialize.lock"));
    if (!m_lock->tryLock(0)) {
        m_lock.reset(); emit statusChanged(QString::fromUtf8(u8"另一个程序实例正在初始化此上游端口，请稍后重试。")); return;
    }
    QJsonObject state; QString reason;
    const auto snapshot = inspectSystem();
    if (!readState(m_statePath, &state, &reason)) { m_lock.reset(); emit statusChanged(reason); return; }
    if (state.isEmpty() || state.value("machineId").toString() != snapshot.machineId) state = newState(snapshot);
    if (!validState(state, &reason) || !saveState(m_statePath, state, &reason)) {
        m_lock.reset(); emit statusChanged(reason); return;
    }
    const QString application = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    const QString parameters = QStringLiteral("--setup-virtual-com \"%1\"").arg(QDir::toNativeSeparators(m_statePath));
    const QString directory = QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
    SHELLEXECUTEINFOW request = {};
    request.cbSize = sizeof(request);
    request.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    request.lpVerb = L"runas";
    request.lpFile = reinterpret_cast<LPCWSTR>(application.utf16());
    request.lpParameters = reinterpret_cast<LPCWSTR>(parameters.utf16());
    request.lpDirectory = reinterpret_cast<LPCWSTR>(directory.utf16());
    request.nShow = SW_HIDE;
    m_busy = true; emit busyChanged(true);
    emit statusChanged(QString::fromUtf8(u8"正在初始化专用上游端口，请确认 Windows 管理员授权。"));
    if (!ShellExecuteExW(&request)) {
        const DWORD error = GetLastError();
        finishSetup(error == ERROR_CANCELLED ? QString::fromUtf8(u8"已取消管理员授权，可以再次点击初始化。")
                    : QString::fromUtf8(u8"无法启动上游端口初始化，Windows 错误 %1。").arg(error));
        return;
    }
    m_process = request.hProcess;
    if (!m_process) {
        finishSetup(QString::fromUtf8(u8"初始化程序未返回进程句柄，请稍后重试。"));
        return;
    }
    m_pollTimer->start();
#else
    emit statusChanged(QString::fromUtf8(u8"专用虚拟串口初始化需要 Windows。"));
#endif
}

void VirtualComProvider::pollSetup()
{
#ifdef Q_OS_WIN
    if (!m_process) return;
    DWORD code = 0;
    if (!GetExitCodeProcess(static_cast<HANDLE>(m_process), &code)) {
        finishSetup(QString::fromUtf8(u8"无法读取上游端口初始化结果，请重试。")); return;
    }
    if (code == STILL_ACTIVE) return;
    QString reason;
    if (code != 0) {
        QJsonObject state;
        readState(m_statePath, &state, &reason);
        reason = state.value("lastError").toString(reason);
        if (reason.isEmpty()) reason = QString::fromUtf8(u8"初始化未完成，请重试。退出码 %1。").arg(code);
    }
    finishSetup(reason);
#endif
}

void VirtualComProvider::finishSetup(const QString &failureReason)
{
    m_pollTimer->stop();
#ifdef Q_OS_WIN
    if (m_process) CloseHandle(static_cast<HANDLE>(m_process));
#endif
    m_process = nullptr;
    m_lock.reset();
    m_busy = false; emit busyChanged(false);
    if (!failureReason.isEmpty()) {
        emit needsInitialization(true); emit statusChanged(failureReason); return;
    }
    refresh();
}

int VirtualComProvider::runSetup(const QString &statePath)
{
#ifdef Q_OS_WIN
    QJsonObject state; QString reason;
    if (!readState(statePath, &state, &reason) || state.isEmpty()) return 2;
    const auto failSetup = [&](const QString &text) {
        state.insert("lastError", text);
        QString ignored; saveState(statePath, state, &ignored);
        return 1;
    };
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    PSID administrators = nullptr;
    BOOL elevated = FALSE;
    if (AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
                                 0, 0, 0, 0, 0, 0, &administrators)) {
        CheckTokenMembership(nullptr, administrators, &elevated);
        FreeSid(administrators);
    }
    if (!elevated) return failSetup(QString::fromUtf8(u8"初始化需要 Windows 管理员授权，请从软件中的初始化按钮重试。"));
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Global\\TrendwinTranslationVirtualComSetup");
    if (!mutex) return failSetup(QString::fromUtf8(u8"无法锁定上游端口初始化任务。"));
    const DWORD wait = WaitForSingleObject(mutex, 30000);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
        CloseHandle(mutex);
        return failSetup(QString::fromUtf8(u8"另一个端口初始化任务仍在运行，请稍后重试。"));
    }
    struct MutexGuard {
        HANDLE handle;
        ~MutexGuard() { ReleaseMutex(handle); CloseHandle(handle); }
    } guard{mutex};
    auto snapshot = inspectSystem();
    if (state.value("machineId").toString() != snapshot.machineId) state = newState(snapshot);
    int ownPair = matchingPair(state, snapshot);
    if (ownPair >= 0) {
        if (!snapshot.pairs.value(ownPair).healthy)
            return failSetup(QString::fromUtf8(u8"专用端口已创建，但 Windows 尚未使其就绪。请重启 Windows 后重试。"));
        state.insert("pairId", ownPair); state.insert("status", "ready"); state.remove("lastError");
        return saveState(statePath, state, &reason) ? 0 : 1;
    }
    if (state.value("creationAttempted").toBool())
        return failSetup(QString::fromUtf8(u8"上次创建的专用端口尚未完整就绪，已保留端口记录。请联系维护人员检查 upstream-port.json。"));
    if (snapshot.pairs.contains(state.value("pairId").toInt())
            || !publicPortAvailable(snapshot, state.value("publicPort").toString())) state = newState(snapshot);
    if (!validState(state, &reason) || !saveState(statePath, state, &reason)) return failSetup(reason);
    QString setup;
    if (!ensureDriver(&setup, &reason)) return failSetup(reason);
    // Recheck after installing the driver, because another installer may have reserved names.
    snapshot = inspectSystem();
    if (snapshot.pairs.contains(state.value("pairId").toInt())
            || !publicPortAvailable(snapshot, state.value("publicPort").toString())) state = newState(snapshot);
    if (!validState(state, &reason)) return failSetup(reason);
    if (!installPair(setup, statePath, &state, &reason)) return failSetup(reason);
    for (int attempt = 0; attempt < 30; ++attempt) {
        snapshot = inspectSystem();
        ownPair = matchingPair(state, snapshot);
        if (ownPair >= 0 && snapshot.pairs.value(ownPair).healthy) {
            state.insert("pairId", ownPair); state.insert("status", "ready"); state.remove("lastError");
            return saveState(statePath, state, &reason) ? 0 : 1;
        }
        QThread::msleep(500);
    }
    return failSetup(QString::fromUtf8(u8"驱动配置已运行，但专用端口仍未就绪，请重启 Windows 后重试。详情见 upstream-setup.log。"));
#else
    Q_UNUSED(statePath)
    return 1;
#endif
}
