#include "qipipe.h"
#include <QStringList>
#include <QRegExp>
#include <QNetworkProxyFactory>
#include <QSslSocket>
#include <QThread>
#include <QApplication>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QDebug>
#include <QNetworkProxy>
#include <QTcpSocket>
#ifdef Q_OS_WIN
#include "qiwinhttp.h"
#endif
#include <QByteArray>
#include <qglobal.h>
#include "qiproxyserver.h"
#include <QFile>
#include <qirulemanager.h>

static void isInMain(QString info){
    if(QThread::currentThread() == QApplication::instance()->thread()){
        qDebug()<<info<<" in main thread.";
    }else{
        qDebug()<<info<<" not main thread.";
    }
}

QiPipe::QiPipe(int socketDescriptor):_socketDescriptor(socketDescriptor){
    //qDebug()<<"new QiPipe"<<socketDescriptor;
}

QiPipe::~QiPipe(){
    //QMutexLocker locker(&mutex);
    //Q_UNUSED(locker);
    if(qp){
        qp->disconnect(this);
        qp->deleteLater();
        qp = NULL;
    }
    //qDebug()<<"~QiPipe "<<_socketDescriptor;
}

void QiPipe::run(){
    qp = new QiPipe_Private(_socketDescriptor);
    connect(qp,SIGNAL(connected(ConnectionData_ptr)),this,SIGNAL(connected(ConnectionData_ptr)));
    connect(qp,SIGNAL(finishSuccess(ConnectionData_ptr)),this,SIGNAL(completed(ConnectionData_ptr)));
    connect(qp,SIGNAL(finishedWithError(ConnectionData_ptr)),this,SIGNAL(completed(ConnectionData_ptr)));
    connect(qp,SIGNAL(pipeFinished()),SIGNAL(pipeFinished()));
    //connect(qp,SIGNAL(pipeFinished()),SLOT(onPipeFinished()));
}


void QiPipe::onPipeFinished(){

}

//===========QiPipe_Private
QiPipe_Private::QiPipe_Private(int descriptor):requestSocket(NULL),responseSocket(NULL){

    requestState = Initial;
    responseState = Initial;

    // setup request socket
    requestSocket = new QTcpSocket();
    connect(requestSocket,SIGNAL(readyRead()),this,SLOT(onRequestReadReady()));
    connect(requestSocket,SIGNAL(disconnected()),this,SLOT(onRequestClose()));
    connect(requestSocket,SIGNAL(aboutToClose()),this,SLOT(onRequestClose()));
    connect(requestSocket,SIGNAL(error(QAbstractSocket::SocketError)),this,SLOT(onRequestError()));

    bool isSocketValid = requestSocket->setSocketDescriptor(descriptor);
    if(!isSocketValid){
        qWarning()<<"invalid socket descriptor!!";
        return;
    }
}
QiPipe_Private::~QiPipe_Private(){
    //qDebug()<<"~QiPipe_Private";
    tearDown();
}


void QiPipe_Private::onRequestReadReady(){

    QMutexLocker locker(&mutex);
    Q_UNUSED(locker);

    QByteArray newReqData = requestSocket->readAll();
    //if(newReqData.indexOf("u148.net")!=-1){
        //qDebug()<<"onRequestReady:"<<"\n"<<newReqData;
    //}
    //update request buffer
    requestBuffer.append(newReqData);
    parseRequest(newReqData);
}

