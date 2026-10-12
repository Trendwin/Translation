#include "requestmanager.h"
#include "qusheng/qushengcommandcatalog.h"

namespace {
bool isRelative(const UnifiedCommand &command)
{ return command.operation == "RELATIVE_FORWARD" || command.operation == "RELATIVE_BACKWARD"; }
bool isMotion(const UnifiedCommand &command)
{ return QushengCommandCatalog::isAbsoluteMotion(command.operation) || isRelative(command); }
}

RequestManager::RequestManager(IInternalProtocol *protocol,ITransport *transport,QObject *parent)
    :QObject(parent),m_protocol(protocol),m_transport(transport)
{
    for(QTimer *t:{&m_sendTimer,&m_ackTimer,&m_motionTimer,&m_pollTimer}) t->setSingleShot(true);
    connect(&m_sendTimer,&QTimer::timeout,this,[this]() {
        if(!m_active) return;
        if(m_preflight) { setBusy(true); failCurrent(RequestState::TimedOut,"POSITION_QUERY_TIMEOUT",QString::fromUtf8(u8"相对运动前的当前位置查询写出超时，未发送运动")); return; }
        if(m_probeActive) { m_probeActive=false; scheduleProbe(); return; }
        if(isMotion(m_current)) setBusy(true);
        failCurrent(RequestState::TimedOut,"SEND_TIMEOUT",QString::fromUtf8(u8"串口写出未在时限内完成"));
    });
    connect(&m_ackTimer,&QTimer::timeout,this,[this]() {
        if(!m_active) return;
        if(m_preflight) { setBusy(true); failCurrent(RequestState::TimedOut,"POSITION_QUERY_TIMEOUT",QString::fromUtf8(u8"相对运动前未收到当前位置，未发送运动")); return; }
        if(m_probeActive) { m_probeActive=false; scheduleProbe(); return; }
        if(isMotion(m_current)) setBusy(true);
        failCurrent(RequestState::TimedOut,"ACK_TIMEOUT",QString::fromUtf8(u8"等待设备命令应答超时"));
    });
    connect(&m_motionTimer,&QTimer::timeout,this,[this]() {
        if(m_active && m_acked) failCurrent(RequestState::TimedOut,"MOTION_TIMEOUT",QString::fromUtf8(u8"动作完成未确认；设备保持忙或状态未知"));
    });
    connect(&m_pollTimer,&QTimer::timeout,this,&RequestManager::startProbe);
    connect(m_transport,&ITransport::sendFinished,this,&RequestManager::onSendFinished);
    connect(m_transport,&ITransport::bytesReceived,this,&RequestManager::onBytesReceived);
    connect(m_transport,&ITransport::connectionChanged,this,&RequestManager::onConnectionChanged);
}

void RequestManager::setTimeouts(int sendMs,int ackMs,int motionMs)
{ m_sendTimeoutMs=qMax(1,sendMs); m_ackTimeoutMs=qMax(1,ackMs); m_motionTimeoutMs=qMax(1,motionMs); }
void RequestManager::setBusy(bool busy)
{ if(m_deviceBusy!=busy) { m_deviceBusy=busy; emit deviceBusyChanged(busy); } }

void RequestManager::enqueue(const UnifiedCommand &command)
{
    m_queue.enqueue(command);
    emit stateChanged(command.requestId,RequestState::Queued,QString::fromUtf8(u8"请求已进入发送队列"));
    QTimer::singleShot(0,this,&RequestManager::startNext);
}

