// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>
#include <memory>

#include "ZoteroClient.h"

namespace {
bool fail(const char* message) {
  std::fprintf(stderr, "TestZoteroClient: %s\n", message);
  return false;
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  QTcpServer server;
  if (!server.listen(QHostAddress::LocalHost, 0)) {
    return fail("could not start isolated loopback server") ? 0 : 1;
  }

  QTemporaryDir tempDir;
  if (!tempDir.isValid()) {
    return fail("could not create temporary directory") ? 0 : 1;
  }
  const QString pdfPath = tempDir.filePath(QStringLiteral("cleaned.pdf"));
  QFile pdf(pdfPath);
  if (!pdf.open(QIODevice::WriteOnly) || pdf.write("%PDF-1.4\n") == -1) {
    return fail("could not create temporary PDF") ? 0 : 1;
  }
  pdf.close();

  bool requestValidated = false;
  QObject::connect(&server, &QTcpServer::newConnection, &app, [&]() {
    QTcpSocket* socket = server.nextPendingConnection();
    auto requestBytes = std::make_shared<QByteArray>();
    QObject::connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
    QObject::connect(socket, &QTcpSocket::readyRead, socket,
                     [socket, requestBytes, &requestValidated, &pdfPath]() {
      if (socket->property("requestHandled").toBool()) {
        return;
      }
      requestBytes->append(socket->readAll());
      const int headerEnd = requestBytes->indexOf("\r\n\r\n");
      if (headerEnd < 0) {
        return;
      }

      const QByteArray headers = requestBytes->left(headerEnd).toLower();
      int contentLength = -1;
      for (const QByteArray& line : headers.split('\n')) {
        if (line.startsWith("content-length:")) {
          contentLength = line.mid(sizeof("content-length:") - 1).trimmed().toInt();
          break;
        }
      }
      if (contentLength < 0 || requestBytes->size() < headerEnd + 4 + contentLength) {
        return;
      }
      socket->setProperty("requestHandled", true);

      const QByteArray body = requestBytes->mid(headerEnd + 4, contentLength);
      const QJsonObject payload = QJsonDocument::fromJson(body).object();
      requestValidated = headers.startsWith("post /st-spectre/return ")
                         && headers.contains("\r\nx-zotero-connector-api-version: 3\r\n")
                         && headers.contains("\r\nauthorization: bearer test-token\r\n")
                         && payload.value(QStringLiteral("itemKey")).toString() == QStringLiteral("ABCD1234")
                         && payload.value(QStringLiteral("filePath")).toString() == pdfPath;

      const QByteArray responseBody = R"({"attachmentKey":"ZXCV5678"})";
      socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                    + QByteArray::number(responseBody.size()) + "\r\nConnection: close\r\n\r\n"
                    + responseBody);
      socket->disconnectFromHost();
    });
  });

  ZoteroClient client;
  ZoteroClient::Result result{ZoteroClient::Status::InvalidResponse, {}, {}};
  bool callbackCalled = false;
  QEventLoop loop;
  client.returnAttachmentAsync(
      QStringLiteral("http://127.0.0.1:%1/st-spectre/return").arg(server.serverPort()),
      QStringLiteral("test-token"), QStringLiteral("ABCD1234"), pdfPath,
      [&](ZoteroClient::Result value) {
        result = std::move(value);
        callbackCalled = true;
        loop.quit();
      },
      5000);
  QTimer::singleShot(6000, &loop, &QEventLoop::quit);
  loop.exec();

  if (!callbackCalled) {
    return fail("return callback was not called") ? 0 : 1;
  }
  if (!requestValidated) {
    return fail("return POST headers or JSON payload were incorrect") ? 0 : 1;
  }
  if (!result.ok() || result.attachmentKey != QStringLiteral("ZXCV5678")) {
    return fail("valid success response was not parsed") ? 0 : 1;
  }
  return 0;
}
