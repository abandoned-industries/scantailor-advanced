// Copyright (C) 2024  ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "BookLookup.h"

#include <QByteArray>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <algorithm>

namespace {
const char* const kUserAgent = "ScanTailorSpectre/2.0";

// OpenLibrary usually answers in a couple of seconds but now and then stalls
// past ten. Since it is effectively our only source (see the class comment), a
// single attempt makes one stall a dead end: one try plus up to two retries,
// all inside the caller's budget, turns it into a slow success instead.
const int kOpenLibraryAttempts = 3;
const int kRetryBackoffMs = 400;  // multiplied by the retry number
const int kMinAttemptMs = 1000;   // don't start an attempt that can't finish

// Deadline for one OpenLibrary attempt. Attempts other than the last stop well
// short of the shared deadline so that a stalled connection still leaves room
// for another try; the last one may use whatever is left. Either way the whole
// lookup stays inside the caller's budget.
QDeadlineTimer attemptDeadline(const QDeadlineTimer& shared, int timeoutMs, bool lastAttempt) {
  const qint64 remaining = shared.remainingTime();
  if (lastAttempt) {
    return QDeadlineTimer(remaining);
  }
  return QDeadlineTimer(std::min<qint64>(remaining, timeoutMs * 3 / 5));
}

// Backoff that keeps the (already blocking) caller's event loop turning.
void pause(qint64 ms) {
  if (ms <= 0) {
    return;
  }
  QEventLoop loop;
  QTimer::singleShot(static_cast<int>(ms), &loop, &QEventLoop::quit);
  loop.exec();
}

// Digits only, but preserve a trailing X (ISBN-10 check digit), uppercased.
QString normalizeIsbn(const QString& raw) {
  QString out;
  for (const QChar& c : raw) {
    if (c.isDigit()) {
      out.append(c);
    } else if (c == QLatin1Char('X') || c == QLatin1Char('x')) {
      out.append(QLatin1Char('X'));
    }
  }
  return out;
}

// Extract the first 4-digit year (15xx-20xx) from a free-form publish date such
// as "2000", "June 2000" or "1992-06-01".
QString extractYear(const QString& date) {
  static const QRegularExpression yearRe(QStringLiteral("\\b(1[5-9]\\d{2}|20\\d{2})\\b"));
  const QRegularExpressionMatch m = yearRe.match(date);
  return m.hasMatch() ? m.captured(1) : QString();
}
}  // namespace

BookLookup::BookLookup(QObject* parent)
    : QObject(parent), m_networkManager(new QNetworkAccessManager(this)) {}

BookLookup::~BookLookup() = default;

QNetworkReply* BookLookup::startRequest(const QString& url) {
  QNetworkRequest request((QUrl(url)));
  request.setRawHeader("User-Agent", kUserAgent);
  return m_networkManager->get(request);
}

void BookLookup::awaitReplies(const QList<QNetworkReply*>& replies, QDeadlineTimer deadline) {
  QEventLoop loop;
  int pending = 0;
  for (QNetworkReply* reply : replies) {
    if (reply->isFinished()) {
      continue;
    }
    ++pending;
    connect(reply, &QNetworkReply::finished, &loop, [&loop, &pending]() {
      if (--pending <= 0) {
        loop.quit();
      }
    });
  }
  if (pending == 0) {
    return;
  }

  QTimer timer;
  timer.setSingleShot(true);
  connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
  timer.start(static_cast<int>(std::max<qint64>(0, deadline.remainingTime())));
  loop.exec();

  // Anything still in flight has run out of the shared budget. abort() is the
  // only place we cancel a reply, which is how harvest() tells a stall from a
  // refusal.
  for (QNetworkReply* reply : replies) {
    if (!reply->isFinished()) {
      reply->abort();
    }
  }
}

