// Copyright (C) 2024  ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_BOOKLOOKUP_H_
#define SCANTAILOR_CORE_BOOKLOOKUP_H_

#include <QDeadlineTimer>
#include <QList>
#include <QObject>
#include <QString>

#include "BookMetadata.h"

class QNetworkAccessManager;
class QNetworkReply;

/**
 * Looks up canonical book metadata by ISBN from public online databases.
 *
 * OpenLibrary is the primary source (no key, generous rate limits); Google
 * Books is used to fill the language field (which OpenLibrary's jscmd=data
 * usually omits) and as a fallback when OpenLibrary has no record.
 *
 * Both sources are queried CONCURRENTLY under one shared deadline, so the worst
 * case is a single timeout rather than two stacked ones. That matters because
 * Google Books currently answers every keyless request with HTTP 429 (its
 * shared quota pool is exhausted); queried serially it would eat budget that
 * OpenLibrary needs. OpenLibrary — and only OpenLibrary — is retried on a
 * stall, a 429 or a 5xx; Google's 429 is a hard quota, so retrying it would be
 * both pointless and rude.
 *
 * All calls are blocking (nested QEventLoop; replies still running when the
 * shared deadline expires are aborted), and every failure is reported as a
 * soft, human-readable Result — never an exception. A failure message names
 * what each source did, e.g. "Open Library: timed out after 15 s. Google
 * Books: HTTP 429 (rate limited)."
 *
 * Endpoints used:
 *   - GET https://openlibrary.org/api/books?bibkeys=ISBN:<isbn>&format=json&jscmd=data
 *   - GET https://www.googleapis.com/books/v1/volumes?q=isbn:<isbn>
 */
class BookLookup : public QObject {
 public:
  enum class Status { Ok, NotFound, NetworkError, Timeout };

  struct Result {
    Status status;
    QString message;  // human-readable, already tr()'d

    bool ok() const { return status == Status::Ok; }
  };

  explicit BookLookup(QObject* parent = nullptr);
  ~BookLookup() override;

  /**
   * Blocking: normalize isbn to digits (keeping a trailing X), query
   * OpenLibrary and Google Books at the same time, then merge what came back.
   * timeoutMs is the budget for the whole lookup, OpenLibrary retries included.
   * On success out is populated (out.isbn is set to the normalized isbn).
   * Never throws.
   */
  Result lookupByIsbn(const QString& isbn, BookMetadata& out, int timeoutMs = 15000);

 private:
  // What one source did, kept so the failure message can name it.
  struct SourceOutcome {
    bool timedOut = false;      // still running when its deadline expired
    bool networkError = false;  // anything else that kept the body away
    int httpStatus = 0;         // 0 when no response line ever arrived

    bool ok() const { return !timedOut && !networkError; }

    // Worth another attempt: a stall, a rate limit or a server-side error.
    bool retryable() const {
      return timedOut || (httpStatus == 429) || ((httpStatus >= 500) && (httpStatus < 600));
    }
  };

  // Issues a GET for url and returns the pending reply. Never blocks.
  QNetworkReply* startRequest(const QString& url);

  // Spins a nested event loop until every reply has finished or deadline
  // expires; whatever is still running at that point is aborted.
  void awaitReplies(const QList<QNetworkReply*>& replies, QDeadlineTimer deadline);

  // Takes the body of a finished (or aborted) reply, records how it went in
  // outcome, and hands the reply over for deletion.
  QByteArray harvest(QNetworkReply* reply, SourceOutcome& outcome);

  // One sentence about one source, e.g. "Open Library: timed out after 15 s."
  // Never exposes raw Qt error enums.
  static QString describeOutcome(const QString& source, const SourceOutcome& outcome, int timeoutMs);

  // Fills fields of out from an OpenLibrary record object. Returns true if the
  // record had at least a title.
  bool parseOpenLibrary(const QByteArray& body, const QString& isbn, BookMetadata& out);

  // Merges Google Books data into out: fills language always, and any still-empty
  // fields. Returns true if Google Books had a record.
  bool mergeGoogleBooks(const QByteArray& body, const QString& isbn, BookMetadata& out);

  QNetworkAccessManager* m_networkManager;
};

#endif  // ifndef SCANTAILOR_CORE_BOOKLOOKUP_H_