// 检查请求数据中是否有header，如果有header则检查请求包是否完整，并重置requestBuffer
void QiPipe_Private::parseRequest(const QByteArray &newContent){

    //qDebug()<<"requesting"<< requestSocket->socketDescriptor()<<newContent;

    if(requestState != HeaderFound){// no header, parse one more time ( state =  Initial || PackageFound
        requestBodyRemain = 0;
        parseRequestHeader(newContent);//如果获取新header,则放入bufferConnectionArray
        if(requestState != HeaderFound){
            //qDebug()<<"requestState is not PackageFound 1";
            return;
        }
        QByteArray contentLenght = gettingRequestConnectionData->getRequestHeader("Content-Length");
        //qDebug()<<contentLenght;
        requestContentLength = contentLenght.toInt();
        requestBodyRemain = requestContentLength;
    }
    // parse body
    // is need to count request length?
    // 检查是否已获取所有数据
    if(requestContentLength == 0){
        // no body to send

        //TODO connectionData 入栈与requestState相关联，需要setRequestState方法统一
        requestState = PackageFound;
        bufferConnectionArray.push_back(gettingRequestConnectionData);
        gettingRequestConnectionData.clear();


    }else{
        // need body
        //qDebug()<<"req content-length="<<requestContentLength<<" remain="<<requestBodyRemain;
        int bufferBodyLength = requestBuffer.length();
        //qDebug()<<bufferBodyLength;
        requestBodyRemain = requestBodyRemain - bufferBodyLength;
        //qDebug()<<"req content-length="<<requestContentLength<<" remain="<<requestBodyRemain;
        if(gettingRequestConnectionData->appendRequestBody(requestBuffer)){
            //TODO connectionData 入栈与requestState相关联，需要setRequestState方法统一
            requestState = PackageFound;
            bufferConnectionArray.push_back(gettingRequestConnectionData);
            gettingRequestConnectionData.clear();
        }
        requestBuffer.clear();
        //TODO?
    }
    if(requestState != PackageFound){
        //为后续逻辑简化，第一个请求仅当收到一个完整的包才开始发送
        //qDebug()<<"requestState is not PackageFound 2";
        return;
    }
    if(receivingResponseConnectinoData.isNull()){
        receivingResponseConnectinoData = nextConnectionData();
    }
	if((!receivingResponseConnectinoData.isNull()) &&
			(receivingResponseConnectinoData->getRequestHeader("Host") == "127.0.0.1" || receivingResponseConnectinoData->host == "localhost") &&
			receivingResponseConnectinoData->getRequestHeader("Port")=="8889"){//避免死循环
        //TODO
        QMap<QString,QString> contentTypeMapping;
        contentTypeMapping["jpg"] = "image/jpeg";
        contentTypeMapping["js"] = "application/javascript";
        contentTypeMapping["png"] = "image/png";
        contentTypeMapping["gif"] = "image/gif";
        contentTypeMapping["css"] = "text/css";
        contentTypeMapping["html"] = "text/html";
        contentTypeMapping["htm"] = "text/html";
        contentTypeMapping["txt"] = "text/plain";
        contentTypeMapping["jpeg"] = "image/jpeg";
        contentTypeMapping["manifest"] = "text/cache-manifest";

        // output content
        QByteArray s;
        QString returnStatus = "200 OK";
        QString contentType = "text/html";

        QByteArray byteToWrite;
        QString filePath = receivingResponseConnectinoData->path;
        //remove ?xxx
        int queryIndex = filePath.indexOf('?');
        if(queryIndex != -1) filePath = filePath.left(queryIndex);

        //remote #xxx
        int hashIndex = filePath.indexOf('#');
        if(hashIndex != -1) filePath = filePath.left(hashIndex);

        if(filePath.endsWith("/")){
            filePath.append("index.html");
        }
		filePath.prepend(":/web");
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly )){
            returnStatus = "404 NOT FOUND";
            s.append(filePath + " Not Found");
        }else{
            s.clear();
            s = file.readAll();
        }
        file.close();
        int postFixIndex = filePath.lastIndexOf(".");
        if(postFixIndex!=-1){
            QString postFix = filePath.mid(postFixIndex+1);
            contentType = contentTypeMapping.value(postFix,"text/plain");
        }


        int count = s.length();

        byteToWrite.append(QString("HTTP/1.0 %1 \r\nServer: Qiddler \r\nContent-Type: %2 \r\nContent-Length: %3 \r\n\r\n").arg(returnStatus).arg(contentType).arg(count));
        receivingResponseConnectinoData->setResponseHeader(byteToWrite);
        byteToWrite.append(s);

        QByteArray tmp = byteToWrite;
        qDebug()<<tmp.replace("\r","\\r");

        receivingResponseConnectinoData->appendResponseBody(s);
        //qDebug(byteToWrite);
        emit(finishSuccess(receivingResponseConnectinoData));
        receivingResponseConnectinoData.clear();
        int n = requestSocket->write(byteToWrite)<<s.size();
        qDebug()<<n << byteToWrite.size();
        requestSocket->flush();
        return;
    }


    bool gotRule;
    QMap<QiRuleManager::ConfigKey,QVariant> rule = QiRuleManager::instance()->getRule( receivingResponseConnectinoData,&gotRule );
    if(gotRule){
        qDebug()<<"got rule";
        int type = rule[ QiRuleManager::ConfigKey_RuleType].toInt();
        QString pattern = rule[QiRuleManager::ConfigKey_RulePattern].toString();
        QString replace = rule[QiRuleManager::ConfigKey_RuleReplace].toString();
        QByteArray byteToWrite;
        int count;
        QByteArray body;
        QEventLoop theLoop;
        QNetworkReply* reply;
        QFile f;
        QString status;
        bool fileCanOpen;
        qDebug()<<"rultype="<<type;
        switch(type){
            case QiRuleManager::RuleType_DomainReplace:
                //receivingResponseConnectinoData->setHost(replace);
                //qDebug()<<receivingResponseConnectinoData->host;
                break;
            case QiRuleManager::RuleType_SimpleAddressReplace:
                break;
             case QiRuleManager::RuleType_LocalContentReplace:
                f.setFileName(replace);
                fileCanOpen = f.open(QFile::ReadOnly);

                status = "200 OK";

                if(fileCanOpen){
                    body = f.readAll();
                    f.close();
                }else{
                    status = "404 Not Found";
                    body.append(QString("file:%1 not found").arg(replace));
                }
                count = body.size();
                byteToWrite.append(QString("HTTP/1.1 %1 \r\nServer: Qiddler \r\nContent-Type: %2 \r\nContent-Length: %3 \r\n\r\n")
                                   .arg(status)
                                   .arg("text/html") // TODO reuse contentTypeMapping above
                                   .arg(count));
                receivingResponseConnectinoData->setResponseHeader(byteToWrite);
                qDebug()<<"body="<<body;
                byteToWrite.append(body);
                receivingResponseConnectinoData->appendResponseBody(body);
                requestSocket->write(byteToWrite);
                requestSocket->flush();
                emit finishSuccess(receivingResponseConnectinoData);
                break;
            case QiRuleManager::RuleType_ComplexAddressReplace:
                break;

            case QiRuleManager::RuleType_RemoteContentReplace:
                reply = networkManager.get(QNetworkRequest(QUrl(replace)));
                connect(&networkManager,SIGNAL(finished(QNetworkReply*)),&theLoop,SLOT(quit()));
                theLoop.exec();
                int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() || 404;
                QByteArray resone = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toByteArray();
                byteToWrite.append(QString("HTTP/1.1 %1 FROM REMOTE REPLACE \r\nServer: Qiddler \r\nContent-Type: %2 \r\nContent-Length: %3 \r\n\r\n")
                        .arg(status)
                        .arg(reply->header(QNetworkRequest::ContentTypeHeader).toString())
                        .arg(reply->header(QNetworkRequest::ContentLengthHeader).toString()));
                receivingResponseConnectinoData->setResponseHeader(byteToWrite);
                body = reply->readAll();
                byteToWrite.append(body);
                receivingResponseConnectinoData->appendResponseBody(body);
                requestSocket->write(byteToWrite);
                requestSocket->flush();
                requestSocket->close();
                qDebug()<<body;
                break;

        }

        if(QiRuleManager::isRuleNeedBlockOrientResponse(type)){
            qDebug()<<"discard this request";
            receivingResponseConnectinoData.clear();
            return;
        }
    }


    if(responseState != Initial && responseState != Connecting){
        //同一个socket中发起n个请求的情况
        //条件是：当第二个同域请求发起时，有已经连接成功并返回数据的数据
        //所以这里可以简单处理

        //2012.1.19 ippan:需先判断是否需替换内容
        responseSocket->write(newContent);
        responseSocket->flush();

    }else if(responseState == Initial){
        responseState = Connecting;
        responseSocket = new QTcpSocket();
        connect(responseSocket,SIGNAL(connected()),SLOT(onResponseConnected()));
        connect(responseSocket,SIGNAL(readyRead()),SLOT(onResponseReadReady()));
        connect(responseSocket,SIGNAL(disconnected()),SLOT(onResponseClose()));
        connect(responseSocket,SIGNAL(aboutToClose()),SLOT(onResponseClose()));
        connect(responseSocket,SIGNAL(error(QAbstractSocket::SocketError)),SLOT(onResponseError(QAbstractSocket::SocketError)));

        //connect(responseSocket,SIGNAL(hostFound()),SLOT(onResponseHostFound()));
        //connect(responseSocket,SIGNAL(stateChanged(QAbstractSocket::SocketState)),SLOT(onResponseHostFound()));
#ifdef Q_OS_WIN
        // TODO add mac pac support
        QList<QNetworkProxy> proxylist = QiWinHttp::queryProxy(QNetworkProxyQuery(QUrl(receivingResponseConnectinoData->fullUrl)));
        for(int i=0,l=proxylist.length();i<l;++i){
            QNetworkProxy p = proxylist.at(i);
            responseSocket->setProxy(p);
            //qDebug()<<"proxy="<<p.hostName()<<p.port();
        }
#endif

        //qDebug()<<"CONNECT TO "<<receivingResponseConnectinoData->getRequestHeader("Host")<<receivingResponseConnectinoData->getRequestHeader("Port");
        //responseSocket->setProxy(QNetworkProxy(QNetworkProxy::HttpProxy,"127.0.0.1",8888));
        responseSocket->connectToHost(receivingResponseConnectinoData->getRequestHeader("Host"),receivingResponseConnectinoData->getRequestHeader("Port").toInt());
    }
}


