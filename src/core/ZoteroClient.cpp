// Copyright (C) 2024  ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ZoteroClient.h"

#include <QByteArray>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUuid>

namespace {
// IPv4 literal on purpose: "localhost" can resolve to ::1 and miss the
// connector server, which binds 127.0.0.1.
const char* const kBaseUrl = "http://127.0.0.1:23119";

// Zotero advertises connector API version 3. It is not strictly required for a
// 2xx on current builds, but we send it defensively to match the official
// connector's behaviour.
const char* const kApiVersionHeader = "X-Zotero-Connector-API-Version";
const char* const kApiVersionValue = "3";

QUrl endpoint(const QString& path) {
  return QUrl(QString::fromUtf8(kBaseUrl) + path);
}

// Adds a field to obj only when the value is non-empty (Zotero ignores empty
// strings, but omitting keeps the payload clean and matches the plan).
void putIfNotEmpty(QJsonObject& obj, const QString& key, const QString& value) {
  if (!value.isEmpty()) {
    obj.insert(key, value);
  }
}
}  // namespace

ZoteroClient::ZoteroClient(QObject* parent)
    : QObject(parent), m_networkManager(new QNetworkAccessManager(this)) {}

ZoteroClient::~ZoteroClient() = default;

void ZoteroClient::returnAttachmentAsync(const QString& returnUrl,
                                         const QString& token,
                                         const QString& itemKey,
                                         const QString& pdfPath,
                                         std::function<void(Result)> callback,
                                         int timeoutMs) {
  QJsonObject payload;
  payload.insert(QStringLiteral("itemKey"), itemKey);
  payload.insert(QStringLiteral("filePath"), QFileInfo(pdfPath).absoluteFilePath());

  QNetworkRequest request{QUrl(returnUrl)};
  request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
  request.setRawHeader(kApiVersionHeader, kApiVersionValue);
  request.setRawHeader("Authorization", QByteArray("Bearer ") + token.toUtf8());

  QNetworkReply* reply = m_networkManager->post(
      request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
  auto* timer = new QTimer(reply);
  timer->setSingleShot(true);
  connect(timer, &QTimer::timeout, reply, [reply]() {
    reply->setProperty("zoteroReturnTimedOut", true);
    reply->abort();
  });
  connect(reply, &QNetworkReply::finished, this,
          [reply, timer, callback = std::move(callback)]() mutable {
            timer->stop();
            Result result;
            const int httpStatus =
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray responseBody = reply->readAll();

            if (reply->property("zoteroReturnTimedOut").toBool()) {
              result = {Status::Timeout,
                        tr("Zotero did not respond in time. The PDF was exported normally."), {}};
            } else if (httpStatus == 200 && reply->error() == QNetworkReply::NoError) {
              QJsonParseError parseError;
              const QJsonDocument response = QJsonDocument::fromJson(responseBody, &parseError);
              const QString attachmentKey = response.isObject()
                                                ? response.object().value(QStringLiteral("attachmentKey")).toString()
                                                : QString();
              if (parseError.error == QJsonParseError::NoError && !attachmentKey.isEmpty()) {
                result = {Status::Ok,
                          tr("Returned to Zotero as attachment %1.").arg(attachmentKey),
                          attachmentKey};
              } else {
                result = {Status::InvalidResponse,
                          tr("Zotero returned an invalid success response. The PDF was exported normally."), {}};
              }
            } else if (httpStatus == 401) {
              result = {Status::HttpError,
                        tr("Zotero rejected the return token (HTTP 401). The PDF was exported normally."), {}};
            } else if (httpStatus == 404) {
              result = {Status::HttpError,
                        tr("The original Zotero item was not found (HTTP 404). The PDF was exported normally."), {}};
            } else if (httpStatus == 400) {
              result = {Status::HttpError,
                        tr("Zotero rejected the returned file or request (HTTP 400). The PDF was exported normally."), {}};
            } else if (httpStatus > 0) {
              result = {Status::HttpError,
                        tr("Zotero rejected the return request (HTTP %1). The PDF was exported normally.")
                            .arg(httpStatus), {}};
            } else {
              result = {Status::NotRunning,
                        tr("Could not reach Zotero. The PDF was exported normally."), {}};
            }

            reply->deleteLater();
            if (callback) {
              callback(std::move(result));
            }
          });
  timer->start(timeoutMs);
}

void ZoteroClient::pingAsync(std::function<void(bool)> callback) {
  QNetworkRequest request(endpoint("/connector/ping"));
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  request.setRawHeader(kApiVersionHeader, kApiVersionValue);

  // Empty JSON body: the connector rejects a POST with no Content-Length.
  QNetworkReply* reply = m_networkManager->post(request, QByteArray("{}"));

  connect(reply, &QNetworkReply::finished, this, [reply, callback = std::move(callback)]() {
    const bool ok = (reply->error() == QNetworkReply::NoError);
    reply->deleteLater();
    if (callback) {
      callback(ok);
    }
  });
}

bool ZoteroClient::ping(int timeoutMs) {
  QNetworkRequest request(endpoint("/connector/ping"));
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  request.setRawHeader(kApiVersionHeader, kApiVersionValue);

  QNetworkReply* reply = m_networkManager->post(request, QByteArray("{}"));

  QEventLoop loop;
  QTimer timer;
  timer.setSingleShot(true);
  bool timedOut = false;

  connect(&timer, &QTimer::timeout, &loop, [&loop, &timedOut, reply]() {
    timedOut = true;
    reply->abort();
    loop.quit();
  });
  connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

  timer.start(timeoutMs);
  loop.exec();

  const bool ok = !timedOut && reply->error() == QNetworkReply::NoError;
  reply->deleteLater();
  return ok;
}

ZoteroClient::Result ZoteroClient::sendBookWithAttachment(const BookMetadata& meta,
                                                          int numPages,
                                                          const QString& pdfPath,
                                                          int itemTimeoutMs,
                                                          int attachmentTimeoutMs) {
  // --- Pre-flight: is Zotero reachable at all? ---
  if (!ping()) {
    return {Status::NotRunning,
            tr("Zotero is not running. Start Zotero and try again "
               "(the PDF was still exported normally).")};
  }

  // --- Session and item identifiers (shared between the two requests) ---
  const QString sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  const QString itemId = QUuid::createUuid().toString(QUuid::WithoutBraces);

  // --- Build the book item JSON from the metadata ---
  QJsonObject item;
  item.insert("id", itemId);
  item.insert("itemType", "book");
  putIfNotEmpty(item, "title", meta.title);
  putIfNotEmpty(item, "date", meta.year);
  putIfNotEmpty(item, "publisher", meta.publisher);
  putIfNotEmpty(item, "place", meta.place);
  putIfNotEmpty(item, "ISBN", meta.isbn);  // Zotero's canonical casing is uppercase.
  putIfNotEmpty(item, "language", meta.language);
  if (numPages > 0) {
    item.insert("numPages", QString::number(numPages));  // Zotero expects a string.
  }

  QJsonArray creators;
  for (const BookMetadata::Creator& creator : meta.parseCreators()) {
    if (creator.firstName.isEmpty() && creator.lastName.isEmpty()) {
      continue;
    }
    QJsonObject c;
    c.insert("creatorType", meta.zoteroCreatorType());
    c.insert("firstName", creator.firstName);
    c.insert("lastName", creator.lastName);
    creators.append(c);
  }
  if (!creators.isEmpty()) {
    item.insert("creators", creators);
  }

  QJsonObject payload;
  payload.insert("sessionID", sessionId);
  payload.insert("uri", "http://localhost/scantailor-spectre");
  QJsonArray items;
  items.append(item);
  payload.insert("items", items);

  const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);

  // --- POST /connector/saveItems ---
  {
    QNetworkRequest request(endpoint("/connector/saveItems"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader(kApiVersionHeader, kApiVersionValue);

    QNetworkReply* reply = m_networkManager->post(request, body);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    bool timedOut = false;
    connect(&timer, &QTimer::timeout, &loop, [&loop, &timedOut, reply]() {
      timedOut = true;
      reply->abort();
      loop.quit();
    });
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(itemTimeoutMs);
    loop.exec();

    if (timedOut) {
      reply->deleteLater();
      return {Status::Timeout,
              tr("Zotero did not respond in time while creating the item "
                 "(the PDF was still exported normally).")};
    }
    if (reply->error() != QNetworkReply::NoError) {
      const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
      reply->deleteLater();
      if (httpStatus > 0) {
        return {Status::HttpError,
                tr("Zotero rejected the item (HTTP %1). "
                   "The PDF was still exported normally.")
                    .arg(httpStatus)};
      }
      return {Status::NotRunning,
              tr("Could not reach Zotero while creating the item "
                 "(the PDF was still exported normally).")};
    }
    reply->deleteLater();
  }

  // --- POST /connector/saveAttachment (raw PDF bytes) ---
  QFile pdfFile(pdfPath);
  if (!pdfFile.open(QIODevice::ReadOnly)) {
    return {Status::InvalidResponse,
            tr("The Zotero item was created, but the exported PDF could not be "
               "read to attach it.")};
  }
  const QByteArray pdfBytes = pdfFile.readAll();
  pdfFile.close();

  QJsonObject metadata;
  metadata.insert("sessionID", sessionId);
  metadata.insert("parentItemID", itemId);
  metadata.insert("url", QUrl::fromLocalFile(pdfPath).toString());
  metadata.insert("title", QFileInfo(pdfPath).fileName());
  const QByteArray metadataHeader = QJsonDocument(metadata).toJson(QJsonDocument::Compact);

  {
    QNetworkRequest request(endpoint("/connector/saveAttachment"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/pdf");
    request.setRawHeader(kApiVersionHeader, kApiVersionValue);
    request.setRawHeader("X-Metadata", metadataHeader);

    QNetworkReply* reply = m_networkManager->post(request, pdfBytes);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    bool timedOut = false;
    connect(&timer, &QTimer::timeout, &loop, [&loop, &timedOut, reply]() {
      timedOut = true;
      reply->abort();
      loop.quit();
    });
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(attachmentTimeoutMs);
    loop.exec();

    if (timedOut) {
      reply->deleteLater();
      return {Status::Timeout,
              tr("The Zotero item was created, but attaching the PDF timed out.")};
    }
    if (reply->error() != QNetworkReply::NoError) {
      const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
      reply->deleteLater();
      if (httpStatus > 0) {
        return {Status::HttpError,
                tr("The Zotero item was created, but the PDF attachment failed "
                   "(HTTP %1).")
                    .arg(httpStatus)};
      }
      return {Status::HttpError,
              tr("The Zotero item was created, but the PDF attachment could not "
                 "be sent.")};
    }
    reply->deleteLater();
  }

  return {Status::Ok, tr("Sent to Zotero.")};
}
