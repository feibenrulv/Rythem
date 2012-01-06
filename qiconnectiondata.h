#ifndef PIPEDATA_H
#define PIPEDATA_H

#include <QObject>
#include <QMap>
#include <QDebug>
#include <QSharedPointer>

class QiConnectionData {
private:
public:
    QiConnectionData(int socketDescriptor=-1);
    ~QiConnectionData(){
        qDebug()<<"~PipeData";
    }

    inline QiConnectionData(const QiConnectionData& p){//copy ctor
        number = p.number;
        /*
        returnCode = p.returnCode;
        protocol = p.protocol;
        serverIP = p.serverIP;
        host = p.host;
        path = p.path;
        port = p.port;
        resStatus = p.resStatus;
        resHeader = p.resHeader;
        resBody = p.resBody;
        fullUrl = p.fullUrl;
        requestMethod = p.requestMethod;
        */
        setRequestRawData(p.requestRawData);
        setResponseRawData(p.responseRawData);
    }

    int socketId;
    int number;
    int returnCode;
    QString protocol;
    QString serverIP;
    QString host;
    QString path;
    QString fullUrl;
    int port;
    QString requestMethod;
    QString responseStatus;

    QByteArray requestRawDataToSend;


    void setRequestHeader(QByteArray name,QByteArray value);
    void setRequestHeader(QByteArray headerBa);
    QByteArray getRequestHeader(QByteArray name)const;
    QByteArray getRequestHeader()const;
    QByteArray getRequestBody()const;

    void setResponseHeader(QByteArray header);
    void setResponseBody(QByteArray body);
    QByteArray getResponseHeader(QByteArray name)const;
    QByteArray getResponseHeader()const;
    QByteArray getResponseBody()const;
private:
    QByteArray requestRawData;
    QByteArray responseRawData;
    QMap<QByteArray,QByteArray> allRequestHeaders;
    QMap<QByteArray,QByteArray> allResponseHeaders;

    void setRequestRawData(QByteArray request);//only for copy Ctor
    void setResponseRawData(QByteArray response);//only for copy Ctor
};
typedef QSharedPointer<const QiConnectionData> ConnectionData_const_ptr;
typedef QSharedPointer<QiConnectionData> ConnectionData_ptr;
typedef const QSharedPointer<QiConnectionData> ConnectionData_ptr_const;
typedef const QSharedPointer<const QiConnectionData> ConnectionData_const_ptr_const;




#endif // PIPEDATA_H