void QiPipe_Private::parseRequestHeader(const QByteArray &newContent){
    Q_UNUSED(newContent);

    QByteArray header;
    int indexOfRN = requestBuffer.indexOf(QByteArray("\r\n\r\n"));
    int indexOfN = requestBuffer.indexOf(QByteArray("\n\n"));
    if(indexOfRN!=-1){
        requestHeaderSpliterSize = 4;
        requestHeaderSpliterIndex = indexOfRN;
    }else if(indexOfN!=-1){
        requestHeaderSpliterSize = 2;
        requestHeaderSpliterIndex = indexOfN;
    }else{
        return;
    }
    header = requestBuffer.left(requestHeaderSpliterIndex);

    //cut header part from buffer
    if(requestBuffer.size()>=requestHeaderSpliterIndex+requestHeaderSpliterSize){
        requestBuffer.remove(0,requestHeaderSpliterIndex+requestHeaderSpliterSize);
    }else{
        // seems will never enter here
        Q_ASSERT_X(false,"parseRequestHeader","invalid header length?");
        requestBuffer.clear();
    }

    //TODO use setRequestState
    requestState = HeaderFound;

    // create new QiConnectionData (will sharedpoiter do clear for old pointer? anwser:YES)

    gettingRequestConnectionData = ConnectionData_ptr(new QiConnectionData());
    gettingRequestConnectionData->setRequestHeader(header);
    gettingRequestConnectionData->id = QiProxyServer::nextConnectionId();
    //qDebug()<<"---gettingRequestConnectionData->id = "<<gettingRequestConnectionData->id;

    emit connected(gettingRequestConnectionData);
}


