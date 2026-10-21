#include "requestmanager.h"
#include "qusheng/qushengcommandcatalog.h"

// 生命周期：排队 → 本地写出 → 等待有效应答；运动收到应答即结束本地等待，不主动查询完成。
// m_current是业务请求；m_probe是其内部0x12查询，两者共用本地requestId但各有线上序号。
// m_preflight与m_probeActive仅用于旧相对运动的前置位置查询。
namespace {
bool isRelative(const UnifiedCommand &command)
{ return command.operation == "RELATIVE_FORWARD" || command.operation == "RELATIVE_BACKWARD"; }
bool isMotion(const UnifiedCommand &command)
{ return QushengCommandCatalog::isAbsoluteMotion(command.operation) || isRelative(command)
      || QushengCommandCatalog::isStopOrReset(command.operation) || command.operation == "QUSHENG_C1"; }
bool sameMotor(const UnifiedCommand &left, const UnifiedCommand &right)
{
    if (left.operation == "QUSHENG_C1" || right.operation == "QUSHENG_C1") return false;
    bool leftOk = false, rightOk = false;
    const uint leftId = left.targetDevice.toUInt(&leftOk), rightId = right.targetDevice.toUInt(&rightOk);
    return leftOk && rightOk && leftId == rightId;
}
}

RequestManager::RequestManager(IInternalProtocol *protocol,ITransport *transport,QObject *parent)
    :QObject(parent),m_protocol(protocol),m_transport(transport)
{
    // 仅保留写出与应答超时；运动和查询都不自动重发。
    for(QTimer *t:{&m_sendTimer,&m_ackTimer}) t->setSingleShot(true);
    connect(&m_sendTimer,&QTimer::timeout,this,[this]() {
        if(!m_active) return;
        if(m_preflight) { setBusy(true); failCurrent(RequestState::TimedOut,"POSITION_QUERY_TIMEOUT",QString::fromUtf8(u8"相对运动前的当前位置查询写出超时，未发送运动")); return; }
        if(isMotion(m_current)) setBusy(true);
        failCurrent(RequestState::TimedOut,"SEND_TIMEOUT",QString::fromUtf8(u8"串口写出未在时限内完成"));
    });
    connect(&m_ackTimer,&QTimer::timeout,this,[this]() {
        if(!m_active) return;
        if(m_preflight) { setBusy(true); failCurrent(RequestState::TimedOut,"POSITION_QUERY_TIMEOUT",QString::fromUtf8(u8"相对运动前未收到当前位置，未发送运动")); return; }
        if(isMotion(m_current)) setBusy(true);
        failCurrent(RequestState::TimedOut,"ACK_TIMEOUT",QString::fromUtf8(u8"等待设备命令应答超时"));
    });
    connect(m_transport,&ITransport::sendFinished,this,&RequestManager::onSendFinished);
    connect(m_transport,&ITransport::bytesReceived,this,&RequestManager::onBytesReceived);
    connect(m_transport,&ITransport::connectionChanged,this,&RequestManager::onConnectionChanged);
}

void RequestManager::setTimeouts(int sendMs,int ackMs)
{ m_sendTimeoutMs=qMax(1,sendMs); m_ackTimeoutMs=qMax(1,ackMs); }
void RequestManager::setBusy(bool busy)
{
    // 状态按目标保存并汇总显示；查询另一路空闲不能清掉此前未知的运动状态。
    // 此状态不作为发送条件，完整写出与应答等待仍由请求队列管理。
    if (busy) m_busyDevices.insert(m_current.targetDevice);
    else m_busyDevices.remove(m_current.targetDevice);
    const bool anyBusy = !m_busyDevices.isEmpty();
    if (m_deviceBusy != anyBusy) { m_deviceBusy = anyBusy; emit deviceBusyChanged(anyBusy); }
}