QByteArray BookLookup::harvest(QNetworkReply* reply, SourceOutcome& outcome) {
  outcome = SourceOutcome();
  outcome.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

  QByteArray body;
  if (reply->error() == QNetworkReply::NoError) {
    body = reply->readAll();
  } else if (reply->error() == QNetworkReply::OperationCanceledError) {
    outcome.timedOut = true;
  } else {
    // Includes HTTP 429 and 5xx, which arrive as errors with a status code.
    outcome.networkError = true;
  }
  reply->deleteLater();
  return body;
}

QString BookLookup::describeOutcome(const QString& source, const SourceOutcome& outcome, int timeoutMs) {
  if (outcome.timedOut) {
    return tr("%1: timed out after %2 s.").arg(source, QString::number(timeoutMs / 1000.0, 'g', 2));
  }
  if (outcome.httpStatus == 429) {
    return tr("%1: HTTP 429 (rate limited).").arg(source);
  }
  if ((outcome.httpStatus >= 500) && (outcome.httpStatus < 600)) {
    return tr("%1: HTTP %2 (service unavailable).").arg(source).arg(outcome.httpStatus);
  }
  if (outcome.networkError && (outcome.httpStatus > 0)) {
    return tr("%1: HTTP %2.").arg(source).arg(outcome.httpStatus);
  }
  if (outcome.networkError) {
    return tr("%1: could not connect.").arg(source);
  }
  return tr("%1: no record.").arg(source);
}

bool BookLookup::parseOpenLibrary(const QByteArray& body, const QString& isbn, BookMetadata& out) {
  const QJsonDocument doc = QJsonDocument::fromJson(body);
  if (!doc.isObject()) {
    return false;
  }
  const QJsonObject root = doc.object();
  // Response is keyed by "ISBN:<isbn>"; an empty root means "not found".
  const QString key = QStringLiteral("ISBN:") + isbn;
  const QJsonObject rec = root.value(key).toObject();
  if (rec.isEmpty()) {
    return false;
  }

  QString title = rec.value("title").toString();
  if (title.isEmpty()) {
    return false;
  }
  const QString subtitle = rec.value("subtitle").toString();
  if (!subtitle.isEmpty()) {
    title += QStringLiteral(": ") + subtitle;
  }
  out.title = title;

  QStringList authorNames;
  for (const QJsonValue& a : rec.value("authors").toArray()) {
    const QString name = a.toObject().value("name").toString();
    if (!name.isEmpty()) {
      authorNames << name;
    }
  }
  if (!authorNames.isEmpty()) {
    out.authors = authorNames.join(QStringLiteral("; "));
  }

  const QString year = extractYear(rec.value("publish_date").toString());
  if (!year.isEmpty()) {
    out.year = year;
  }

  const QJsonArray publishers = rec.value("publishers").toArray();
  if (!publishers.isEmpty()) {
    const QString name = publishers.first().toObject().value("name").toString();
    if (!name.isEmpty()) {
      out.publisher = name;
    }
  }

  const QJsonArray places = rec.value("publish_places").toArray();
  if (!places.isEmpty()) {
    const QString name = places.first().toObject().value("name").toString();
    if (!name.isEmpty()) {
      out.place = name;
    }
  }

  out.isbn = isbn;
  return true;
}