void QiPipe_Private::onRequestError(){
    //qDebug()<<"onRequestError";
    tearDown();
}


void QiPipe_Private::onResponseConnected(){

    // need?
    QMutexLocker locker(&mutex);
    Q_UNUSED(locker)
    /*
    if(serverIp.isEmpty()){
        serverIp = responseSocket->peerAddress().toString();
        receivingResponseConnectinoData->serverIP = serverIp;
    }
    qDebug()<<responseSocket->peerName()<<serverIp;
    */
    responseState = Connected;
    if(receivingResponseConnectinoData.isNull()){
        receivingResponseConnectinoData = nextConnectionData();
    }
    // emit connect signal
    //if(receivingResponseConnectinoData->getRequestHeader("Host") == "support.qq.com"){
    //    qDebug()<<"send this:\n"<<responseSocket->peerName()<<responseSocket->peerPort()<<receivingResponseConnectinoData->requestRawDataToSend;
    //}
    while(receivingResponseConnectinoData->requestRawDataToSend.size()>0){
        if(receivingResponseConnectinoData->host == "id.qq.com"){
            qDebug()<<"id.qq.com"<<receivingResponseConnectinoData->requestRawDataToSend;
        }
        qint64 n = responseSocket->write(receivingResponseConnectinoData->requestRawDataToSend);
        responseSocket->flush();
        if(n==-1){
            qDebug()<<"write error !!!!!";
            break;
        }
        receivingResponseConnectinoData->requestRawDataToSend.remove(0,n);
    }
}