void RequestManager::enqueue(const UnifiedCommand &command)
{
    // 按值入队保存本次命令快照，不在排到队首时重新读取用户正在编辑的配置。
    if (command.operation == "MOTOR_STOP") {
        // 停止后不应自动恢复同一电机已排队的运动；其他模块的控制/查询仍保留顺序。
        for (int i = m_queue.size() - 1; i >= 0; --i) {
            if (!sameMotor(m_queue.at(i), command) || !isMotion(m_queue.at(i)) || m_queue.at(i).operation == "MOTOR_STOP") continue;
            const quint64 id = m_queue.takeAt(i).requestId;
            UnifiedResult cancelled; cancelled.requestId = id;
            cancelled.error = {"QUEUED_MOTION_STOPPED", QString::fromUtf8(u8"停止请求已取消同一电机尚未发送的运动")};
            emit stateChanged(id, RequestState::Cancelled, cancelled.error.message); emit completed(cancelled);
        }
        m_queue.prepend(command);
        interruptForStop();
    } else m_queue.enqueue(command);
    emit stateChanged(command.requestId,RequestState::Queued,QString::fromUtf8(u8"请求已进入发送队列"));
    emit activityChanged();
    QTimer::singleShot(0,this,&RequestManager::startNext);
}

bool RequestManager::canInterruptWithStop(const UnifiedCommand &command) const
{
    return command.operation == "MOTOR_STOP" && m_active && !m_finishAfterWrite && isMotion(m_current)
        && m_current.operation != "MOTOR_STOP" && sameMotor(m_current, command);
}

bool RequestManager::interruptForStop()
{
    if (m_writePending || m_queue.isEmpty() || !canInterruptWithStop(m_queue.head())) return false;
    // 只终止原运动的本地完成跟踪；此时设备可能仍在运动，不能清除占用或宣告原目标已完成。
    setBusy(true);
    failCurrent(RequestState::Cancelled, "MOTION_INTERRUPTED_BY_STOP", QString::fromUtf8(u8"原运动等待已被停止请求替代；原目标未确认完成"));
    // 在旧请求终止信号后恢复新停止请求的排队状态，界面仍显示存在待执行请求。
    emit stateChanged(m_queue.head().requestId, RequestState::Queued, QString::fromUtf8(u8"即将发送停止命令"));
    return true;
}

void RequestManager::startNext()
{
    if(m_active || m_queue.isEmpty()) return;
    m_current=m_queue.dequeue(); m_active=true; m_acked=false; m_probeActive=false; m_completionEmitted=false; m_preflight=false;
    // 统一编号表示；显式SDK电机查询参数是本次实际查询对象，必须与忙状态归属一致。
    bool targetOk = false; uint target = m_current.targetDevice.toUInt(&targetOk);
    if (m_current.operation == "QUERY_MOTOR" && m_current.parameters.contains("devId"))
        target = m_current.parameters.value("devId").toUInt(&targetOk);
    if (targetOk) m_current.targetDevice = QString::number(target);
    if (m_current.operation == "QUSHENG_C1")
        m_current.targetDevice = QStringLiteral("xyz:%1").arg(m_current.parameters.value("moduleIndex").toString());
    if(!m_transport->isConnected()) { failCurrent(RequestState::Failed,"DISCONNECTED",QString::fromUtf8(u8"下游串口未连接")); return; }
    // 设备忙闲仅用于显示；不以之前的运动或未知状态阻止用户再次发送。
    if (isRelative(m_current) && !m_current.packetDebug) {
        m_preflight = true;
        emit stateChanged(m_current.requestId, RequestState::Sending, QString::fromUtf8(u8"先查询当前位置并校验相对运动终点；尚未发送运动"));
        startProbe(); return;
    }
    sendCurrent();
}