bool BookLookup::mergeGoogleBooks(const QByteArray& body, const QString& isbn, BookMetadata& out) {
  const QJsonDocument doc = QJsonDocument::fromJson(body);
  if (!doc.isObject()) {
    return false;
  }
  const QJsonArray items = doc.object().value("items").toArray();
  if (items.isEmpty()) {
    return false;
  }
  const QJsonObject info = items.first().toObject().value("volumeInfo").toObject();
  if (info.isEmpty()) {
    return false;
  }

  // Language is what Google Books adds over OpenLibrary; always take it.
  const QString language = info.value("language").toString();
  if (!language.isEmpty()) {
    out.language = language;
  }

  // Fill only fields OpenLibrary left empty — don't overwrite its (usually
  // richer) values.
  if (out.title.isEmpty()) {
    QString title = info.value("title").toString();
    const QString subtitle = info.value("subtitle").toString();
    if (!title.isEmpty() && !subtitle.isEmpty()) {
      title += QStringLiteral(": ") + subtitle;
    }
    if (!title.isEmpty()) {
      out.title = title;
    }
  }
  if (out.authors.isEmpty()) {
    QStringList authorNames;
    for (const QJsonValue& a : info.value("authors").toArray()) {
      const QString name = a.toString();
      if (!name.isEmpty()) {
        authorNames << name;
      }
    }
    if (!authorNames.isEmpty()) {
      out.authors = authorNames.join(QStringLiteral("; "));
    }
  }
  if (out.year.isEmpty()) {
    const QString year = extractYear(info.value("publishedDate").toString());
    if (!year.isEmpty()) {
      out.year = year;
    }
  }
  if (out.publisher.isEmpty()) {
    const QString publisher = info.value("publisher").toString();
    if (!publisher.isEmpty()) {
      out.publisher = publisher;
    }
  }

  out.isbn = isbn;
  return true;
}

BookLookup::Result BookLookup::lookupByIsbn(const QString& isbn, BookMetadata& out, int timeoutMs) {
  const QString norm = normalizeIsbn(isbn);
  if (norm.isEmpty()) {
    return {Status::NotFound, tr("No ISBN to look up.")};
  }

  // One budget for the whole lookup, retries included.
  const QDeadlineTimer deadline(timeoutMs);

  const QString olUrl = QStringLiteral(
                            "https://openlibrary.org/api/books?bibkeys=ISBN:%1&format=json&jscmd=data")
                            .arg(norm);
  const QString gbUrl =
      QStringLiteral("https://www.googleapis.com/books/v1/volumes?q=isbn:%1").arg(norm);

  // --- Both sources at once: OpenLibrary (primary) and Google Books ---
  QNetworkReply* olReply = startRequest(olUrl);
  QNetworkReply* gbReply = startRequest(gbUrl);
  awaitReplies({olReply, gbReply}, attemptDeadline(deadline, timeoutMs, kOpenLibraryAttempts == 1));

  SourceOutcome ol;
  SourceOutcome gb;
  QByteArray olBody = harvest(olReply, ol);
  const QByteArray gbBody = harvest(gbReply, gb);

  // --- Retry OpenLibrary only; Google's 429 is a quota, not a hiccup ---
  for (int attempt = 2; (attempt <= kOpenLibraryAttempts) && !ol.ok() && ol.retryable(); ++attempt) {
    pause(std::min<qint64>(kRetryBackoffMs * (attempt - 1), deadline.remainingTime()));
    if (deadline.remainingTime() < kMinAttemptMs) {
      break;
    }
    QNetworkReply* retryReply = startRequest(olUrl);
    awaitReplies({retryReply}, attemptDeadline(deadline, timeoutMs, attempt == kOpenLibraryAttempts));
    olBody = harvest(retryReply, ol);
  }

  // OpenLibrary first: Google Books only fills what it left empty, and a
  // Google failure must never cost us an OpenLibrary record.
  bool haveRecord = false;
  if (ol.ok()) {
    haveRecord = parseOpenLibrary(olBody, norm, out);
  }
  bool gbRecord = false;
  if (gb.ok()) {
    gbRecord = mergeGoogleBooks(gbBody, norm, out);
  }

  if (haveRecord || gbRecord) {
    return {Status::Ok, tr("Found metadata for ISBN %1.").arg(norm)};
  }

  // Neither source had a usable record. If the primary source never answered we
  // cannot claim the book is unknown — say what each source actually did.
  if (!ol.ok()) {
    const QString detail = QStringList{describeOutcome(QStringLiteral("Open Library"), ol, timeoutMs),
                                       describeOutcome(QStringLiteral("Google Books"), gb, timeoutMs)}
                               .join(QLatin1Char(' '));
    return {ol.timedOut ? Status::Timeout : Status::NetworkError, detail};
  }
  return {Status::NotFound, tr("No record found for ISBN %1.").arg(norm)};
}
