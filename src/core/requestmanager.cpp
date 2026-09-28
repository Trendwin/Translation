#include "requestmanager.h"

RequestManager::RequestManager(IInternalProtocol *protocol,ITransport *transport,QObject *parent)
    :QObject(parent),m_protocol(protocol),m_transport(transport)
{
    for(QTimer *t:{&m_sendTimer,&m_ackTimer,&m_motionTimer,&m_pollTimer}) t->setSingleShot(true);
    connect(&m_sendTimer,&QTimer::timeout,this,[this]() {
        if(!m_active) return;
        if(m_probeActive) { m_probeActive=false; scheduleProbe(); return; }
        if(m_current.operation=="ABSOLUTE_MOVE") setBusy(true);
        failCurrent(RequestState::TimedOut,"SEND_TIMEOUT","serial write did not finish");
    });
    connect(&m_ackTimer,&QTimer::timeout,this,[this]() {
        if(!m_active) return;
        if(m_probeActive) { m_probeActive=false; scheduleProbe(); return; }
        if(m_current.operation=="ABSOLUTE_MOVE") setBusy(true);
        failCurrent(RequestState::TimedOut,"ACK_TIMEOUT","device command ACK timed out");
    });
    connect(&m_motionTimer,&QTimer::timeout,this,[this]() {
        if(m_active && m_acked) failCurrent(RequestState::TimedOut,"MOTION_TIMEOUT","motion completion not confirmed; device state remains busy/unknown");
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
    emit stateChanged(command.requestId,RequestState::Queued,QStringLiteral("queued"));
    QTimer::singleShot(0,this,&RequestManager::startNext);
}

void RequestManager::startNext()
{
    if(m_active || m_queue.isEmpty()) return;
    m_current=m_queue.dequeue(); m_active=true; m_acked=false; m_probeActive=false; m_upstreamReplied=false;
    if(!m_transport->isConnected()) { failCurrent(RequestState::Failed,"DISCONNECTED","downstream disconnected"); return; }
    if(m_deviceBusy && m_current.operation=="ABSOLUTE_MOVE") {
        failCurrent(RequestState::Failed,"DEVICE_BUSY","previous motion is still busy or its final state is unknown"); return;
    }
    QByteArray frame; TranslationError error;
    if(!m_protocol->encodeCommand(m_current,&frame,&error)) { failCurrent(RequestState::Failed,error.code,error.message); return; }
    emit frameReady(m_current.requestId,frame);
    emit stateChanged(m_current.requestId,RequestState::Sending,"writing downstream frame");
    m_sendTimer.start(m_sendTimeoutMs);
    m_transport->sendBytes(m_current.requestId,frame);
}

void RequestManager::onSendFinished(quint64 requestId,bool success,const QString &reason)
{
    if(!m_active || requestId!=m_current.requestId) return;
    if(m_acked && !m_probeActive) return; // ACK can precede bytesWritten callback.
    m_sendTimer.stop();
    if(!success) {
        if(m_probeActive) { m_probeActive=false; scheduleProbe(); return; }
        if(m_current.operation=="ABSOLUTE_MOVE") setBusy(true);
        failCurrent(RequestState::Failed,"SEND_FAILED",reason); return;
    }
    emit stateChanged(requestId,RequestState::Written,"bytes written; no command ACK yet");
    emit stateChanged(requestId,RequestState::WaitingReply,m_probeActive?"waiting 0x12 probe reply":"waiting command ACK");
    m_ackTimer.start(m_ackTimeoutMs);
}

void RequestManager::onBytesReceived(const QByteArray &data)
{
    emit rawDataReceived(data);
    QList<TranslationError> errors;
    const QList<ProtocolMessage> messages=m_protocol->feedReceivedData(data,&errors);
    for(const TranslationError &e:errors) emit protocolError(e);
    for(const ProtocolMessage &message:messages) {
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
            if(!result.success) { scheduleProbe(); continue; }
            const int state=result.data.value("motorState").toInt();
            const quint32 position=result.data.value("positionUm").toUInt();
            if(state==1 && position==m_current.parameters.value("positionUm").toUInt()) {
                setBusy(false);
                if(!m_upstreamReplied) { result.requestId=m_current.requestId; result.data.insert("deviceBusy",false); emit completed(result); m_upstreamReplied=true; }
                emit stateChanged(m_current.requestId,RequestState::Succeeded,"motion idle at requested absolute position");
                finishCurrent();
            } else if(state==4) failCurrent(RequestState::Failed,"MOTOR_ERROR","0x12 reports motor error");
            else scheduleProbe();
            continue;
        }
        if(!result.success) { failCurrent(RequestState::Failed,result.error.code,result.error.message); continue; }
        m_acked=true;
        emit stateChanged(m_current.requestId,RequestState::Acknowledged,"device accepted command; motion completion not implied");
        if(m_current.operation=="QUERY_MOTOR") {
            const int motorState=result.data.value("motorState").toInt();
            if(motorState==1) setBusy(false);
            if(motorState==2 || motorState==3 || motorState==4) setBusy(true);
            if(motorState==4) { failCurrent(RequestState::Failed,"MOTOR_ERROR","0x12 reports motor error"); continue; }
            if(motorState==0 || motorState>4 || result.data.value("positionUm").toUInt()==0xffffffu) {
                failCurrent(RequestState::Failed,"MOTOR_STATE_UNKNOWN","0x12 reports unhomed or unknown motor state"); continue;
            }
            result.data.insert("deviceBusy",m_deviceBusy);
            emit stateChanged(m_current.requestId,RequestState::Succeeded,"query reply received");
            emit completed(result); m_upstreamReplied=true; finishCurrent(); continue;
        }
        setBusy(true);
        if(m_current.responsePolicy=="ack") {
            result.data.insert("deviceBusy",true);
            emit completed(result); m_upstreamReplied=true;
        }
        emit stateChanged(m_current.requestId,RequestState::InMotion,"motion acknowledged; polling 0x12 for completion");
        m_motionTimer.start(m_motionTimeoutMs); scheduleProbe();
    }
}

void RequestManager::scheduleProbe()
{ if(m_active && m_acked && !m_probeActive) m_pollTimer.start(200); }

void RequestManager::startProbe()
{
    if(!m_active || !m_acked || m_probeActive || !m_transport->isConnected()) return;
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
    m_active=false; m_probeActive=false; m_acked=false;
    QTimer::singleShot(0,this,&RequestManager::startNext);
}

void RequestManager::failCurrent(RequestState state,const QString &code,const QString &detail)
{
    if(!m_active) return;
    m_protocol->clearReceiveBuffer();
    UnifiedResult result; result.requestId=m_current.requestId; result.error={code,detail};
    emit stateChanged(result.requestId,state,detail);
    if(!m_upstreamReplied) emit completed(result);
    finishCurrent();
}

void RequestManager::cancel(quint64 requestId)
{
    if(m_active && m_current.requestId==requestId) {
        if(m_current.operation=="ABSOLUTE_MOVE" && (m_acked || m_sendTimer.isActive() || m_ackTimer.isActive())) setBusy(true);
        failCurrent(RequestState::Cancelled,"LOCAL_WAIT_CANCELLED","local wait cancelled; device was not stopped"); return;
    }
    for(int i=0;i<m_queue.size();++i) if(m_queue.at(i).requestId==requestId) {
        m_queue.removeAt(i); UnifiedResult result; result.requestId=requestId;
        result.error={"CANCELLED","queued request cancelled before send"};
        emit stateChanged(requestId,RequestState::Cancelled,result.error.message); emit completed(result); return;
    }
}

void RequestManager::onConnectionChanged(bool connected,const QString &reason)
{
    if(!connected && m_active) {
        if(m_current.operation=="ABSOLUTE_MOVE") setBusy(true);
        failCurrent(RequestState::Failed,"DISCONNECTED",reason);
    }
}