void RequestManager::sendCurrent()
{
    // 定时器先于发送启动，因为传输层可同步报告失败，回告也可能很快到达。
    QByteArray frame; TranslationError error;
    if(!m_protocol->encodeCommand(m_current,&frame,&error)) { failCurrent(RequestState::Failed,error.code,error.message); return; }
    emit frameReady(m_current.requestId,frame);
    emit stateChanged(m_current.requestId,RequestState::Sending,QString::fromUtf8(u8"正在向下游串口写出趋盛帧"));
    m_sendTimer.start(m_sendTimeoutMs);
    m_writePending = true;
    m_transport->sendBytes(m_current.requestId,frame);
}

void RequestManager::onSendFinished(quint64 requestId,bool success,const QString &reason)
{
    if(!m_active || requestId!=m_current.requestId) return;
    m_writePending = false;
    m_sendTimer.stop();
    if(!success) {
        if(m_preflight) { setBusy(true); failCurrent(RequestState::Failed,"POSITION_QUERY_FAILED",QString::fromUtf8(u8"相对运动前的当前位置查询写出失败，未发送运动：%1").arg(reason)); return; }
        if(isMotion(m_current)) setBusy(true);
        failCurrent(RequestState::Failed,"SEND_FAILED",reason); return;
    }
    if (m_finishAfterWrite) { finishCurrent(); return; }
    if (interruptForStop()) return;
    if (m_sendAfterProbe) { m_sendAfterProbe = false; sendCurrent(); return; }
    emit stateChanged(requestId,RequestState::Written,QString::fromUtf8(u8"字节已写出，尚未收到设备命令应答"));
    emit stateChanged(requestId,RequestState::WaitingReply,m_probeActive
                      ? QString::fromUtf8(u8"等待 0x12 状态查询回告")
                      : QString::fromUtf8(u8"等待设备命令应答"));
    m_ackTimer.start(m_ackTimeoutMs);
}

