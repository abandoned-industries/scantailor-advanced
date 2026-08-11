// Copyright (C) 2024  ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_ZOTEROCLIENT_H_
#define SCANTAILOR_CORE_ZOTEROCLIENT_H_

#include <QObject>
#include <QString>
#include <functional>

#include "BookMetadata.h"

class QNetworkAccessManager;

/**
 * Talks to the locally running Zotero desktop app via its connector HTTP
 * server (the same interface the official browser connector uses),
 * listening on 127.0.0.1:23119.
 *
 * No account, API key, or add-on is required; Zotero simply has to be running.
 * The connector API is unofficial, so every failure is reported as a soft,
 * human-readable Result and never throws.
 *
 * Endpoints used (verified empirically against Zotero 9.0.4):
 *   - POST /connector/ping            -> 200, advertises connector API v3
 *   - POST /connector/saveItems       -> 201 Created (empty body)
 *   - POST /connector/saveAttachment  -> 201 Created (empty body)
 */
class ZoteroClient : public QObject {
 public:
  enum class Status { Ok, NotRunning, Timeout, HttpError, InvalidResponse };

  struct Result {
    Status status;
    QString message;  // human-readable, already tr()'d
    QString attachmentKey{};

    bool ok() const { return status == Status::Ok; }
  };

  explicit ZoteroClient(QObject* parent = nullptr);
  ~ZoteroClient() override;

  /**
   * Non-blocking connectivity check for a live status label.
   * The callback is invoked with true/false on the caller's (UI) thread,
   * driven by the reply's finished signal. Safe to call repeatedly (e.g.
   * from a timer). The client must outlive the pending reply.
   */
  void pingAsync(std::function<void(bool)> callback);

  /**
   * Asynchronously return an exported PDF to an existing Zotero item through
   * the authenticated ScanTailor Spectre add-on endpoint.
   */
  void returnAttachmentAsync(const QString& returnUrl,
                             const QString& token,
                             const QString& itemKey,
                             const QString& pdfPath,
                             std::function<void(Result)> callback,
                             int timeoutMs = 60000);

  /**
   * Blocking connectivity check. Runs a nested event loop with a single-shot
   * timeout; aborts the reply on timeout. Returns true iff Zotero answered
   * with a 2xx within timeoutMs.
   */
  bool ping(int timeoutMs = 1500);

  /**
   * Blocking: create a Zotero "book" item from meta plus attach the PDF at
   * pdfPath to it.
   *
   * Sequence: ping pre-flight -> POST /connector/saveItems ->
   * POST /connector/saveAttachment. A ping failure yields NotRunning; a failed
   * attachment (after a successful item) yields a soft HttpError/Timeout with a
   * message noting the item was still created. Never throws.
   */
  Result sendBookWithAttachment(const BookMetadata& meta,
                                int numPages,
                                const QString& pdfPath,
                                int itemTimeoutMs = 10000,
                                int attachmentTimeoutMs = 60000);

 private:
  QNetworkAccessManager* m_networkManager;
};

#endif  // ifndef SCANTAILOR_CORE_ZOTEROCLIENT_H_