void QiPipe_Private::onResponseReadReady(){
    QMutexLocker locker(&mutex);
    Q_UNUSED(locker)
    QByteArray ba = responseSocket->readAll();


    //qDebug()<<"when readReady:"<<responseSocket->peerAddress().toString();
    if(serverIp.isEmpty()){
        serverIp = responseSocket->peerAddress().toString();
        receivingResponseConnectinoData->serverIP = serverIp;
    }
    //qDebug()<<responseSocket->peerName()<<serverIp;

    requestSocket->write(ba);
    requestSocket->flush();

    //qDebug()<<"========response========***"<<responseSocket->peerName()<<connectionData->path;
    //qDebug()<<connectionData->requestRawDataToSend;
    //qDebug()<<ba;
    //qDebug()<<"***========response========"<<responseSocket->peerName()<<connectionData->path;

    responseBuffer.append(ba);
    if(parseResponse(ba)){
        // package got end
        //qDebug()<<receivingResponseConnectinoData->responseBody;
        ConnectionData_ptr tmp = receivingResponseConnectinoData;
        emit finishSuccess(tmp);
        /*
        if(receivingResponseConnectinoData->getResponseHeader("Connection")=="close"){
            locker.unlock();
            tearDown();
            qDebug()<<"turn down connection";
            return;
        }
        */
        // reset receivingResponseConnection
        receivingResponseConnectinoData.clear();
    }
}


void QiPipe_Private::onResponseError(QAbstractSocket::SocketError e){
    //qDebug()<<"responseSocket error"<<e;
    tearDown();
}


void QiPipe_Private::onRequestClose(){
    //qDebug()<<"onRequestClose";
    tearDown();
}


void QiPipe_Private::onResponseClose(){
    //qDebug()<<"response close";
    tearDown();
}



bool QiPipe_Private::parseResponse(const QByteArray newContent){
    // if got response end return true
    if(responseState != HeaderFound){
        //qDebug()<<"response no found header 1\n"<<responseBuffer;
        if(!responseBuffer.startsWith(QByteArray("HTTP"))){
            qDebug()<<"======== wrong header!!!"<<responseState;
            qDebug()<<responseBuffer;
            // try to fix it?
            int i = responseBuffer.indexOf("HTTP/1.1");
            responseBuffer.remove(0,i);
            qDebug()<<responseBuffer;
            //return false;
        }
        parseResponseHeader(responseBuffer);
    }

    if(receivingResponseConnectinoData->returnCode == 400){
        qDebug()<<"response 400\n"<<receivingResponseConnectinoData->requestHeaderRawData;
    }

    if(responseState != HeaderFound){//check if got end
        //qDebug()<<"response no found header 2";
        return false;
    }
    //qDebug()<<"response HeaderFound";
    if(parseResponseBody(newContent)){
        return true;
    }
    return false;
}