void RequestManager::startNext()
{
    if(m_active || m_queue.isEmpty()) return;
    m_current=m_queue.dequeue(); m_active=true; m_acked=false; m_probeActive=false; m_completionEmitted=false; m_preflight=false;
    if(!m_transport->isConnected()) { failCurrent(RequestState::Failed,"DISCONNECTED",QString::fromUtf8(u8"下游串口未连接")); return; }
    // 已经向界面发出 ack 结果并不释放设备；只有完成查询确认空闲后才能接收下一条运动。
    if(m_deviceBusy && isMotion(m_current)) {
        failCurrent(RequestState::Failed,"DEVICE_BUSY",QString::fromUtf8(u8"上一动作仍在执行，或设备最终状态尚未确认")); return;
    }
    if (isRelative(m_current)) {
        m_preflight = true;
        emit stateChanged(m_current.requestId, RequestState::Sending, QString::fromUtf8(u8"先查询当前位置并校验相对运动终点；尚未发送运动"));
        startProbe(); return;
    }
    sendCurrent();
}

void RequestManager::sendCurrent()
{
    QByteArray frame; TranslationError error;
    if(!m_protocol->encodeCommand(m_current,&frame,&error)) { failCurrent(RequestState::Failed,error.code,error.message); return; }
    emit frameReady(m_current.requestId,frame);
    emit stateChanged(m_current.requestId,RequestState::Sending,QString::fromUtf8(u8"正在向下游串口写出趋盛帧"));
    m_sendTimer.start(m_sendTimeoutMs);
    m_transport->sendBytes(m_current.requestId,frame);
}

void RequestManager::onSendFinished(quint64 requestId,bool success,const QString &reason)
{
    if(!m_active || requestId!=m_current.requestId) return;
    if(m_acked && !m_probeActive) return; // 回告可能早于本地 bytesWritten；不能把已应答状态退回等待应答。
    m_sendTimer.stop();
    if(!success) {
        if(m_preflight) { setBusy(true); failCurrent(RequestState::Failed,"POSITION_QUERY_FAILED",QString::fromUtf8(u8"相对运动前的当前位置查询写出失败，未发送运动：%1").arg(reason)); return; }
        if(m_probeActive) { m_probeActive=false; scheduleProbe(); return; }
        if(isMotion(m_current)) setBusy(true);
        failCurrent(RequestState::Failed,"SEND_FAILED",reason); return;
    }
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
                && m_active && quint8(message.payload.at(0))==m_current.targetDevice.toUInt()) {
            const int state=(quint8(message.payload.at(1))>>4)&0x0f;
            if(state==3 || state==2 || state==4) setBusy(true);
            emit unsolicitedMessage(message); continue;
        }
        const UnifiedCommand &expected=m_probeActive?m_probe:m_current;
        if(!m_active || !m_protocol->matchesReply(expected,message)) { emit unsolicitedMessage(message); continue; }
        UnifiedResult result; TranslationError error;
        if(!m_protocol->decodeReply(expected,message,&result,&error)) { failCurrent(RequestState::Failed,error.code,error.message); continue; }
        m_sendTimer.stop(); m_ackTimer.stop();
        if(m_probeActive) {
            m_probeActive=false;
            if (m_preflight) {
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
                sendCurrent(); continue;
            }
            if(!result.success) { scheduleProbe(); continue; }
            const int state=result.data.value("motorState").toInt();
            const quint32 position=result.data.value("positionUm").toUInt();
            const quint32 targetPosition = m_current.parameters.value(isRelative(m_current) ? "expectedPositionUm" : "positionUm").toUInt();
            if(state==1 && position==targetPosition) {
                setBusy(false);
                if(!m_completionEmitted) { result.requestId=m_current.requestId; result.data.insert("deviceBusy",false); emit completed(result); m_completionEmitted=true; }
                emit stateChanged(m_current.requestId,RequestState::Succeeded,QString::fromUtf8(u8"设备已空闲且位置等于本次目标"));
                finishCurrent();
            } else if(state==4) failCurrent(RequestState::Failed,"MOTOR_ERROR",QString::fromUtf8(u8"0x12 状态查询报告电机错误"));
            else scheduleProbe();
            continue;
        }
        if(!result.success) { failCurrent(RequestState::Failed,result.error.code,result.error.message); continue; }
        m_acked=true;
        // 0x11 成功回告仅表示命令被接纳，不能合并为“动作完成”。
        emit stateChanged(m_current.requestId,RequestState::Acknowledged,QString::fromUtf8(u8"设备已接纳命令，但这不表示动作已完成"));
        if (m_current.operation == "QUERY_DEVICE_INFO") {
            emit stateChanged(m_current.requestId, RequestState::Succeeded, QString::fromUtf8(u8"设备版本回告已解析"));
            emit completed(result); m_completionEmitted=true; finishCurrent(); continue;
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
            emit completed(result); m_completionEmitted=true; finishCurrent(); continue;
        }
        setBusy(true);
        if(m_current.responsePolicy=="ack") {
            result.data.insert("deviceBusy",true);
            emit completed(result); m_completionEmitted=true;
        }
        emit stateChanged(m_current.requestId,RequestState::InMotion,QString::fromUtf8(u8"动作已应答，继续轮询 0x12 判断是否完成"));
        m_motionTimer.start(m_motionTimeoutMs); scheduleProbe();
    }
}

