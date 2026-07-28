#include "core/json5.h"

#include <QJsonParseError>
#include <algorithm>

namespace yauml {
namespace {

bool isIdentifierStart(QChar c) {
  return c.isLetter() || c == u'_' || c == u'$';
}

bool isIdentifierPart(QChar c) {
  return c.isLetterOrNumber() || c == u'_' || c == u'$';
}

bool isSafeSerializedIdentifier(const QStringView &value) {
  if (value.isEmpty())
    return false;
  const auto isAsciiLetter = [](QChar c) {
    return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z');
  };
  if (!isAsciiLetter(value.first()) && value.first() != u'_' &&
      value.first() != u'$')
    return false;
  return std::all_of(value.cbegin() + 1, value.cend(), [&](QChar c) {
    return isAsciiLetter(c) || (c >= u'0' && c <= u'9') || c == u'_' ||
           c == u'$';
  });
}

QString normalizeStringsAndComments(const QString &input, bool &hadComments,
                                    QString &error) {
  QString out;
  out.reserve(input.size());
  enum class State {
    Normal,
    DoubleString,
    SingleString,
    LineComment,
    BlockComment
  };
  State state = State::Normal;

  for (qsizetype i = 0; i < input.size(); ++i) {
    const QChar c = input.at(i);
    const QChar next = i + 1 < input.size() ? input.at(i + 1) : QChar();

    switch (state) {
    case State::Normal:
      if (c == u'"') {
        state = State::DoubleString;
        out += c;
      } else if (c == u'\'') {
        state = State::SingleString;
        out += u'"';
      } else if (c == u'/' && next == u'/') {
        hadComments = true;
        state = State::LineComment;
        out += u' ';
        ++i;
        out += u' ';
      } else if (c == u'/' && next == u'*') {
        hadComments = true;
        state = State::BlockComment;
        out += u' ';
        ++i;
        out += u' ';
      } else {
        out += c;
      }
      break;
    case State::DoubleString:
      out += c;
      if (c == u'\\' && i + 1 < input.size())
        out += input.at(++i);
      else if (c == u'"')
        state = State::Normal;
      break;
    case State::SingleString:
      if (c == u'\\' && i + 1 < input.size()) {
        const QChar escaped = input.at(++i);
        if (escaped == u'\'')
          out += u'\'';
        else if (escaped == u'"')
          out += QStringLiteral("\\\"");
        else {
          out += u'\\';
          out += escaped;
        }
      } else if (c == u'"') {
        out += QStringLiteral("\\\"");
      } else if (c == u'\'') {
        out += u'"';
        state = State::Normal;
      } else {
        out += c;
      }
      break;
    case State::LineComment:
      if (c == u'\n' || c == u'\r') {
        out += c;
        state = State::Normal;
      } else {
        out += u' ';
      }
      break;
    case State::BlockComment:
      if (c == u'*' && next == u'/') {
        out += QStringLiteral("  ");
        ++i;
        state = State::Normal;
      } else {
        out += (c == u'\n' || c == u'\r') ? c : u' ';
      }
      break;
    }
  }

  if (state == State::SingleString || state == State::DoubleString)
    error = QStringLiteral("Unterminated string");
  else if (state == State::BlockComment)
    error = QStringLiteral("Unterminated block comment");
  return out;
}

QString quoteKeysAndRemoveTrailingCommas(const QString &input) {
  QString out;
  out.reserve(input.size() + 32);
  bool inString = false;

  for (qsizetype i = 0; i < input.size();) {
    const QChar c = input.at(i);
    if (inString) {
      out += c;
      if (c == u'\\' && i + 1 < input.size())
        out += input.at(++i);
      else if (c == u'"')
        inString = false;
      ++i;
      continue;
    }
    if (c == u'"') {
      inString = true;
      out += c;
      ++i;
      continue;
    }

    if (c == u',') {
      qsizetype j = i + 1;
      while (j < input.size() && input.at(j).isSpace())
        ++j;
      if (j < input.size() && (input.at(j) == u'}' || input.at(j) == u']')) {
        ++i;
        continue;
      }
    }

    const bool keyPosition =
        isIdentifierStart(c) && ([&] {
          qsizetype p = out.size() - 1;
          while (p >= 0 && out.at(p).isSpace())
            --p;
          return p < 0 || out.at(p) == u'{' || out.at(p) == u',';
        })();

    if (keyPosition) {
      qsizetype j = i + 1;
      while (j < input.size() && isIdentifierPart(input.at(j)))
        ++j;
      qsizetype k = j;
      while (k < input.size() && input.at(k).isSpace())
        ++k;
      if (k < input.size() && input.at(k) == u':') {
        out += u'"';
        out += input.mid(i, j - i);
        out += u'"';
        i = j;
        continue;
      }
    }

    out += c;
    ++i;
  }
  return out;
}

} // namespace

Json5Result Json5::parse(const QByteArray &source) {
  Json5Result result;
  QString normalizeError;
  const QString input = QString::fromUtf8(source);
  const QString noComments =
      normalizeStringsAndComments(input, result.hadComments, normalizeError);
  if (!normalizeError.isEmpty()) {
    result.error = normalizeError;
    return result;
  }

  const QString normalized = quoteKeysAndRemoveTrailingCommas(noComments);
  QJsonParseError parseError;
  result.document = QJsonDocument::fromJson(normalized.toUtf8(), &parseError);
  if (parseError.error != QJsonParseError::NoError) {
    result.error = QStringLiteral("JSON5 parse error at offset %1: %2")
                       .arg(parseError.offset)
                       .arg(parseError.errorString());
  }
  return result;
}

QByteArray Json5::serialize(const QJsonDocument &document,
                            QJsonDocument::JsonFormat format) {
  const QString json = QString::fromUtf8(document.toJson(format));
  QString json5;
  json5.reserve(json.size());

  for (qsizetype index = 0; index < json.size();) {
    if (json.at(index) != u'"') {
      json5 += json.at(index++);
      continue;
    }

    qsizetype end = index + 1;
    while (end < json.size()) {
      if (json.at(end) == u'\\' && end + 1 < json.size()) {
        end += 2;
        continue;
      }
      if (json.at(end++) == u'"')
        break;
    }
    if (end > json.size() || json.at(end - 1) != u'"') {
      // QJsonDocument always emits complete strings. Retaining the remainder
      // is safer than producing malformed output if that invariant changes.
      json5 += json.mid(index);
      break;
    }

    qsizetype next = end;
    while (next < json.size() && json.at(next).isSpace())
      ++next;
    const QStringView contents(json.constData() + index + 1, end - index - 2);
    const bool objectKey = next < json.size() && json.at(next) == u':';
    if (objectKey && isSafeSerializedIdentifier(contents))
      json5 += contents;
    else
      json5 += QStringView(json).mid(index, end - index);
    index = end;
  }
  return json5.toUtf8();
}

} // namespace yauml