void RequestManager::onBytesReceived(const QByteArray &data)
{
    emit rawDataReceived(data);
    QList<TranslationError> errors;
    const QList<ProtocolMessage> messages=m_protocol->feedReceivedData(data,&errors);
    for(const TranslationError &e:errors) emit protocolError(e);
    for(const ProtocolMessage &message:messages) {
        // 0x17 是主动状态上报，序号可能与当前请求无关。首版只用它更新忙状态，
        // 不能让它直接完成新请求，避免迟到上报误配。
        if(message.messageType==0x17 && message.payload.size()>=2
                && m_active && m_current.operation != "QUSHENG_C1"
                && quint8(message.payload.at(0))==m_current.targetDevice.toUInt()) {
            const int state=(quint8(message.payload.at(1))>>4)&0x0f;
            if(state==3 || state==2 || state==4) setBusy(true);
            emit unsolicitedMessage(message); continue;
        }
        const UnifiedCommand &expected=m_probeActive?m_probe:m_current;
        if(!m_active || m_finishAfterWrite || !m_protocol->matchesReply(expected,message)) { emit unsolicitedMessage(message); continue; }
        UnifiedResult result; TranslationError error;
        // 地址/序号正确但布局损坏的帧仍不能结束请求；显示解析错误并保留原应答超时。
        // 设备明确拒绝会成功解码为success=false，与格式错误不可合并。
        if(!m_protocol->decodeReply(expected,message,&result,&error)) { emit protocolError(error); continue; }
        // 接纳回告、手动查询和前置查询均立即显示；接纳不能显示成动作完成。
        // 内部查询沿用业务请求编号；错误序号和不匹配的设备报文不会进入此信号。
        result.requestId=m_current.requestId;
        if(result.success && result.data.contains("motorState"))
            result.data.insert("deviceBusy",result.data.value("motorState").toInt()!=1);
        else if(result.success && isMotion(expected)) result.data.insert("deviceBusy",true);
        emit replyReceived(result);
        // 若回告先到，仍等bytesWritten确认前帧已完全写出，停止/后续查询不能与它重叠。
        if (!m_writePending) m_sendTimer.stop();
        m_ackTimer.stop();
        if(m_probeActive) {
            m_probeActive=false;
            if (m_preflight) {
                // 相对目标必须由实机当前位置计算；前置查询失败时尚未发出运动。
                m_preflight = false;
                if (!result.success) { setBusy(true); failCurrent(RequestState::Failed,result.error.code,result.error.message); continue; }
                const int state = result.data.value("motorState").toInt();
                const qint64 currentPosition = result.data.value("positionUm").toLongLong();
                if (state != 1 || currentPosition == 0xffffff) {
                    setBusy(true); failCurrent(RequestState::Failed,"POSITION_QUERY_STATE",QString::fromUtf8(u8"设备未确认空闲和当前位置，未发送相对运动")); continue;
                }
                bool distanceOk = false, minOk = false, maxOk = false;
                const qint64 distance = m_current.parameters.value("distanceUm").toLongLong(&distanceOk);
                const qint64 minimum = m_current.parameters.value("minPositionUm").toLongLong(&minOk);
                const qint64 maximum = m_current.parameters.value("maxPositionUm").toLongLong(&maxOk);
                // 距离和当前值限制为24位，求和不会溢出64位；终点越界只结束请求，绝不发0x11。
                if (!distanceOk || !minOk || !maxOk || distance < 0 || distance >= 0xffffff
                        || minimum < 0 || maximum >= 0xffffff || minimum > maximum) {
                    setBusy(false); failCurrent(RequestState::Failed,"POSITION_LIMIT",QString::fromUtf8(u8"相对运动距离或行程无效，未发送运动")); continue;
                }
                const qint64 target = currentPosition + (m_current.operation == "RELATIVE_FORWARD" ? distance : -distance);
                if (currentPosition < minimum || currentPosition > maximum || target < minimum || target > maximum) {
                    setBusy(false); failCurrent(RequestState::Failed,"POSITION_LIMIT",QString::fromUtf8(u8"相对运动终点超出已配置行程，未发送运动")); continue;
                }
                m_current.parameters.insert("expectedPositionUm", target);
                if (m_writePending) m_sendAfterProbe = true;
                else sendCurrent();
                continue;
            }
            continue;
        }
        if(!result.success) { failCurrent(RequestState::Failed,result.error.code,result.error.message); continue; }
        m_acked=true;
        // 0x11 成功回告仅表示命令被接纳，不能合并为“动作完成”。
        emit stateChanged(m_current.requestId,RequestState::Acknowledged,QString::fromUtf8(u8"设备已接纳命令，但这不表示动作已完成"));
        if (!isMotion(m_current) && m_current.operation != "QUERY_MOTOR") {
            // 普通SDK命令以有效执行结果为结束条件，不套用电机位置轮询。
            emit stateChanged(m_current.requestId, RequestState::Succeeded, QString::fromUtf8(u8"设备回告已解析"));
            emit completed(result); m_completionEmitted=true; finishCurrent(true); continue;
        }
        if(m_current.operation=="QUERY_MOTOR") {
            const int motorState=result.data.value("motorState").toInt();
            if(motorState==1) setBusy(false);
            if(motorState==2 || motorState==3 || motorState==4) setBusy(true);
            if(motorState==4) { failCurrent(RequestState::Failed,"MOTOR_ERROR",QString::fromUtf8(u8"0x12 状态查询报告电机错误")); continue; }
            if(motorState==0 || motorState>4 || result.data.value("positionUm").toUInt()==0xffffffu) {
                setBusy(true); // 查询未能确认空闲，不能沿用之前的空闲状态放行新动作。
                failCurrent(RequestState::Failed,"MOTOR_STATE_UNKNOWN",QString::fromUtf8(u8"0x12 报告电机未回零或状态未知")); continue;
            }
            result.data.insert("deviceBusy",m_deviceBusy);
            emit stateChanged(m_current.requestId,RequestState::Succeeded,QString::fromUtf8(u8"状态查询回告已解析"));
            emit completed(result); m_completionEmitted=true; finishCurrent(true); continue;
        }
        setBusy(true);
        // 接纳结束本地请求；状态仅供参考，不宣告动作完成，也不主动发送查询。
        result.data.insert("deviceBusy",true);
        emit completed(result); m_completionEmitted=true;
        emit stateChanged(m_current.requestId,RequestState::Acknowledged,QString::fromUtf8(u8"设备已应答，可继续发送；动作完成未确认"));
        finishCurrent(true);
    }
}