void RequestManager::scheduleProbe()
{ if(m_active && m_acked && !m_probeActive) m_pollTimer.start(200); }

void RequestManager::startProbe()
{
    if(!m_active || (!m_acked && !m_preflight) || m_probeActive || !m_transport->isConnected()) return;
    m_probe=UnifiedCommand(); m_probe.requestId=m_current.requestId;
    m_probe.targetDevice=m_current.targetDevice; m_probe.operation="QUERY_MOTOR";
    m_probe.action=ActionType::Query;
    QByteArray frame; TranslationError error;
    if(!m_protocol->encodeCommand(m_probe,&frame,&error)) { failCurrent(RequestState::Failed,error.code,error.message); return; }
    m_probeActive=true; emit frameReady(m_current.requestId,frame);
    m_sendTimer.start(m_sendTimeoutMs);
    m_transport->sendBytes(m_current.requestId,frame);
}

void RequestManager::finishCurrent()
{
    m_sendTimer.stop(); m_ackTimer.stop(); m_motionTimer.stop(); m_pollTimer.stop();
    m_active=false; m_probeActive=false; m_acked=false; m_preflight=false;
    QTimer::singleShot(0,this,&RequestManager::startNext);
}

void RequestManager::failCurrent(RequestState state,const QString &code,const QString &detail)
{
    if(!m_active) return;
    m_protocol->clearReceiveBuffer();
    UnifiedResult result; result.requestId=m_current.requestId; result.error={code,detail};
    emit stateChanged(result.requestId,state,detail);
    // ack 策略已经通知过“命令接纳”，后续失败仍必须展示；它不撤回之前的接纳事实。
    // 使用独立错误信号避免重复发出业务 completed，同时让界面看到动作超时等最终原因。
    if(!m_completionEmitted) emit completed(result);
    else emit protocolError(result.error);
    finishCurrent();
}

void RequestManager::cancel(quint64 requestId)
{
    if(m_active && m_current.requestId==requestId) {
        if(isMotion(m_current) && (m_acked || m_sendTimer.isActive() || m_ackTimer.isActive())) setBusy(true);
        failCurrent(RequestState::Cancelled,"LOCAL_WAIT_CANCELLED",QString::fromUtf8(u8"已取消本地等待；未向设备发送停止命令")); return;
    }
    for(int i=0;i<m_queue.size();++i) if(m_queue.at(i).requestId==requestId) {
        m_queue.removeAt(i); UnifiedResult result; result.requestId=requestId;
        result.error={"CANCELLED",QString::fromUtf8(u8"排队请求已在发送前取消")};
        emit stateChanged(requestId,RequestState::Cancelled,result.error.message); emit completed(result); return;
    }
}

void RequestManager::onConnectionChanged(bool connected,const QString &reason)
{
    if(!connected && m_active) {
        if(isMotion(m_current)) setBusy(true);
        failCurrent(RequestState::Failed,"DISCONNECTED",reason);
    }
}