void QiPipe_Private::parseResponseHeader(const QByteArray &newContent){
    Q_UNUSED(newContent)
    responseHeaderSpliterIndex = responseBuffer.indexOf(QByteArray("\r\n\r\n"));
    if(responseHeaderSpliterIndex!=-1){
        responseHeaderSpliterSize = 4;
    }else{
        responseHeaderSpliterIndex = responseBuffer.indexOf(QByteArray("\n\n"));
        if(responseHeaderSpliterIndex != -1){
            responseHeaderSpliterSize = 2;
        }else{
            return;
        }
    }
    //qDebug()<<"got response header";
    // got header : cut buffer & set state to HeaderFound
    responseState = HeaderFound;
    receivingResponseConnectinoData->setResponseHeader(responseBuffer.left(responseHeaderSpliterIndex));
    responseBuffer.remove(0,responseHeaderSpliterIndex+responseHeaderSpliterSize);
    //qDebug()<<"after set header:"<<responseBuffer;

    // 需要在pipe这里保存一份吗？
    isResponseChunked = receivingResponseConnectinoData->getResponseHeader("Transfer-Encoding").toLower() == "chunked";
    responseContentLength = receivingResponseConnectinoData->getResponseHeader("Content-Length").toInt();
    responseComressType = receivingResponseConnectinoData->getResponseHeader("Content-Encoding");
}

bool QiPipe_Private::parseResponseBody(QByteArray newContent){

    //根据http协议，需由header及body共同判断请求是否结束。
    QByteArray ba = responseBuffer;
    responseBuffer.clear();
    if(receivingResponseConnectinoData->appendResponseBody(ba)){
        //qDebug()<<"PackageFound";
        responseState = PackageFound;
        //qDebug()<<"response PackageFound";
        //qDebug()<<ba;
        return true;
    }
    return false;

}

//为避免死锁，调用些函数里*必须*解锁mutex
void QiPipe_Private::tearDown(){
    //QMutexLocker locker(&mutex);
    //Q_UNUSED(locker);
    if(requestSocket){
        requestSocket->blockSignals(true);
        requestSocket->disconnect(this);
        if(requestSocket->isOpen()){
            requestSocket->abort();
        }
        requestSocket->deleteLater();
        requestSocket = NULL;
    }
    if(responseSocket){
        responseSocket->blockSignals(true);
        responseSocket->disconnect(this);
        if(responseSocket->isOpen()){
            responseSocket->abort();
        }
        responseSocket->deleteLater();
        responseSocket = NULL;
    }
    for(int i=0;i<bufferConnectionArray.size();++i){
        if(bufferConnectionArray.at(i)->returnCode == -1){
            bufferConnectionArray.at(i)->returnCode = 500;
        }
        emit finishedWithError(bufferConnectionArray.at(i));
    }
    if(receivingResponseConnectinoData){
        if(receivingResponseConnectinoData->returnCode == -1){
            receivingResponseConnectinoData->returnCode = 500;
        }
        emit finishedWithError(receivingResponseConnectinoData);
    }
    emit pipeFinished();
}

void QiPipe_Private::finishConnectionSuccess(){

}

void QiPipe_Private::finishConnectionWithError(int errno){

}
ConnectionData_ptr QiPipe_Private::nextConnectionData(){
    // when package done fetch new connection data
    // TODO use single function do this
    if(bufferConnectionArray.size()==0){//是否会出现请求未发送完成就开始返回内容的情况？
        qDebug()<<"gettingRequestConnectionData isNull?"<<gettingRequestConnectionData.isNull();
        qDebug()<<"ERROR!!! connect response socket without any request!是否会出现请求未发送完成就开始返回内容的情况?";
        ConnectionData_ptr empty;
        return empty;
    }
    receivingResponseConnectinoData = bufferConnectionArray.at(0);
    bufferConnectionArray.remove(0,1);
    receivingResponseConnectinoData->serverIP = serverIp;
    return receivingResponseConnectinoData;
}