void RequestManager::startProbe()
{
    // 内部查询只携带当前目标编号；地址/序号由同一适配器生成，不复用上次查询帧。
    if(!m_active || m_finishAfterWrite || !m_preflight || m_probeActive || m_writePending || !m_transport->isConnected()) return;
    m_probe=UnifiedCommand(); m_probe.requestId=m_current.requestId;
    m_probe.targetDevice=m_current.targetDevice; m_probe.operation="QUERY_MOTOR";
    m_probe.action=ActionType::Query;
    QByteArray frame; TranslationError error;
    if(!m_protocol->encodeCommand(m_probe,&frame,&error)) { failCurrent(RequestState::Failed,error.code,error.message); return; }
    m_probeActive=true; emit frameReady(m_current.requestId,frame);
    m_sendTimer.start(m_sendTimeoutMs);
    m_writePending = true;
    m_transport->sendBytes(m_current.requestId,frame);
}

void RequestManager::finishCurrent(bool waitForWrite)
{
    // 结束本地请求不等于设备空闲，因此只清定时器/等待标志，保留未知运动占用。
    // 延后启动下一请求，避免当前回告处理尚未退出就递归进入另一轮发送。
    m_ackTimer.stop();
    if (waitForWrite && m_writePending) { m_finishAfterWrite = true; return; }
    m_sendTimer.stop();
    m_active=false; m_probeActive=false; m_acked=false; m_preflight=false; m_writePending=false; m_sendAfterProbe=false; m_finishAfterWrite=false;
    emit activityChanged();
    QTimer::singleShot(0,this,&RequestManager::startNext);
}

void RequestManager::failCurrent(RequestState state,const QString &code,const QString &detail)
{
    if(!m_active) return;
    m_protocol->clearReceiveBuffer();
    UnifiedResult result; result.requestId=m_current.requestId; result.error={code,detail};
    emit stateChanged(result.requestId,state,detail);
    // 回告先于写出完成时仍可能发生本地写出失败；不重复业务结果，但保留错误。
    if(!m_completionEmitted) emit completed(result);
    else emit protocolError(result.error);
    finishCurrent();
}

void RequestManager::cancel(quint64 requestId)
{
    // 已发送运动的取消只停止本地等待，保留未知占用；排队请求则尚未产生设备效果。
    if(m_active && m_current.requestId==requestId) {
        if(isMotion(m_current) && (m_acked || m_sendTimer.isActive() || m_ackTimer.isActive())) setBusy(true);
        failCurrent(RequestState::Cancelled,"LOCAL_WAIT_CANCELLED",QString::fromUtf8(u8"已取消本地等待；未向设备发送停止命令")); return;
    }
    for(int i=0;i<m_queue.size();++i) if(m_queue.at(i).requestId==requestId) {
        m_queue.removeAt(i); UnifiedResult result; result.requestId=requestId;
        result.error={"CANCELLED",QString::fromUtf8(u8"排队请求已在发送前取消")};
        emit stateChanged(requestId,RequestState::Cancelled,result.error.message); emit completed(result); emit activityChanged(); return;
    }
}

void RequestManager::onConnectionChanged(bool connected,const QString &reason)
{
    if(!connected && m_active) {
        if(isMotion(m_current)) setBusy(true);
        failCurrent(RequestState::Failed,"DISCONNECTED",reason);
    }
}
